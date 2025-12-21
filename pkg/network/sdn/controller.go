// Package sdn provides Software-Defined Networking controller functionality.
package sdn

import (
	"context"
	"encoding/json"
	"fmt"
	"sync"
	"time"

	"go.uber.org/zap"

	"hypervisor/pkg/cluster/etcd"
	"hypervisor/pkg/network"
	"hypervisor/pkg/network/ipam"
	"hypervisor/pkg/network/overlay"
)

const (
	networkKeyPrefix       = "/hypervisor/network/networks/"
	portKeyPrefix          = "/hypervisor/network/ports/"
	securityGroupKeyPrefix = "/hypervisor/network/security-groups/"
	routerKeyPrefix        = "/hypervisor/network/routers/"
	floatingIPKeyPrefix    = "/hypervisor/network/floating-ips/"
)

// Controller is the SDN controller for the hypervisor.
type Controller struct {
	config      *network.NetworkConfig
	logger      *zap.Logger
	etcdClient  *etcd.Client

	// Managers
	vxlanMgr    *overlay.VXLANManager
	vtepMgr     *overlay.VTEPManager
	ipam        *ipam.IPAM
	flowMgr     *FlowManager

	// Local state
	networks       map[string]*network.Network
	networksMu     sync.RWMutex

	ports          map[string]*network.Port
	portsMu        sync.RWMutex

	securityGroups map[string]*network.SecurityGroup
	sgMu           sync.RWMutex

	routers        map[string]*network.Router
	routersMu      sync.RWMutex

	floatingIPs    map[string]*network.FloatingIP
	fipMu          sync.RWMutex

	ctx            context.Context
	cancel         context.CancelFunc
	wg             sync.WaitGroup
}

// NewController creates a new SDN controller.
func NewController(
	config *network.NetworkConfig,
	etcdClient *etcd.Client,
	vxlanMgr *overlay.VXLANManager,
	vtepMgr *overlay.VTEPManager,
	ipam *ipam.IPAM,
	logger *zap.Logger,
) (*Controller, error) {
	if config == nil {
		config = network.DefaultNetworkConfig()
	}

	ctx, cancel := context.WithCancel(context.Background())

	flowMgr, err := NewFlowManager(config, logger)
	if err != nil {
		cancel()
		return nil, fmt.Errorf("failed to create flow manager: %w", err)
	}

	c := &Controller{
		config:         config,
		logger:         logger,
		etcdClient:     etcdClient,
		vxlanMgr:       vxlanMgr,
		vtepMgr:        vtepMgr,
		ipam:           ipam,
		flowMgr:        flowMgr,
		networks:       make(map[string]*network.Network),
		ports:          make(map[string]*network.Port),
		securityGroups: make(map[string]*network.SecurityGroup),
		routers:        make(map[string]*network.Router),
		floatingIPs:    make(map[string]*network.FloatingIP),
		ctx:            ctx,
		cancel:         cancel,
	}

	return c, nil
}

// Start starts the SDN controller.
func (c *Controller) Start() error {
	c.logger.Info("starting SDN controller")

	// Load existing state from etcd
	if err := c.loadState(); err != nil {
		return fmt.Errorf("failed to load state: %w", err)
	}

	// Start watching for changes
	c.wg.Add(1)
	go c.watchNetworks()

	c.logger.Info("SDN controller started")
	return nil
}

// loadState loads all network state from etcd.
func (c *Controller) loadState() error {
	ctx, cancel := context.WithTimeout(c.ctx, 30*time.Second)
	defer cancel()

	// Load networks
	kvs, err := c.etcdClient.GetWithPrefixKV(ctx, networkKeyPrefix)
	if err != nil {
		return fmt.Errorf("failed to load networks: %w", err)
	}
	c.networksMu.Lock()
	for _, kv := range kvs {
		var net network.Network
		if err := json.Unmarshal([]byte(kv.Value), &net); err != nil {
			c.logger.Warn("failed to unmarshal network", zap.Error(err))
			continue
		}
		c.networks[net.ID] = &net

		// Register with VXLAN manager if applicable
		if net.Type == network.NetworkTypeVXLAN {
			if err := c.vxlanMgr.RegisterNetwork(&net); err != nil {
				c.logger.Warn("failed to register network with VXLAN manager",
					zap.String("network_id", net.ID),
					zap.Error(err),
				)
			}
		}
	}
	c.networksMu.Unlock()
	c.logger.Info("loaded networks", zap.Int("count", len(kvs)))

	// Load ports
	kvs, err = c.etcdClient.GetWithPrefixKV(ctx, portKeyPrefix)
	if err != nil {
		return fmt.Errorf("failed to load ports: %w", err)
	}
	c.portsMu.Lock()
	for _, kv := range kvs {
		var port network.Port
		if err := json.Unmarshal([]byte(kv.Value), &port); err != nil {
			c.logger.Warn("failed to unmarshal port", zap.Error(err))
			continue
		}
		c.ports[port.ID] = &port
	}
	c.portsMu.Unlock()
	c.logger.Info("loaded ports", zap.Int("count", len(kvs)))

	// Load security groups
	kvs, err = c.etcdClient.GetWithPrefixKV(ctx, securityGroupKeyPrefix)
	if err != nil {
		return fmt.Errorf("failed to load security groups: %w", err)
	}
	c.sgMu.Lock()
	for _, kv := range kvs {
		var sg network.SecurityGroup
		if err := json.Unmarshal([]byte(kv.Value), &sg); err != nil {
			c.logger.Warn("failed to unmarshal security group", zap.Error(err))
			continue
		}
		c.securityGroups[sg.ID] = &sg
	}
	c.sgMu.Unlock()
	c.logger.Info("loaded security groups", zap.Int("count", len(kvs)))

	// Load subnets into IPAM
	if err := c.ipam.LoadSubnets(ctx); err != nil {
		return fmt.Errorf("failed to load subnets: %w", err)
	}

	return nil
}

// watchNetworks watches for network changes in etcd.
func (c *Controller) watchNetworks() {
	defer c.wg.Done()

	watchCh := c.etcdClient.WatchPrefixEvents(c.ctx, networkKeyPrefix)

	for {
		select {
		case <-c.ctx.Done():
			return
		case event, ok := <-watchCh:
			if !ok {
				c.logger.Warn("network watch channel closed, reconnecting...")
				time.Sleep(time.Second)
				watchCh = c.etcdClient.WatchPrefixEvents(c.ctx, networkKeyPrefix)
				continue
			}

			c.handleNetworkEvent(event)
		}
	}
}

// handleNetworkEvent processes a network change event.
func (c *Controller) handleNetworkEvent(event etcd.WatchEvent) {
	networkID := event.Key[len(networkKeyPrefix):]

	switch event.Type {
	case etcd.EventTypePut:
		var net network.Network
		if err := json.Unmarshal([]byte(event.Value), &net); err != nil {
			c.logger.Warn("failed to unmarshal network event", zap.Error(err))
			return
		}

		c.networksMu.Lock()
		c.networks[net.ID] = &net
		c.networksMu.Unlock()

		// Register with VXLAN manager
		if net.Type == network.NetworkTypeVXLAN {
			if err := c.vxlanMgr.RegisterNetwork(&net); err != nil {
				c.logger.Error("failed to register network",
					zap.String("network_id", net.ID),
					zap.Error(err),
				)
			}

			// Establish tunnel mesh for this VNI
			if err := c.vtepMgr.EstablishMesh(net.VNI); err != nil {
				c.logger.Warn("failed to establish tunnel mesh",
					zap.String("network_id", net.ID),
					zap.Uint32("vni", net.VNI),
					zap.Error(err),
				)
			}
		}

		c.logger.Info("network registered",
			zap.String("network_id", net.ID),
			zap.String("type", string(net.Type)),
		)

	case etcd.EventTypeDelete:
		c.networksMu.Lock()
		net, exists := c.networks[networkID]
		delete(c.networks, networkID)
		c.networksMu.Unlock()

		if exists && net.Type == network.NetworkTypeVXLAN {
			// Unregister from VXLAN manager
			c.vxlanMgr.UnregisterNetwork(networkID)

			// Teardown tunnel mesh
			if err := c.vtepMgr.TeardownMesh(net.VNI); err != nil {
				c.logger.Warn("failed to teardown tunnel mesh",
					zap.String("network_id", networkID),
					zap.Error(err),
				)
			}
		}

		c.logger.Info("network unregistered", zap.String("network_id", networkID))
	}
}

// CreateNetwork creates a new virtual network.
func (c *Controller) CreateNetwork(ctx context.Context, net *network.Network) error {
	// Validate
	if net.Type == network.NetworkTypeVXLAN {
		if net.VNI == 0 || net.VNI > 16777215 {
			return fmt.Errorf("invalid VNI: %d (must be 1-16777215)", net.VNI)
		}
	}

	if net.MTU == 0 {
		if net.Type == network.NetworkTypeVXLAN {
			net.MTU = 1450 // VXLAN overhead
		} else {
			net.MTU = 1500
		}
	}

	net.AdminState = true
	net.CreatedAt = time.Now()
	net.UpdatedAt = time.Now()

	// Store in etcd
	key := networkKeyPrefix + net.ID
	data, err := json.Marshal(net)
	if err != nil {
		return fmt.Errorf("failed to marshal network: %w", err)
	}

	if err := c.etcdClient.Put(ctx, key, string(data)); err != nil {
		return fmt.Errorf("failed to store network: %w", err)
	}

	c.logger.Info("created network",
		zap.String("network_id", net.ID),
		zap.String("name", net.Name),
		zap.String("type", string(net.Type)),
		zap.Uint32("vni", net.VNI),
	)

	return nil
}

// GetNetwork retrieves a network by ID.
func (c *Controller) GetNetwork(ctx context.Context, networkID string) (*network.Network, error) {
	c.networksMu.RLock()
	if net, exists := c.networks[networkID]; exists {
		c.networksMu.RUnlock()
		return net, nil
	}
	c.networksMu.RUnlock()

	// Try etcd
	key := networkKeyPrefix + networkID
	value, err := c.etcdClient.Get(ctx, key)
	if err != nil {
		return nil, fmt.Errorf("failed to get network: %w", err)
	}
	if value == "" {
		return nil, fmt.Errorf("network not found: %s", networkID)
	}

	var net network.Network
	if err := json.Unmarshal([]byte(value), &net); err != nil {
		return nil, fmt.Errorf("failed to unmarshal network: %w", err)
	}

	return &net, nil
}

// ListNetworks returns all networks.
func (c *Controller) ListNetworks(ctx context.Context, tenantID string) ([]*network.Network, error) {
	c.networksMu.RLock()
	defer c.networksMu.RUnlock()

	networks := make([]*network.Network, 0, len(c.networks))
	for _, net := range c.networks {
		if tenantID == "" || net.TenantID == tenantID || net.Shared {
			networks = append(networks, net)
		}
	}

	return networks, nil
}

// DeleteNetwork deletes a network.
func (c *Controller) DeleteNetwork(ctx context.Context, networkID string) error {
	// Check for existing ports
	c.portsMu.RLock()
	for _, port := range c.ports {
		if port.NetworkID == networkID {
			c.portsMu.RUnlock()
			return fmt.Errorf("network has active ports, cannot delete")
		}
	}
	c.portsMu.RUnlock()

	// Delete from etcd
	key := networkKeyPrefix + networkID
	if err := c.etcdClient.Delete(ctx, key); err != nil {
		return fmt.Errorf("failed to delete network: %w", err)
	}

	c.logger.Info("deleted network", zap.String("network_id", networkID))
	return nil
}

// CreatePort creates a new virtual port.
func (c *Controller) CreatePort(ctx context.Context, port *network.Port) error {
	// Get network
	net, err := c.GetNetwork(ctx, port.NetworkID)
	if err != nil {
		return fmt.Errorf("network not found: %w", err)
	}

	// Allocate IP if not specified
	if port.IPAddress == "" && port.SubnetID != "" {
		alloc, err := c.ipam.AllocateIP(ctx, port.SubnetID, ipam.AllocationOptions{
			MACAddress: port.MACAddress,
			PortID:     port.ID,
		})
		if err != nil {
			return fmt.Errorf("failed to allocate IP: %w", err)
		}
		port.IPAddress = alloc.IPAddress
	}

	// Generate MAC if not specified
	if port.MACAddress == "" {
		port.MACAddress = generateMAC()
	}

	port.Status = "build"
	port.AdminState = true
	port.CreatedAt = time.Now()
	port.UpdatedAt = time.Now()

	// Store in etcd
	key := portKeyPrefix + port.ID
	data, err := json.Marshal(port)
	if err != nil {
		return fmt.Errorf("failed to marshal port: %w", err)
	}

	if err := c.etcdClient.Put(ctx, key, string(data)); err != nil {
		return fmt.Errorf("failed to store port: %w", err)
	}

	// Update cache
	c.portsMu.Lock()
	c.ports[port.ID] = port
	c.portsMu.Unlock()

	c.logger.Info("created port",
		zap.String("port_id", port.ID),
		zap.String("network_id", port.NetworkID),
		zap.String("ip_address", port.IPAddress),
		zap.String("mac_address", port.MACAddress),
	)

	// Install flow rules for this port
	if net.Type == network.NetworkTypeVXLAN {
		if err := c.flowMgr.InstallPortFlows(port, net); err != nil {
			c.logger.Warn("failed to install port flows",
				zap.String("port_id", port.ID),
				zap.Error(err),
			)
		}
	}

	return nil
}

// BindPort binds a port to an instance and node.
func (c *Controller) BindPort(ctx context.Context, portID, instanceID, nodeID, deviceName string) error {
	c.portsMu.Lock()
	port, exists := c.ports[portID]
	if !exists {
		c.portsMu.Unlock()
		return fmt.Errorf("port not found: %s", portID)
	}

	port.InstanceID = instanceID
	port.NodeID = nodeID
	port.DeviceName = deviceName
	port.Status = "active"
	port.UpdatedAt = time.Now()
	c.portsMu.Unlock()

	// Update in etcd
	key := portKeyPrefix + portID
	data, err := json.Marshal(port)
	if err != nil {
		return fmt.Errorf("failed to marshal port: %w", err)
	}

	if err := c.etcdClient.Put(ctx, key, string(data)); err != nil {
		return fmt.Errorf("failed to update port: %w", err)
	}

	c.logger.Info("bound port",
		zap.String("port_id", portID),
		zap.String("instance_id", instanceID),
		zap.String("node_id", nodeID),
	)

	// Update IP allocation
	if port.SubnetID != "" && port.IPAddress != "" {
		alloc, err := c.ipam.GetAllocation(ctx, port.SubnetID, port.IPAddress)
		if err == nil {
			alloc.InstanceID = instanceID
			// Re-allocate to update
			c.ipam.ReleaseIP(ctx, port.SubnetID, port.IPAddress)
			c.ipam.AllocateIP(ctx, port.SubnetID, ipam.AllocationOptions{
				IPAddress:  port.IPAddress,
				MACAddress: port.MACAddress,
				InstanceID: instanceID,
				PortID:     portID,
			})
		}
	}

	return nil
}

// DeletePort deletes a port.
func (c *Controller) DeletePort(ctx context.Context, portID string) error {
	c.portsMu.Lock()
	port, exists := c.ports[portID]
	if exists {
		delete(c.ports, portID)
	}
	c.portsMu.Unlock()

	if !exists {
		return fmt.Errorf("port not found: %s", portID)
	}

	// Release IP
	if port.SubnetID != "" && port.IPAddress != "" {
		if err := c.ipam.ReleaseIP(ctx, port.SubnetID, port.IPAddress); err != nil {
			c.logger.Warn("failed to release IP",
				zap.String("ip", port.IPAddress),
				zap.Error(err),
			)
		}
	}

	// Remove flow rules
	if err := c.flowMgr.RemovePortFlows(port); err != nil {
		c.logger.Warn("failed to remove port flows",
			zap.String("port_id", portID),
			zap.Error(err),
		)
	}

	// Delete from etcd
	key := portKeyPrefix + portID
	if err := c.etcdClient.Delete(ctx, key); err != nil {
		return fmt.Errorf("failed to delete port: %w", err)
	}

	c.logger.Info("deleted port", zap.String("port_id", portID))
	return nil
}

// GetPort retrieves a port by ID.
func (c *Controller) GetPort(ctx context.Context, portID string) (*network.Port, error) {
	c.portsMu.RLock()
	if port, exists := c.ports[portID]; exists {
		c.portsMu.RUnlock()
		return port, nil
	}
	c.portsMu.RUnlock()

	return nil, fmt.Errorf("port not found: %s", portID)
}

// ListPorts returns ports with optional filters.
func (c *Controller) ListPorts(ctx context.Context, networkID, instanceID, nodeID string) ([]*network.Port, error) {
	c.portsMu.RLock()
	defer c.portsMu.RUnlock()

	ports := make([]*network.Port, 0)
	for _, port := range c.ports {
		if networkID != "" && port.NetworkID != networkID {
			continue
		}
		if instanceID != "" && port.InstanceID != instanceID {
			continue
		}
		if nodeID != "" && port.NodeID != nodeID {
			continue
		}
		ports = append(ports, port)
	}

	return ports, nil
}

// Stop stops the SDN controller.
func (c *Controller) Stop() error {
	c.logger.Info("stopping SDN controller")

	c.cancel()
	c.wg.Wait()

	if err := c.flowMgr.Close(); err != nil {
		c.logger.Warn("failed to close flow manager", zap.Error(err))
	}

	return nil
}

// generateMAC generates a random MAC address with the local bit set.
func generateMAC() string {
	// Use locally administered address (second hex digit is 2, 6, A, or E)
	// Format: fa:16:3e:xx:xx:xx (OpenStack style)
	return fmt.Sprintf("fa:16:3e:%02x:%02x:%02x",
		uint8(time.Now().UnixNano()>>16),
		uint8(time.Now().UnixNano()>>8),
		uint8(time.Now().UnixNano()),
	)
}
