// Package cgo provides Go bindings for network acceleration libraries.
// This is a pure-Go implementation using ovs-vsctl command line tool.
package cgo

import (
	"fmt"
	"net"
	"os/exec"
	"strconv"
	"strings"

	"hypervisor/pkg/network"
	"hypervisor/pkg/network/overlay"
)

// OVSBridge wraps OVS bridge operations using ovs-vsctl.
type OVSBridge struct {
	name string
}

// NewOVSBridge creates a new OVS bridge wrapper.
func NewOVSBridge(name string) *OVSBridge {
	return &OVSBridge{name: name}
}

// CreateBridge creates an OVS bridge.
func (b *OVSBridge) CreateBridge(name string) error {
	cmd := exec.Command("ovs-vsctl", "--may-exist", "add-br", name)
	if out, err := cmd.CombinedOutput(); err != nil {
		return fmt.Errorf("failed to create bridge: %s: %w", string(out), err)
	}
	return nil
}

// DeleteBridge deletes an OVS bridge.
func (b *OVSBridge) DeleteBridge(name string) error {
	cmd := exec.Command("ovs-vsctl", "--if-exists", "del-br", name)
	if out, err := cmd.CombinedOutput(); err != nil {
		return fmt.Errorf("failed to delete bridge: %s: %w", string(out), err)
	}
	return nil
}

// BridgeExists checks if a bridge exists.
func (b *OVSBridge) BridgeExists(name string) (bool, error) {
	cmd := exec.Command("ovs-vsctl", "br-exists", name)
	err := cmd.Run()
	if err != nil {
		if exitErr, ok := err.(*exec.ExitError); ok {
			if exitErr.ExitCode() == 2 {
				return false, nil
			}
		}
		return false, err
	}
	return true, nil
}

// AddPort adds a port to the bridge.
func (b *OVSBridge) AddPort(bridge, port string, options map[string]string) error {
	args := []string{"--may-exist", "add-port", bridge, port}

	// Add options
	for k, v := range options {
		args = append(args, "--", "set", "interface", port, fmt.Sprintf("%s=%s", k, v))
	}

	cmd := exec.Command("ovs-vsctl", args...)
	if out, err := cmd.CombinedOutput(); err != nil {
		return fmt.Errorf("failed to add port: %s: %w", string(out), err)
	}
	return nil
}

// DeletePort removes a port from the bridge.
func (b *OVSBridge) DeletePort(bridge, port string) error {
	cmd := exec.Command("ovs-vsctl", "--if-exists", "del-port", bridge, port)
	if out, err := cmd.CombinedOutput(); err != nil {
		return fmt.Errorf("failed to delete port: %s: %w", string(out), err)
	}
	return nil
}

// AddVXLANPort adds a VXLAN tunnel port.
func (b *OVSBridge) AddVXLANPort(bridge, portName string, vni uint32, remoteIP, localIP net.IP) error {
	args := []string{
		"--may-exist", "add-port", bridge, portName,
		"--", "set", "interface", portName,
		"type=vxlan",
		fmt.Sprintf("options:key=%d", vni),
		fmt.Sprintf("options:remote_ip=%s", remoteIP.String()),
	}

	if localIP != nil {
		args = append(args, fmt.Sprintf("options:local_ip=%s", localIP.String()))
	}

	cmd := exec.Command("ovs-vsctl", args...)
	if out, err := cmd.CombinedOutput(); err != nil {
		return fmt.Errorf("failed to add VXLAN port: %s: %w", string(out), err)
	}
	return nil
}

// DeleteVXLANPort removes a VXLAN tunnel port.
func (b *OVSBridge) DeleteVXLANPort(bridge, portName string) error {
	return b.DeletePort(bridge, portName)
}

// AddFlow adds an OpenFlow rule.
func (b *OVSBridge) AddFlow(bridge string, rule *network.FlowRule) error {
	flowStr := b.buildFlowString(rule)
	cmd := exec.Command("ovs-ofctl", "add-flow", bridge, flowStr)
	if out, err := cmd.CombinedOutput(); err != nil {
		return fmt.Errorf("failed to add flow: %s: %w", string(out), err)
	}
	return nil
}

// buildFlowString converts a FlowRule to ovs-ofctl flow string.
func (b *OVSBridge) buildFlowString(rule *network.FlowRule) string {
	parts := []string{
		fmt.Sprintf("table=%d", rule.TableID),
		fmt.Sprintf("priority=%d", rule.Priority),
		fmt.Sprintf("cookie=0x%x", rule.Cookie),
	}

	// Match fields
	if rule.Match.InPort > 0 {
		parts = append(parts, fmt.Sprintf("in_port=%d", rule.Match.InPort))
	}
	if rule.Match.DLSrc != "" {
		parts = append(parts, fmt.Sprintf("dl_src=%s", rule.Match.DLSrc))
	}
	if rule.Match.DLDst != "" {
		parts = append(parts, fmt.Sprintf("dl_dst=%s", rule.Match.DLDst))
	}
	if rule.Match.DLType > 0 {
		parts = append(parts, fmt.Sprintf("dl_type=0x%04x", rule.Match.DLType))
	}
	if rule.Match.NWSrc != "" {
		parts = append(parts, fmt.Sprintf("nw_src=%s", rule.Match.NWSrc))
	}
	if rule.Match.NWDst != "" {
		parts = append(parts, fmt.Sprintf("nw_dst=%s", rule.Match.NWDst))
	}
	if rule.Match.NWProto > 0 {
		parts = append(parts, fmt.Sprintf("nw_proto=%d", rule.Match.NWProto))
	}
	if rule.Match.TunnelID > 0 {
		parts = append(parts, fmt.Sprintf("tun_id=%d", rule.Match.TunnelID))
	}

	// Actions
	var actions []string
	for _, action := range rule.Actions {
		switch action.Type {
		case network.FlowActionOutput:
			switch v := action.Value.(type) {
			case string:
				actions = append(actions, fmt.Sprintf("output:%s", v))
			case uint32:
				actions = append(actions, fmt.Sprintf("output:%d", v))
			}
		case network.FlowActionGotoTable:
			if tableID, ok := action.Value.(uint8); ok {
				actions = append(actions, fmt.Sprintf("goto_table:%d", tableID))
			}
		case network.FlowActionSetTunnel:
			if tunID, ok := action.Value.(uint32); ok {
				actions = append(actions, fmt.Sprintf("set_tunnel:%d", tunID))
			}
		case network.FlowActionDrop:
			actions = append(actions, "drop")
		case network.FlowActionController:
			actions = append(actions, "controller")
		}
	}

	if len(actions) > 0 {
		parts = append(parts, "actions="+strings.Join(actions, ","))
	} else {
		parts = append(parts, "actions=drop")
	}

	return strings.Join(parts, ",")
}

// DeleteFlow removes an OpenFlow rule by cookie.
func (b *OVSBridge) DeleteFlow(bridge string, cookie uint64) error {
	flowStr := fmt.Sprintf("cookie=0x%x/-1", cookie)
	cmd := exec.Command("ovs-ofctl", "del-flows", bridge, flowStr)
	if out, err := cmd.CombinedOutput(); err != nil {
		return fmt.Errorf("failed to delete flow: %s: %w", string(out), err)
	}
	return nil
}

// DeleteFlowsByMatch removes OpenFlow rules by match criteria.
func (b *OVSBridge) DeleteFlowsByMatch(bridge string, match *network.FlowMatch) error {
	// Build match string
	var parts []string
	if match.TunnelID > 0 {
		parts = append(parts, fmt.Sprintf("tun_id=%d", match.TunnelID))
	}
	if match.DLSrc != "" {
		parts = append(parts, fmt.Sprintf("dl_src=%s", match.DLSrc))
	}

	flowStr := strings.Join(parts, ",")
	cmd := exec.Command("ovs-ofctl", "del-flows", bridge, flowStr)
	if out, err := cmd.CombinedOutput(); err != nil {
		return fmt.Errorf("failed to delete flows: %s: %w", string(out), err)
	}
	return nil
}

// DumpFlows returns all flows on a bridge.
func (b *OVSBridge) DumpFlows(bridge string) ([]*network.FlowRule, error) {
	cmd := exec.Command("ovs-ofctl", "dump-flows", bridge)
	out, err := cmd.Output()
	if err != nil {
		return nil, fmt.Errorf("failed to dump flows: %w", err)
	}

	// Parse output (simplified)
	var flows []*network.FlowRule
	lines := strings.Split(string(out), "\n")
	for _, line := range lines {
		if strings.HasPrefix(line, " cookie=") {
			// Parse flow (simplified)
			flow := &network.FlowRule{}
			// Extract cookie
			if idx := strings.Index(line, "cookie="); idx >= 0 {
				end := strings.Index(line[idx:], ",")
				if end > 0 {
					cookieStr := line[idx+7 : idx+end]
					if val, err := strconv.ParseUint(strings.TrimPrefix(cookieStr, "0x"), 16, 64); err == nil {
						flow.Cookie = val
					}
				}
			}
			flows = append(flows, flow)
		}
	}

	return flows, nil
}

// GetPortStats retrieves port statistics.
func (b *OVSBridge) GetPortStats(bridge, port string) (*overlay.PortStats, error) {
	cmd := exec.Command("ovs-vsctl", "get", "interface", port, "statistics")
	out, err := cmd.Output()
	if err != nil {
		return nil, fmt.Errorf("failed to get port stats: %w", err)
	}

	stats := &overlay.PortStats{}
	// Parse statistics output
	statsStr := strings.Trim(string(out), "{}\n")
	pairs := strings.Split(statsStr, ", ")
	for _, pair := range pairs {
		kv := strings.Split(pair, "=")
		if len(kv) != 2 {
			continue
		}
		val, _ := strconv.ParseUint(kv[1], 10, 64)
		switch kv[0] {
		case "rx_packets":
			stats.RxPackets = val
		case "tx_packets":
			stats.TxPackets = val
		case "rx_bytes":
			stats.RxBytes = val
		case "tx_bytes":
			stats.TxBytes = val
		case "rx_errors":
			stats.RxErrors = val
		case "tx_errors":
			stats.TxErrors = val
		case "rx_dropped":
			stats.RxDropped = val
		case "tx_dropped":
			stats.TxDropped = val
		}
	}

	return stats, nil
}
