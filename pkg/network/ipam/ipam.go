// Package ipam provides IP Address Management for overlay networks.
package ipam

import (
	"context"
	"encoding/binary"
	"encoding/json"
	"fmt"
	"net"
	"sync"
	"time"

	"go.uber.org/zap"

	"hypervisor/pkg/cluster/etcd"
	"hypervisor/pkg/network"
)

const (
	subnetKeyPrefix     = "/hypervisor/network/subnets/"
	allocationKeyPrefix = "/hypervisor/network/allocations/"
)

// IPAM provides IP address management for virtual networks.
type IPAM struct {
	etcdClient *etcd.Client
	logger     *zap.Logger

	// Local cache of subnets
	subnets   map[string]*network.Subnet
	subnetsMu sync.RWMutex

	// Local allocation tracking
	allocations   map[string]*network.IPAllocation // indexed by IP
	allocationsMu sync.RWMutex
}

// NewIPAM creates a new IPAM instance.
func NewIPAM(etcdClient *etcd.Client, logger *zap.Logger) *IPAM {
	return &IPAM{
		etcdClient:  etcdClient,
		logger:      logger,
		subnets:     make(map[string]*network.Subnet),
		allocations: make(map[string]*network.IPAllocation),
	}
}

// CreateSubnet creates a new subnet for IP allocation.
func (i *IPAM) CreateSubnet(ctx context.Context, subnet *network.Subnet) error {
	// Validate CIDR
	_, ipNet, err := net.ParseCIDR(subnet.CIDR)
	if err != nil {
		return fmt.Errorf("invalid CIDR: %w", err)
	}

	// Validate gateway
	if subnet.GatewayIP != "" {
		gwIP := net.ParseIP(subnet.GatewayIP)
		if gwIP == nil {
			return fmt.Errorf("invalid gateway IP: %s", subnet.GatewayIP)
		}
		if !ipNet.Contains(gwIP) {
			return fmt.Errorf("gateway IP %s not in subnet %s", subnet.GatewayIP, subnet.CIDR)
		}
	}

	// Generate allocation pools if not specified
	if len(subnet.AllocationPools) == 0 {
		pool := i.generateDefaultPool(ipNet, subnet.GatewayIP)
		subnet.AllocationPools = []network.IPPool{pool}
	}

	// Validate allocation pools
	for _, pool := range subnet.AllocationPools {
		startIP := net.ParseIP(pool.Start)
		endIP := net.ParseIP(pool.End)
		if startIP == nil || endIP == nil {
			return fmt.Errorf("invalid IP pool: %s - %s", pool.Start, pool.End)
		}
		if !ipNet.Contains(startIP) || !ipNet.Contains(endIP) {
			return fmt.Errorf("IP pool %s-%s not in subnet %s", pool.Start, pool.End, subnet.CIDR)
		}
	}

	subnet.CreatedAt = time.Now()
	subnet.UpdatedAt = time.Now()

	// Store in etcd
	key := subnetKeyPrefix + subnet.ID
	data, err := json.Marshal(subnet)
	if err != nil {
		return fmt.Errorf("failed to marshal subnet: %w", err)
	}

	if err := i.etcdClient.Put(ctx, key, string(data)); err != nil {
		return fmt.Errorf("failed to store subnet: %w", err)
	}

	// Update local cache
	i.subnetsMu.Lock()
	i.subnets[subnet.ID] = subnet
	i.subnetsMu.Unlock()

	i.logger.Info("created subnet",
		zap.String("subnet_id", subnet.ID),
		zap.String("cidr", subnet.CIDR),
		zap.String("network_id", subnet.NetworkID),
	)

	return nil
}

// generateDefaultPool creates a default allocation pool for a subnet.
func (i *IPAM) generateDefaultPool(ipNet *net.IPNet, gatewayIP string) network.IPPool {
	// Get network and broadcast addresses
	networkIP := ipNet.IP.Mask(ipNet.Mask)
	broadcastIP := make(net.IP, len(networkIP))
	copy(broadcastIP, networkIP)
	for j := range broadcastIP {
		broadcastIP[j] |= ^ipNet.Mask[j]
	}

	// Start from network+2 (skip network and gateway)
	startIP := incrementIP(networkIP)
	startIP = incrementIP(startIP)

	// End at broadcast-1
	endIP := decrementIP(broadcastIP)

	// If gateway is in the middle, we'd need to split the pool
	// For simplicity, just use contiguous range
	return network.IPPool{
		Start: startIP.String(),
		End:   endIP.String(),
	}
}

// DeleteSubnet removes a subnet.
func (i *IPAM) DeleteSubnet(ctx context.Context, subnetID string) error {
	// Check for existing allocations
	i.allocationsMu.RLock()
	for _, alloc := range i.allocations {
		if alloc.SubnetID == subnetID {
			i.allocationsMu.RUnlock()
			return fmt.Errorf("subnet has active allocations, cannot delete")
		}
	}
	i.allocationsMu.RUnlock()

	// Delete from etcd
	key := subnetKeyPrefix + subnetID
	if err := i.etcdClient.Delete(ctx, key); err != nil {
		return fmt.Errorf("failed to delete subnet: %w", err)
	}

	// Remove from cache
	i.subnetsMu.Lock()
	delete(i.subnets, subnetID)
	i.subnetsMu.Unlock()

	i.logger.Info("deleted subnet", zap.String("subnet_id", subnetID))
	return nil
}

// GetSubnet retrieves a subnet by ID.
func (i *IPAM) GetSubnet(ctx context.Context, subnetID string) (*network.Subnet, error) {
	// Check cache first
	i.subnetsMu.RLock()
	if subnet, exists := i.subnets[subnetID]; exists {
		i.subnetsMu.RUnlock()
		return subnet, nil
	}
	i.subnetsMu.RUnlock()

	// Fetch from etcd
	key := subnetKeyPrefix + subnetID
	value, err := i.etcdClient.Get(ctx, key)
	if err != nil {
		return nil, fmt.Errorf("failed to get subnet: %w", err)
	}
	if value == "" {
		return nil, fmt.Errorf("subnet not found: %s", subnetID)
	}

	var subnet network.Subnet
	if err := json.Unmarshal([]byte(value), &subnet); err != nil {
		return nil, fmt.Errorf("failed to unmarshal subnet: %w", err)
	}

	// Update cache
	i.subnetsMu.Lock()
	i.subnets[subnetID] = &subnet
	i.subnetsMu.Unlock()

	return &subnet, nil
}

// ListSubnets returns all subnets, optionally filtered by network ID.
func (i *IPAM) ListSubnets(ctx context.Context, networkID string) ([]*network.Subnet, error) {
	kvs, err := i.etcdClient.GetWithPrefixKV(ctx, subnetKeyPrefix)
	if err != nil {
		return nil, fmt.Errorf("failed to list subnets: %w", err)
	}

	subnets := make([]*network.Subnet, 0, len(kvs))
	for _, kv := range kvs {
		var subnet network.Subnet
		if err := json.Unmarshal([]byte(kv.Value), &subnet); err != nil {
			i.logger.Warn("failed to unmarshal subnet", zap.Error(err))
			continue
		}
		if networkID == "" || subnet.NetworkID == networkID {
			subnets = append(subnets, &subnet)
		}
	}

	return subnets, nil
}

// AllocateIP allocates an IP address from a subnet.
func (i *IPAM) AllocateIP(ctx context.Context, subnetID string, opts AllocationOptions) (*network.IPAllocation, error) {
	subnet, err := i.GetSubnet(ctx, subnetID)
	if err != nil {
		return nil, err
	}

	// If specific IP requested, try to allocate it
	if opts.IPAddress != "" {
		return i.allocateSpecificIP(ctx, subnet, opts)
	}

	// Find next available IP
	return i.allocateNextIP(ctx, subnet, opts)
}

// AllocationOptions specifies options for IP allocation.
type AllocationOptions struct {
	IPAddress  string // Specific IP to allocate (optional)
	MACAddress string
	InstanceID string
	PortID     string
	Hostname   string
}

// allocateSpecificIP tries to allocate a specific IP address.
func (i *IPAM) allocateSpecificIP(ctx context.Context, subnet *network.Subnet, opts AllocationOptions) (*network.IPAllocation, error) {
	ip := net.ParseIP(opts.IPAddress)
	if ip == nil {
		return nil, fmt.Errorf("invalid IP address: %s", opts.IPAddress)
	}

	// Check if IP is in subnet
	_, ipNet, _ := net.ParseCIDR(subnet.CIDR)
	if !ipNet.Contains(ip) {
		return nil, fmt.Errorf("IP %s not in subnet %s", opts.IPAddress, subnet.CIDR)
	}

	// Check if IP is in allocation pool
	if !i.isIPInPools(opts.IPAddress, subnet.AllocationPools) {
		return nil, fmt.Errorf("IP %s not in allocation pools", opts.IPAddress)
	}

	// Check if IP is already allocated (use etcd transaction for atomicity)
	allocKey := fmt.Sprintf("%s%s/%s", allocationKeyPrefix, subnet.ID, opts.IPAddress)

	allocation := &network.IPAllocation{
		ID:         fmt.Sprintf("%s-%s", subnet.ID, opts.IPAddress),
		SubnetID:   subnet.ID,
		IPAddress:  opts.IPAddress,
		MACAddress: opts.MACAddress,
		InstanceID: opts.InstanceID,
		PortID:     opts.PortID,
		Hostname:   opts.Hostname,
		Status:     "allocated",
		CreatedAt:  time.Now(),
	}

	data, err := json.Marshal(allocation)
	if err != nil {
		return nil, fmt.Errorf("failed to marshal allocation: %w", err)
	}

	// Try to create (will fail if already exists)
	created, err := i.etcdClient.CreateIfNotExists(ctx, allocKey, string(data))
	if err != nil {
		return nil, fmt.Errorf("failed to store allocation: %w", err)
	}
	if !created {
		return nil, fmt.Errorf("IP %s already allocated", opts.IPAddress)
	}

	// Update local cache
	i.allocationsMu.Lock()
	i.allocations[opts.IPAddress] = allocation
	i.allocationsMu.Unlock()

	i.logger.Info("allocated IP",
		zap.String("ip", opts.IPAddress),
		zap.String("subnet_id", subnet.ID),
		zap.String("instance_id", opts.InstanceID),
	)

	return allocation, nil
}

// allocateNextIP finds and allocates the next available IP.
func (i *IPAM) allocateNextIP(ctx context.Context, subnet *network.Subnet, opts AllocationOptions) (*network.IPAllocation, error) {
	// Get existing allocations for this subnet
	allocPrefix := fmt.Sprintf("%s%s/", allocationKeyPrefix, subnet.ID)
	kvs, err := i.etcdClient.GetWithPrefixKV(ctx, allocPrefix)
	if err != nil {
		return nil, fmt.Errorf("failed to list allocations: %w", err)
	}

	allocated := make(map[string]bool)
	for _, kv := range kvs {
		var alloc network.IPAllocation
		if err := json.Unmarshal([]byte(kv.Value), &alloc); err == nil {
			allocated[alloc.IPAddress] = true
		}
	}

	// Also mark gateway as allocated
	if subnet.GatewayIP != "" {
		allocated[subnet.GatewayIP] = true
	}

	// Find first available IP in pools
	for _, pool := range subnet.AllocationPools {
		ip := net.ParseIP(pool.Start)
		endIP := net.ParseIP(pool.End)

		for ; !ip.Equal(endIP); ip = incrementIP(ip) {
			ipStr := ip.String()
			if !allocated[ipStr] {
				opts.IPAddress = ipStr
				return i.allocateSpecificIP(ctx, subnet, opts)
			}
		}
		// Check end IP too
		if !allocated[endIP.String()] {
			opts.IPAddress = endIP.String()
			return i.allocateSpecificIP(ctx, subnet, opts)
		}
	}

	return nil, fmt.Errorf("no available IPs in subnet %s", subnet.ID)
}

// ReleaseIP releases an allocated IP address.
func (i *IPAM) ReleaseIP(ctx context.Context, subnetID, ipAddress string) error {
	allocKey := fmt.Sprintf("%s%s/%s", allocationKeyPrefix, subnetID, ipAddress)

	if err := i.etcdClient.Delete(ctx, allocKey); err != nil {
		return fmt.Errorf("failed to release IP: %w", err)
	}

	// Update local cache
	i.allocationsMu.Lock()
	delete(i.allocations, ipAddress)
	i.allocationsMu.Unlock()

	i.logger.Info("released IP",
		zap.String("ip", ipAddress),
		zap.String("subnet_id", subnetID),
	)

	return nil
}

// GetAllocation retrieves an IP allocation.
func (i *IPAM) GetAllocation(ctx context.Context, subnetID, ipAddress string) (*network.IPAllocation, error) {
	allocKey := fmt.Sprintf("%s%s/%s", allocationKeyPrefix, subnetID, ipAddress)

	value, err := i.etcdClient.Get(ctx, allocKey)
	if err != nil {
		return nil, fmt.Errorf("failed to get allocation: %w", err)
	}
	if value == "" {
		return nil, fmt.Errorf("allocation not found: %s", ipAddress)
	}

	var alloc network.IPAllocation
	if err := json.Unmarshal([]byte(value), &alloc); err != nil {
		return nil, fmt.Errorf("failed to unmarshal allocation: %w", err)
	}

	return &alloc, nil
}

// ListAllocations returns all allocations for a subnet.
func (i *IPAM) ListAllocations(ctx context.Context, subnetID string) ([]*network.IPAllocation, error) {
	allocPrefix := fmt.Sprintf("%s%s/", allocationKeyPrefix, subnetID)

	kvs, err := i.etcdClient.GetWithPrefixKV(ctx, allocPrefix)
	if err != nil {
		return nil, fmt.Errorf("failed to list allocations: %w", err)
	}

	allocs := make([]*network.IPAllocation, 0, len(kvs))
	for _, kv := range kvs {
		var alloc network.IPAllocation
		if err := json.Unmarshal([]byte(kv.Value), &alloc); err != nil {
			i.logger.Warn("failed to unmarshal allocation", zap.Error(err))
			continue
		}
		allocs = append(allocs, &alloc)
	}

	return allocs, nil
}

// isIPInPools checks if an IP is within any of the allocation pools.
func (i *IPAM) isIPInPools(ipStr string, pools []network.IPPool) bool {
	ip := net.ParseIP(ipStr)
	if ip == nil {
		return false
	}

	for _, pool := range pools {
		startIP := net.ParseIP(pool.Start)
		endIP := net.ParseIP(pool.End)
		if ipInRange(ip, startIP, endIP) {
			return true
		}
	}
	return false
}

// ipInRange checks if an IP is within a range (inclusive).
func ipInRange(ip, start, end net.IP) bool {
	ip4 := ip.To4()
	start4 := start.To4()
	end4 := end.To4()

	if ip4 == nil || start4 == nil || end4 == nil {
		// Handle IPv6 or mixed
		return false
	}

	ipInt := binary.BigEndian.Uint32(ip4)
	startInt := binary.BigEndian.Uint32(start4)
	endInt := binary.BigEndian.Uint32(end4)

	return ipInt >= startInt && ipInt <= endInt
}

// incrementIP returns the next IP address.
func incrementIP(ip net.IP) net.IP {
	result := make(net.IP, len(ip))
	copy(result, ip)

	for i := len(result) - 1; i >= 0; i-- {
		result[i]++
		if result[i] != 0 {
			break
		}
	}
	return result
}

// decrementIP returns the previous IP address.
func decrementIP(ip net.IP) net.IP {
	result := make(net.IP, len(ip))
	copy(result, ip)

	for i := len(result) - 1; i >= 0; i-- {
		result[i]--
		if result[i] != 0xff {
			break
		}
	}
	return result
}

// LoadSubnets loads all subnets into cache.
func (i *IPAM) LoadSubnets(ctx context.Context) error {
	subnets, err := i.ListSubnets(ctx, "")
	if err != nil {
		return err
	}

	i.subnetsMu.Lock()
	for _, subnet := range subnets {
		i.subnets[subnet.ID] = subnet
	}
	i.subnetsMu.Unlock()

	i.logger.Info("loaded subnets into cache", zap.Int("count", len(subnets)))
	return nil
}
