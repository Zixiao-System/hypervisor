// Package overlay provides VXLAN overlay networking implementation.
package overlay

import (
	"context"
	"fmt"
	"net"
	"sync"

	"go.uber.org/zap"

	"hypervisor/pkg/network"
)

// VXLANManager manages VXLAN tunnels and network overlays.
type VXLANManager struct {
	config *network.NetworkConfig
	logger *zap.Logger

	// Local VTEP information
	localVTEP *network.VTEP

	// Active tunnels indexed by remote node ID
	tunnels   map[string]*network.Tunnel
	tunnelsMu sync.RWMutex

	// VNI to network mapping
	vniMap   map[uint32]*network.Network
	vniMapMu sync.RWMutex

	// OVS bridge interface
	ovsClient OVSClient
}

// OVSClient defines the interface for OVS operations.
type OVSClient interface {
	// Bridge operations
	CreateBridge(name string) error
	DeleteBridge(name string) error
	BridgeExists(name string) (bool, error)

	// Port operations
	AddPort(bridge, port string, options map[string]string) error
	DeletePort(bridge, port string) error

	// VXLAN port operations
	AddVXLANPort(bridge, portName string, vni uint32, remoteIP net.IP, localIP net.IP) error
	DeleteVXLANPort(bridge, portName string) error

	// Flow operations
	AddFlow(bridge string, rule *network.FlowRule) error
	DeleteFlow(bridge string, cookie uint64) error

	// Port stats
	GetPortStats(bridge, port string) (*PortStats, error)
}

// PortStats represents port statistics.
type PortStats struct {
	RxPackets uint64
	TxPackets uint64
	RxBytes   uint64
	TxBytes   uint64
	RxErrors  uint64
	TxErrors  uint64
	RxDropped uint64
	TxDropped uint64
}

// NewVXLANManager creates a new VXLAN manager.
func NewVXLANManager(config *network.NetworkConfig, logger *zap.Logger, ovsClient OVSClient) (*VXLANManager, error) {
	if config == nil {
		config = network.DefaultNetworkConfig()
	}

	localIP := net.ParseIP(config.VXLANLocalIP)
	if localIP == nil && config.VXLANLocalIP != "" {
		return nil, fmt.Errorf("invalid VXLAN local IP: %s", config.VXLANLocalIP)
	}

	mgr := &VXLANManager{
		config:    config,
		logger:    logger,
		tunnels:   make(map[string]*network.Tunnel),
		vniMap:    make(map[uint32]*network.Network),
		ovsClient: ovsClient,
	}

	return mgr, nil
}

// Initialize sets up the VXLAN infrastructure.
func (m *VXLANManager) Initialize(ctx context.Context, nodeID string, localIP net.IP) error {
	m.logger.Info("initializing VXLAN manager",
		zap.String("node_id", nodeID),
		zap.String("local_ip", localIP.String()),
	)

	// Create integration bridge if not exists
	exists, err := m.ovsClient.BridgeExists(m.config.OVSBridge)
	if err != nil {
		return fmt.Errorf("failed to check integration bridge: %w", err)
	}
	if !exists {
		if err := m.ovsClient.CreateBridge(m.config.OVSBridge); err != nil {
			return fmt.Errorf("failed to create integration bridge: %w", err)
		}
	}

	// Create tunnel bridge if not exists
	exists, err = m.ovsClient.BridgeExists(m.config.OVSTunnelBridge)
	if err != nil {
		return fmt.Errorf("failed to check tunnel bridge: %w", err)
	}
	if !exists {
		if err := m.ovsClient.CreateBridge(m.config.OVSTunnelBridge); err != nil {
			return fmt.Errorf("failed to create tunnel bridge: %w", err)
		}
	}

	// Set local VTEP
	m.localVTEP = &network.VTEP{
		NodeID:    nodeID,
		IP:        localIP,
		Port:      m.config.VXLANPort,
		Interface: m.config.OVSTunnelBridge,
		Status:    "active",
	}

	// Add patch ports between br-int and br-tun
	if err := m.setupPatchPorts(); err != nil {
		return fmt.Errorf("failed to setup patch ports: %w", err)
	}

	// Install base flow rules
	if err := m.installBaseFlows(); err != nil {
		return fmt.Errorf("failed to install base flows: %w", err)
	}

	m.logger.Info("VXLAN manager initialized successfully")
	return nil
}

// setupPatchPorts creates patch ports between integration and tunnel bridges.
func (m *VXLANManager) setupPatchPorts() error {
	// Add patch-tun port on br-int
	if err := m.ovsClient.AddPort(m.config.OVSBridge, "patch-tun", map[string]string{
		"type":         "patch",
		"options:peer": "patch-int",
	}); err != nil {
		// Ignore error if port already exists
		m.logger.Debug("patch-tun port may already exist", zap.Error(err))
	}

	// Add patch-int port on br-tun
	if err := m.ovsClient.AddPort(m.config.OVSTunnelBridge, "patch-int", map[string]string{
		"type":         "patch",
		"options:peer": "patch-tun",
	}); err != nil {
		m.logger.Debug("patch-int port may already exist", zap.Error(err))
	}

	return nil
}

// installBaseFlows installs base OpenFlow rules on both bridges.
func (m *VXLANManager) installBaseFlows() error {
	// Table 0 on br-int: Normal learning switch behavior as fallback
	baseRule := &network.FlowRule{
		TableID:  0,
		Priority: 1,
		Cookie:   0x1000,
		Actions: []network.FlowAction{
			{Type: network.FlowActionOutput, Value: "normal"},
		},
	}
	if err := m.ovsClient.AddFlow(m.config.OVSBridge, baseRule); err != nil {
		return fmt.Errorf("failed to add base flow on br-int: %w", err)
	}

	// Table 0 on br-tun: Classify incoming traffic (from patch port vs tunnel)
	// Unicast from patch-int goes to table 20 (unicast lookup)
	// Broadcast/multicast from patch-int goes to table 21 (flood)
	// Traffic from tunnels goes to table 10 (tunnel processing)

	return nil
}

// CreateTunnel creates a VXLAN tunnel to a remote node.
func (m *VXLANManager) CreateTunnel(ctx context.Context, remoteNodeID string, remoteIP net.IP, vni uint32) (*network.Tunnel, error) {
	m.tunnelsMu.Lock()
	defer m.tunnelsMu.Unlock()

	tunnelKey := fmt.Sprintf("%s-%d", remoteNodeID, vni)

	// Check if tunnel already exists
	if tunnel, exists := m.tunnels[tunnelKey]; exists {
		m.logger.Debug("tunnel already exists",
			zap.String("remote_node", remoteNodeID),
			zap.Uint32("vni", vni),
		)
		return tunnel, nil
	}

	// Create VXLAN port name
	portName := fmt.Sprintf("vxlan-%s", remoteNodeID[:8])

	m.logger.Info("creating VXLAN tunnel",
		zap.String("remote_node", remoteNodeID),
		zap.String("remote_ip", remoteIP.String()),
		zap.Uint32("vni", vni),
		zap.String("port_name", portName),
	)

	// Add VXLAN port to tunnel bridge
	if err := m.ovsClient.AddVXLANPort(
		m.config.OVSTunnelBridge,
		portName,
		vni,
		remoteIP,
		m.localVTEP.IP,
	); err != nil {
		return nil, fmt.Errorf("failed to create VXLAN port: %w", err)
	}

	tunnel := &network.Tunnel{
		ID:         fmt.Sprintf("%s-%s-%d", m.localVTEP.NodeID, remoteNodeID, vni),
		VNI:        vni,
		LocalVTEP:  m.localVTEP.NodeID,
		RemoteVTEP: remoteNodeID,
		RemoteIP:   remoteIP,
		Status:     "active",
	}

	m.tunnels[tunnelKey] = tunnel

	// Install flow rules for this tunnel
	if err := m.installTunnelFlows(tunnel, portName); err != nil {
		m.logger.Error("failed to install tunnel flows", zap.Error(err))
		// Continue anyway, tunnel is created
	}

	return tunnel, nil
}

// installTunnelFlows installs OpenFlow rules for a specific tunnel.
func (m *VXLANManager) installTunnelFlows(tunnel *network.Tunnel, portName string) error {
	// Flow to handle incoming traffic from this tunnel
	// Match on in_port and tun_id, output to patch-int
	incomingFlow := &network.FlowRule{
		TableID:  10, // Tunnel processing table
		Priority: 100,
		Cookie:   uint64(tunnel.VNI) << 16,
		Match: network.FlowMatch{
			TunnelID: tunnel.VNI,
		},
		Actions: []network.FlowAction{
			{Type: network.FlowActionPopVXLAN},
			{Type: network.FlowActionOutput, Value: "patch-int"},
		},
	}
	if err := m.ovsClient.AddFlow(m.config.OVSTunnelBridge, incomingFlow); err != nil {
		return fmt.Errorf("failed to add incoming flow: %w", err)
	}

	return nil
}

// DeleteTunnel removes a VXLAN tunnel.
func (m *VXLANManager) DeleteTunnel(ctx context.Context, remoteNodeID string, vni uint32) error {
	m.tunnelsMu.Lock()
	defer m.tunnelsMu.Unlock()

	tunnelKey := fmt.Sprintf("%s-%d", remoteNodeID, vni)
	tunnel, exists := m.tunnels[tunnelKey]
	if !exists {
		return fmt.Errorf("tunnel not found: %s", tunnelKey)
	}

	portName := fmt.Sprintf("vxlan-%s", remoteNodeID[:8])

	m.logger.Info("deleting VXLAN tunnel",
		zap.String("remote_node", remoteNodeID),
		zap.Uint32("vni", vni),
	)

	// Delete flow rules
	if err := m.ovsClient.DeleteFlow(m.config.OVSTunnelBridge, uint64(tunnel.VNI)<<16); err != nil {
		m.logger.Warn("failed to delete tunnel flows", zap.Error(err))
	}

	// Delete VXLAN port
	if err := m.ovsClient.DeleteVXLANPort(m.config.OVSTunnelBridge, portName); err != nil {
		return fmt.Errorf("failed to delete VXLAN port: %w", err)
	}

	delete(m.tunnels, tunnelKey)
	return nil
}

// GetTunnel returns a tunnel by remote node ID and VNI.
func (m *VXLANManager) GetTunnel(remoteNodeID string, vni uint32) (*network.Tunnel, bool) {
	m.tunnelsMu.RLock()
	defer m.tunnelsMu.RUnlock()

	tunnelKey := fmt.Sprintf("%s-%d", remoteNodeID, vni)
	tunnel, exists := m.tunnels[tunnelKey]
	return tunnel, exists
}

// ListTunnels returns all active tunnels.
func (m *VXLANManager) ListTunnels() []*network.Tunnel {
	m.tunnelsMu.RLock()
	defer m.tunnelsMu.RUnlock()

	tunnels := make([]*network.Tunnel, 0, len(m.tunnels))
	for _, t := range m.tunnels {
		tunnels = append(tunnels, t)
	}
	return tunnels
}

// RegisterNetwork registers a network with its VNI mapping.
func (m *VXLANManager) RegisterNetwork(net *network.Network) error {
	if net.Type != network.NetworkTypeVXLAN {
		return fmt.Errorf("network type must be VXLAN, got %s", net.Type)
	}
	if net.VNI == 0 || net.VNI > 16777215 {
		return fmt.Errorf("invalid VNI: %d (must be 1-16777215)", net.VNI)
	}

	m.vniMapMu.Lock()
	defer m.vniMapMu.Unlock()

	if existing, exists := m.vniMap[net.VNI]; exists {
		if existing.ID != net.ID {
			return fmt.Errorf("VNI %d already in use by network %s", net.VNI, existing.ID)
		}
	}

	m.vniMap[net.VNI] = net
	m.logger.Info("registered network",
		zap.String("network_id", net.ID),
		zap.Uint32("vni", net.VNI),
	)

	return nil
}

// UnregisterNetwork removes a network from VNI mapping.
func (m *VXLANManager) UnregisterNetwork(networkID string) {
	m.vniMapMu.Lock()
	defer m.vniMapMu.Unlock()

	for vni, net := range m.vniMap {
		if net.ID == networkID {
			delete(m.vniMap, vni)
			m.logger.Info("unregistered network",
				zap.String("network_id", networkID),
				zap.Uint32("vni", vni),
			)
			return
		}
	}
}

// GetNetworkByVNI returns a network by its VNI.
func (m *VXLANManager) GetNetworkByVNI(vni uint32) (*network.Network, bool) {
	m.vniMapMu.RLock()
	defer m.vniMapMu.RUnlock()

	net, exists := m.vniMap[vni]
	return net, exists
}

// GetLocalVTEP returns the local VTEP information.
func (m *VXLANManager) GetLocalVTEP() *network.VTEP {
	return m.localVTEP
}

// Close cleans up VXLAN manager resources.
func (m *VXLANManager) Close() error {
	m.logger.Info("shutting down VXLAN manager")

	// Delete all tunnels
	m.tunnelsMu.Lock()
	for key, tunnel := range m.tunnels {
		portName := fmt.Sprintf("vxlan-%s", tunnel.RemoteVTEP[:8])
		if err := m.ovsClient.DeleteVXLANPort(m.config.OVSTunnelBridge, portName); err != nil {
			m.logger.Warn("failed to delete tunnel on shutdown",
				zap.String("tunnel", key),
				zap.Error(err),
			)
		}
	}
	m.tunnels = make(map[string]*network.Tunnel)
	m.tunnelsMu.Unlock()

	return nil
}
