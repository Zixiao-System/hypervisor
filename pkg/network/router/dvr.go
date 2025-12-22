// Package router provides distributed virtual router functionality.
package router

import (
	"context"
	"encoding/json"
	"fmt"
	"net"
	"os/exec"
	"sync"
	"time"

	"go.uber.org/zap"

	"hypervisor/pkg/cluster/etcd"
	"hypervisor/pkg/network"
)

const (
	routerKeyPrefix    = "/hypervisor/network/routers/"
	interfaceKeyPrefix = "/hypervisor/network/router-interfaces/"
)

// DVR implements a Distributed Virtual Router.
type DVR struct {
	config     *network.NetworkConfig
	logger     *zap.Logger
	etcdClient *etcd.Client
	nodeID     string

	// Router namespaces on this node
	namespaces map[string]*RouterNamespace
	nsMu       sync.RWMutex

	// Active routers
	routers   map[string]*network.Router
	routersMu sync.RWMutex

	// Router interfaces (subnet attachments)
	interfaces   map[string][]*RouterInterface
	interfacesMu sync.RWMutex

	ctx    context.Context
	cancel context.CancelFunc
	wg     sync.WaitGroup
}

// RouterNamespace represents a router namespace on a node.
type RouterNamespace struct {
	RouterID   string
	Name       string // e.g., "qrouter-<router-id>"
	Interfaces []string
	Created    time.Time
}

// RouterInterface represents a router's connection to a subnet.
type RouterInterface struct {
	RouterID   string
	SubnetID   string
	PortID     string
	IPAddress  string
	MACAddress string
	VNI        uint32
}

// NewDVR creates a new distributed virtual router.
func NewDVR(
	config *network.NetworkConfig,
	etcdClient *etcd.Client,
	nodeID string,
	logger *zap.Logger,
) *DVR {
	ctx, cancel := context.WithCancel(context.Background())

	return &DVR{
		config:     config,
		logger:     logger,
		etcdClient: etcdClient,
		nodeID:     nodeID,
		namespaces: make(map[string]*RouterNamespace),
		routers:    make(map[string]*network.Router),
		interfaces: make(map[string][]*RouterInterface),
		ctx:        ctx,
		cancel:     cancel,
	}
}

// Start starts the DVR service.
func (d *DVR) Start() error {
	d.logger.Info("starting distributed virtual router")

	// Load existing routers
	if err := d.loadRouters(); err != nil {
		return fmt.Errorf("failed to load routers: %w", err)
	}

	// Start watching for router changes
	d.wg.Add(1)
	go d.watchRouters()

	d.logger.Info("DVR started")
	return nil
}

// loadRouters loads all routers from etcd.
func (d *DVR) loadRouters() error {
	ctx, cancel := context.WithTimeout(d.ctx, 10*time.Second)
	defer cancel()

	kvs, err := d.etcdClient.GetWithPrefixKV(ctx, routerKeyPrefix)
	if err != nil {
		return err
	}

	d.routersMu.Lock()
	defer d.routersMu.Unlock()

	for _, kv := range kvs {
		var router network.Router
		if err := json.Unmarshal([]byte(kv.Value), &router); err != nil {
			d.logger.Warn("failed to unmarshal router", zap.Error(err))
			continue
		}
		d.routers[router.ID] = &router

		// Create namespace if distributed
		if router.Distributed {
			if err := d.ensureNamespace(&router); err != nil {
				d.logger.Warn("failed to ensure router namespace",
					zap.String("router_id", router.ID),
					zap.Error(err),
				)
			}
		}
	}

	d.logger.Info("loaded routers", zap.Int("count", len(d.routers)))
	return nil
}

// watchRouters watches for router changes in etcd.
func (d *DVR) watchRouters() {
	defer d.wg.Done()

	watchCh := d.etcdClient.WatchPrefixEvents(d.ctx, routerKeyPrefix)

	for {
		select {
		case <-d.ctx.Done():
			return
		case event, ok := <-watchCh:
			if !ok {
				d.logger.Warn("router watch channel closed, reconnecting...")
				time.Sleep(time.Second)
				watchCh = d.etcdClient.WatchPrefixEvents(d.ctx, routerKeyPrefix)
				continue
			}

			d.handleRouterEvent(event)
		}
	}
}

// handleRouterEvent processes a router change event.
func (d *DVR) handleRouterEvent(event etcd.WatchEvent) {
	routerID := event.Key[len(routerKeyPrefix):]

	switch event.Type {
	case etcd.EventTypePut:
		var router network.Router
		if err := json.Unmarshal([]byte(event.Value), &router); err != nil {
			d.logger.Warn("failed to unmarshal router event", zap.Error(err))
			return
		}

		d.routersMu.Lock()
		d.routers[router.ID] = &router
		d.routersMu.Unlock()

		if router.Distributed {
			if err := d.ensureNamespace(&router); err != nil {
				d.logger.Error("failed to ensure router namespace",
					zap.String("router_id", router.ID),
					zap.Error(err),
				)
			}
		}

		d.logger.Info("router updated", zap.String("router_id", router.ID))

	case etcd.EventTypeDelete:
		d.routersMu.Lock()
		delete(d.routers, routerID)
		d.routersMu.Unlock()

		if err := d.deleteNamespace(routerID); err != nil {
			d.logger.Warn("failed to delete router namespace",
				zap.String("router_id", routerID),
				zap.Error(err),
			)
		}

		d.logger.Info("router deleted", zap.String("router_id", routerID))
	}
}

// ensureNamespace creates a network namespace for a router if it doesn't exist.
func (d *DVR) ensureNamespace(router *network.Router) error {
	d.nsMu.Lock()
	defer d.nsMu.Unlock()

	if _, exists := d.namespaces[router.ID]; exists {
		return nil
	}

	nsName := fmt.Sprintf("%s-%s", d.config.DVRNamespace, router.ID[:8])

	// Create network namespace
	if err := exec.Command("ip", "netns", "add", nsName).Run(); err != nil {
		// Namespace might already exist
		d.logger.Debug("namespace may already exist", zap.String("name", nsName))
	}

	// Enable loopback
	if err := exec.Command("ip", "netns", "exec", nsName, "ip", "link", "set", "lo", "up").Run(); err != nil {
		d.logger.Warn("failed to enable loopback", zap.Error(err))
	}

	// Enable IP forwarding in namespace
	if err := exec.Command("ip", "netns", "exec", nsName, "sysctl", "-w", "net.ipv4.ip_forward=1").Run(); err != nil {
		d.logger.Warn("failed to enable IP forwarding", zap.Error(err))
	}

	d.namespaces[router.ID] = &RouterNamespace{
		RouterID: router.ID,
		Name:     nsName,
		Created:  time.Now(),
	}

	d.logger.Info("created router namespace",
		zap.String("router_id", router.ID),
		zap.String("namespace", nsName),
	)

	return nil
}

// deleteNamespace removes a router's network namespace.
func (d *DVR) deleteNamespace(routerID string) error {
	d.nsMu.Lock()
	defer d.nsMu.Unlock()

	ns, exists := d.namespaces[routerID]
	if !exists {
		return nil
	}

	// Delete network namespace
	if err := exec.Command("ip", "netns", "delete", ns.Name).Run(); err != nil {
		return fmt.Errorf("failed to delete namespace: %w", err)
	}

	delete(d.namespaces, routerID)

	d.logger.Info("deleted router namespace",
		zap.String("router_id", routerID),
		zap.String("namespace", ns.Name),
	)

	return nil
}

// AddRouterInterface adds a subnet interface to a router.
func (d *DVR) AddRouterInterface(ctx context.Context, routerID, subnetID, portID string, ip net.IP, mac string, vni uint32) error {
	d.nsMu.RLock()
	ns, exists := d.namespaces[routerID]
	d.nsMu.RUnlock()

	if !exists {
		return fmt.Errorf("router namespace not found: %s", routerID)
	}

	// Create veth pair
	hostVeth := fmt.Sprintf("qr-%s", portID[:8])
	nsVeth := fmt.Sprintf("qri-%s", portID[:8])

	// Create veth pair
	cmd := exec.Command("ip", "link", "add", hostVeth, "type", "veth", "peer", "name", nsVeth)
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("failed to create veth pair: %w", err)
	}

	// Move ns end into namespace
	cmd = exec.Command("ip", "link", "set", nsVeth, "netns", ns.Name)
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("failed to move veth to namespace: %w", err)
	}

	// Configure interface in namespace
	mask := "/24" // TODO: Get from subnet
	cmd = exec.Command("ip", "netns", "exec", ns.Name, "ip", "addr", "add", ip.String()+mask, "dev", nsVeth)
	if err := cmd.Run(); err != nil {
		d.logger.Warn("failed to add IP to interface", zap.Error(err))
	}

	// Set MAC address
	if mac != "" {
		cmd = exec.Command("ip", "netns", "exec", ns.Name, "ip", "link", "set", nsVeth, "address", mac)
		if err := cmd.Run(); err != nil {
			d.logger.Warn("failed to set MAC address", zap.Error(err))
		}
	}

	// Bring interfaces up
	exec.Command("ip", "link", "set", hostVeth, "up").Run()
	exec.Command("ip", "netns", "exec", ns.Name, "ip", "link", "set", nsVeth, "up").Run()

	// Add host end to OVS bridge
	cmd = exec.Command("ovs-vsctl", "add-port", d.config.OVSBridge, hostVeth,
		"--", "set", "interface", hostVeth, fmt.Sprintf("external_ids:router-id=%s", routerID))
	if err := cmd.Run(); err != nil {
		d.logger.Warn("failed to add veth to OVS", zap.Error(err))
	}

	// Store interface
	iface := &RouterInterface{
		RouterID:   routerID,
		SubnetID:   subnetID,
		PortID:     portID,
		IPAddress:  ip.String(),
		MACAddress: mac,
		VNI:        vni,
	}

	d.interfacesMu.Lock()
	d.interfaces[routerID] = append(d.interfaces[routerID], iface)
	d.nsMu.Lock()
	ns.Interfaces = append(ns.Interfaces, nsVeth)
	d.nsMu.Unlock()
	d.interfacesMu.Unlock()

	d.logger.Info("added router interface",
		zap.String("router_id", routerID),
		zap.String("subnet_id", subnetID),
		zap.String("ip", ip.String()),
	)

	return nil
}

// RemoveRouterInterface removes a subnet interface from a router.
func (d *DVR) RemoveRouterInterface(ctx context.Context, routerID, subnetID string) error {
	d.interfacesMu.Lock()
	defer d.interfacesMu.Unlock()

	interfaces := d.interfaces[routerID]
	for i, iface := range interfaces {
		if iface.SubnetID == subnetID {
			hostVeth := fmt.Sprintf("qr-%s", iface.PortID[:8])

			// Remove from OVS
			exec.Command("ovs-vsctl", "del-port", d.config.OVSBridge, hostVeth).Run()

			// Delete veth pair (deleting one end deletes both)
			exec.Command("ip", "link", "delete", hostVeth).Run()

			// Remove from list
			d.interfaces[routerID] = append(interfaces[:i], interfaces[i+1:]...)

			d.logger.Info("removed router interface",
				zap.String("router_id", routerID),
				zap.String("subnet_id", subnetID),
			)

			return nil
		}
	}

	return fmt.Errorf("interface not found for subnet %s", subnetID)
}

// AddRoute adds a static route to a router.
func (d *DVR) AddRoute(ctx context.Context, routerID string, destination, nexthop string) error {
	d.nsMu.RLock()
	ns, exists := d.namespaces[routerID]
	d.nsMu.RUnlock()

	if !exists {
		return fmt.Errorf("router namespace not found: %s", routerID)
	}

	cmd := exec.Command("ip", "netns", "exec", ns.Name, "ip", "route", "add", destination, "via", nexthop)
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("failed to add route: %w", err)
	}

	d.logger.Info("added route",
		zap.String("router_id", routerID),
		zap.String("destination", destination),
		zap.String("nexthop", nexthop),
	)

	return nil
}

// RemoveRoute removes a static route from a router.
func (d *DVR) RemoveRoute(ctx context.Context, routerID string, destination string) error {
	d.nsMu.RLock()
	ns, exists := d.namespaces[routerID]
	d.nsMu.RUnlock()

	if !exists {
		return fmt.Errorf("router namespace not found: %s", routerID)
	}

	cmd := exec.Command("ip", "netns", "exec", ns.Name, "ip", "route", "del", destination)
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("failed to remove route: %w", err)
	}

	d.logger.Info("removed route",
		zap.String("router_id", routerID),
		zap.String("destination", destination),
	)

	return nil
}

// SetupSNAT configures SNAT for external network access.
func (d *DVR) SetupSNAT(ctx context.Context, routerID string, externalIP, internalSubnet string) error {
	d.nsMu.RLock()
	ns, exists := d.namespaces[routerID]
	d.nsMu.RUnlock()

	if !exists {
		return fmt.Errorf("router namespace not found: %s", routerID)
	}

	// Add SNAT rule
	cmd := exec.Command("ip", "netns", "exec", ns.Name,
		"iptables", "-t", "nat", "-A", "POSTROUTING",
		"-s", internalSubnet, "-j", "SNAT", "--to-source", externalIP)
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("failed to add SNAT rule: %w", err)
	}

	d.logger.Info("configured SNAT",
		zap.String("router_id", routerID),
		zap.String("external_ip", externalIP),
		zap.String("internal_subnet", internalSubnet),
	)

	return nil
}

// SetupDNAT configures DNAT for floating IP.
func (d *DVR) SetupDNAT(ctx context.Context, routerID string, floatingIP, fixedIP string) error {
	d.nsMu.RLock()
	ns, exists := d.namespaces[routerID]
	d.nsMu.RUnlock()

	if !exists {
		return fmt.Errorf("router namespace not found: %s", routerID)
	}

	// Add DNAT rule
	cmd := exec.Command("ip", "netns", "exec", ns.Name,
		"iptables", "-t", "nat", "-A", "PREROUTING",
		"-d", floatingIP, "-j", "DNAT", "--to-destination", fixedIP)
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("failed to add DNAT rule: %w", err)
	}

	// Add SNAT for return traffic
	cmd = exec.Command("ip", "netns", "exec", ns.Name,
		"iptables", "-t", "nat", "-A", "POSTROUTING",
		"-s", fixedIP, "-j", "SNAT", "--to-source", floatingIP)
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("failed to add return SNAT rule: %w", err)
	}

	d.logger.Info("configured floating IP",
		zap.String("router_id", routerID),
		zap.String("floating_ip", floatingIP),
		zap.String("fixed_ip", fixedIP),
	)

	return nil
}

// RemoveDNAT removes DNAT for floating IP.
func (d *DVR) RemoveDNAT(ctx context.Context, routerID string, floatingIP, fixedIP string) error {
	d.nsMu.RLock()
	ns, exists := d.namespaces[routerID]
	d.nsMu.RUnlock()

	if !exists {
		return fmt.Errorf("router namespace not found: %s", routerID)
	}

	// Remove DNAT rule
	exec.Command("ip", "netns", "exec", ns.Name,
		"iptables", "-t", "nat", "-D", "PREROUTING",
		"-d", floatingIP, "-j", "DNAT", "--to-destination", fixedIP).Run()

	// Remove SNAT rule
	exec.Command("ip", "netns", "exec", ns.Name,
		"iptables", "-t", "nat", "-D", "POSTROUTING",
		"-s", fixedIP, "-j", "SNAT", "--to-source", floatingIP).Run()

	d.logger.Info("removed floating IP",
		zap.String("router_id", routerID),
		zap.String("floating_ip", floatingIP),
	)

	return nil
}

// GetNamespace returns the namespace for a router.
func (d *DVR) GetNamespace(routerID string) (*RouterNamespace, bool) {
	d.nsMu.RLock()
	defer d.nsMu.RUnlock()

	ns, exists := d.namespaces[routerID]
	return ns, exists
}

// ListNamespaces returns all router namespaces on this node.
func (d *DVR) ListNamespaces() []*RouterNamespace {
	d.nsMu.RLock()
	defer d.nsMu.RUnlock()

	namespaces := make([]*RouterNamespace, 0, len(d.namespaces))
	for _, ns := range d.namespaces {
		namespaces = append(namespaces, ns)
	}
	return namespaces
}

// Stop stops the DVR service.
func (d *DVR) Stop() error {
	d.logger.Info("stopping DVR")

	d.cancel()
	d.wg.Wait()

	// Clean up namespaces
	d.nsMu.Lock()
	for routerID := range d.namespaces {
		d.deleteNamespace(routerID)
	}
	d.nsMu.Unlock()

	return nil
}
