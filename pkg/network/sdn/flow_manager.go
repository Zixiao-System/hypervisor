package sdn

import (
	"fmt"
	"sync"

	"go.uber.org/zap"

	"hypervisor/pkg/network"
)

// FlowManager manages OpenFlow rules on OVS bridges.
type FlowManager struct {
	config *network.NetworkConfig
	logger *zap.Logger

	// Active flows indexed by port ID
	portFlows map[string][]*network.FlowRule
	flowsMu   sync.RWMutex

	// OVS client for flow operations
	ovsClient OVSFlowClient
}

// OVSFlowClient defines the interface for OVS flow operations.
type OVSFlowClient interface {
	AddFlow(bridge string, rule *network.FlowRule) error
	DeleteFlow(bridge string, cookie uint64) error
	DeleteFlowsByMatch(bridge string, match *network.FlowMatch) error
	DumpFlows(bridge string) ([]*network.FlowRule, error)
}

// NewFlowManager creates a new flow manager.
func NewFlowManager(config *network.NetworkConfig, logger *zap.Logger) (*FlowManager, error) {
	return &FlowManager{
		config:    config,
		logger:    logger,
		portFlows: make(map[string][]*network.FlowRule),
		// ovsClient will be injected or use exec-based implementation
	}, nil
}

// SetOVSClient sets the OVS client.
func (f *FlowManager) SetOVSClient(client OVSFlowClient) {
	f.ovsClient = client
}

// InstallPortFlows installs OpenFlow rules for a port.
func (f *FlowManager) InstallPortFlows(port *network.Port, net *network.Network) error {
	if f.ovsClient == nil {
		f.logger.Debug("OVS client not set, skipping flow installation")
		return nil
	}

	var flows []*network.FlowRule
	cookie := generateCookie(port.ID)

	// Flow 1: L2 learning - MAC to port binding
	// Table 20: Unicast lookup
	// Match: dl_dst=port.MACAddress -> output:port
	l2Flow := &network.FlowRule{
		TableID:  20,
		Priority: 100,
		Cookie:   cookie,
		Match: network.FlowMatch{
			DLDst:    port.MACAddress,
			TunnelID: net.VNI,
		},
		Actions: []network.FlowAction{
			{Type: network.FlowActionOutput, Value: port.DeviceName},
		},
	}
	flows = append(flows, l2Flow)

	// Flow 2: Security group ingress rules
	for _, sgID := range port.SecurityGroups {
		sgFlows := f.generateSecurityGroupFlows(port, sgID, "ingress", cookie)
		flows = append(flows, sgFlows...)
	}

	// Flow 3: Security group egress rules
	for _, sgID := range port.SecurityGroups {
		sgFlows := f.generateSecurityGroupFlows(port, sgID, "egress", cookie)
		flows = append(flows, sgFlows...)
	}

	// Flow 4: Anti-spoofing (source MAC/IP validation)
	antiSpoofFlow := &network.FlowRule{
		TableID:  0,
		Priority: 50,
		Cookie:   cookie,
		Match: network.FlowMatch{
			DLSrc: port.MACAddress,
			NWSrc: port.IPAddress,
		},
		Actions: []network.FlowAction{
			{Type: network.FlowActionGotoTable, Value: uint8(10)}, // Continue to next table
		},
	}
	flows = append(flows, antiSpoofFlow)

	// Install all flows
	for _, flow := range flows {
		if err := f.ovsClient.AddFlow(f.config.OVSBridge, flow); err != nil {
			f.logger.Error("failed to add flow",
				zap.String("port_id", port.ID),
				zap.Uint64("cookie", flow.Cookie),
				zap.Error(err),
			)
			return err
		}
	}

	// Store flows for later cleanup
	f.flowsMu.Lock()
	f.portFlows[port.ID] = flows
	f.flowsMu.Unlock()

	f.logger.Debug("installed port flows",
		zap.String("port_id", port.ID),
		zap.Int("flow_count", len(flows)),
	)

	return nil
}

// generateSecurityGroupFlows creates flows for a security group.
func (f *FlowManager) generateSecurityGroupFlows(port *network.Port, sgID, direction string, baseCookie uint64) []*network.FlowRule {
	// TODO: Look up security group rules and generate appropriate flows
	// For now, return empty slice
	return nil
}

// RemovePortFlows removes all OpenFlow rules for a port.
func (f *FlowManager) RemovePortFlows(port *network.Port) error {
	if f.ovsClient == nil {
		return nil
	}

	f.flowsMu.Lock()
	flows, exists := f.portFlows[port.ID]
	delete(f.portFlows, port.ID)
	f.flowsMu.Unlock()

	if !exists {
		return nil
	}

	// Delete all flows by cookie
	for _, flow := range flows {
		if err := f.ovsClient.DeleteFlow(f.config.OVSBridge, flow.Cookie); err != nil {
			f.logger.Warn("failed to delete flow",
				zap.String("port_id", port.ID),
				zap.Uint64("cookie", flow.Cookie),
				zap.Error(err),
			)
		}
	}

	f.logger.Debug("removed port flows",
		zap.String("port_id", port.ID),
		zap.Int("flow_count", len(flows)),
	)

	return nil
}

// InstallNetworkFlows installs base flows for a network.
func (f *FlowManager) InstallNetworkFlows(net *network.Network) error {
	if f.ovsClient == nil {
		return nil
	}

	cookie := generateCookie(net.ID)

	// Flow 1: Broadcast/multicast handling for this VNI
	// Table 21: Flood
	floodFlow := &network.FlowRule{
		TableID:  21,
		Priority: 100,
		Cookie:   cookie,
		Match: network.FlowMatch{
			TunnelID: net.VNI,
		},
		Actions: []network.FlowAction{
			{Type: network.FlowActionOutput, Value: "all"}, // Flood to all ports in VNI
		},
	}

	if err := f.ovsClient.AddFlow(f.config.OVSBridge, floodFlow); err != nil {
		return fmt.Errorf("failed to add flood flow: %w", err)
	}

	// Flow 2: Unknown unicast handling
	unknownFlow := &network.FlowRule{
		TableID:  20,
		Priority: 1, // Low priority, fallback
		Cookie:   cookie,
		Match: network.FlowMatch{
			TunnelID: net.VNI,
		},
		Actions: []network.FlowAction{
			{Type: network.FlowActionGotoTable, Value: uint8(21)}, // Go to flood table
		},
	}

	if err := f.ovsClient.AddFlow(f.config.OVSBridge, unknownFlow); err != nil {
		return fmt.Errorf("failed to add unknown unicast flow: %w", err)
	}

	f.logger.Debug("installed network flows",
		zap.String("network_id", net.ID),
		zap.Uint32("vni", net.VNI),
	)

	return nil
}

// RemoveNetworkFlows removes all flows for a network.
func (f *FlowManager) RemoveNetworkFlows(net *network.Network) error {
	if f.ovsClient == nil {
		return nil
	}

	cookie := generateCookie(net.ID)
	if err := f.ovsClient.DeleteFlow(f.config.OVSBridge, cookie); err != nil {
		return fmt.Errorf("failed to delete network flows: %w", err)
	}

	f.logger.Debug("removed network flows",
		zap.String("network_id", net.ID),
	)

	return nil
}

// InstallSecurityGroupFlows installs flows for security group rules.
func (f *FlowManager) InstallSecurityGroupFlows(sg *network.SecurityGroup) error {
	if f.ovsClient == nil {
		return nil
	}

	cookie := generateCookie(sg.ID)

	for _, rule := range sg.Rules {
		flow := f.ruleToFlow(&rule, cookie)
		if flow == nil {
			continue
		}

		if err := f.ovsClient.AddFlow(f.config.OVSBridge, flow); err != nil {
			f.logger.Error("failed to add security group flow",
				zap.String("sg_id", sg.ID),
				zap.String("rule_id", rule.ID),
				zap.Error(err),
			)
		}
	}

	return nil
}

// ruleToFlow converts a security group rule to an OpenFlow rule.
func (f *FlowManager) ruleToFlow(rule *network.SecurityGroupRule, baseCookie uint64) *network.FlowRule {
	flow := &network.FlowRule{
		Priority: 100,
		Cookie:   baseCookie + uint64(hashString(rule.ID)),
	}

	// Set match criteria based on rule
	if rule.Direction == "ingress" {
		flow.TableID = 30 // Ingress security table
	} else {
		flow.TableID = 31 // Egress security table
	}

	// EtherType
	if rule.EtherType == "IPv4" {
		flow.Match.DLType = 0x0800
	} else if rule.EtherType == "IPv6" {
		flow.Match.DLType = 0x86DD
	}

	// Protocol
	switch rule.Protocol {
	case "tcp":
		flow.Match.NWProto = 6
	case "udp":
		flow.Match.NWProto = 17
	case "icmp":
		flow.Match.NWProto = 1
	}

	// Port range
	if rule.PortRangeMin > 0 {
		flow.Match.TPDst = rule.PortRangeMin
		// For range, we'd need multiple flows
	}

	// Remote IP prefix
	if rule.RemoteIPPrefix != "" {
		if rule.Direction == "ingress" {
			flow.Match.NWSrc = rule.RemoteIPPrefix
		} else {
			flow.Match.NWDst = rule.RemoteIPPrefix
		}
	}

	// Action: allow (continue to next table)
	flow.Actions = []network.FlowAction{
		{Type: network.FlowActionGotoTable, Value: uint8(flow.TableID + 10)},
	}

	return flow
}

// UpdateSecurityGroupFlows updates flows when security group rules change.
func (f *FlowManager) UpdateSecurityGroupFlows(sg *network.SecurityGroup) error {
	// Remove old flows
	if f.ovsClient != nil {
		cookie := generateCookie(sg.ID)
		f.ovsClient.DeleteFlow(f.config.OVSBridge, cookie)
	}

	// Install new flows
	return f.InstallSecurityGroupFlows(sg)
}

// Close cleans up the flow manager.
func (f *FlowManager) Close() error {
	// Clean up all flows if needed
	return nil
}

// generateCookie creates a unique cookie from an ID.
func generateCookie(id string) uint64 {
	return uint64(hashString(id)) << 32
}

// hashString creates a simple hash of a string.
func hashString(s string) uint32 {
	var h uint32
	for _, c := range s {
		h = 31*h + uint32(c)
	}
	return h
}
