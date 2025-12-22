package router

import (
	"context"
	"fmt"
	"net"
	"sync"

	"go.uber.org/zap"

	"hypervisor/pkg/network"
)

// ARPProxy implements distributed ARP proxy for overlay networks.
type ARPProxy struct {
	logger    *zap.Logger
	ovsClient ARPOVSClient

	// MAC address table: IP -> MAC
	macTable   map[string]string
	macTableMu sync.RWMutex

	// Subnet to VNI mapping
	subnetVNI map[string]uint32
	subnetMu  sync.RWMutex
}

// ARPOVSClient defines OVS operations for ARP proxy.
type ARPOVSClient interface {
	AddFlow(bridge string, rule *network.FlowRule) error
	DeleteFlow(bridge string, cookie uint64) error
}

// NewARPProxy creates a new ARP proxy.
func NewARPProxy(logger *zap.Logger) *ARPProxy {
	return &ARPProxy{
		logger:    logger,
		macTable:  make(map[string]string),
		subnetVNI: make(map[string]uint32),
	}
}

// SetOVSClient sets the OVS client.
func (a *ARPProxy) SetOVSClient(client ARPOVSClient) {
	a.ovsClient = client
}

// RegisterMAC registers an IP-MAC mapping.
func (a *ARPProxy) RegisterMAC(ip, mac string, vni uint32) error {
	a.macTableMu.Lock()
	a.macTable[ip] = mac
	a.macTableMu.Unlock()

	a.logger.Debug("registered MAC",
		zap.String("ip", ip),
		zap.String("mac", mac),
		zap.Uint32("vni", vni),
	)

	// Install ARP responder flow
	if a.ovsClient != nil {
		if err := a.installARPResponderFlow(ip, mac, vni); err != nil {
			return fmt.Errorf("failed to install ARP responder: %w", err)
		}
	}

	return nil
}

// UnregisterMAC removes an IP-MAC mapping.
func (a *ARPProxy) UnregisterMAC(ip string, vni uint32) error {
	a.macTableMu.Lock()
	delete(a.macTable, ip)
	a.macTableMu.Unlock()

	a.logger.Debug("unregistered MAC", zap.String("ip", ip))

	// Remove ARP responder flow
	if a.ovsClient != nil {
		cookie := a.generateCookie(ip, vni)
		if err := a.ovsClient.DeleteFlow("br-tun", cookie); err != nil {
			a.logger.Warn("failed to remove ARP responder", zap.Error(err))
		}
	}

	return nil
}

// GetMAC retrieves MAC address for an IP.
func (a *ARPProxy) GetMAC(ip string) (string, bool) {
	a.macTableMu.RLock()
	defer a.macTableMu.RUnlock()

	mac, exists := a.macTable[ip]
	return mac, exists
}

// RegisterSubnet registers a subnet for ARP proxy.
func (a *ARPProxy) RegisterSubnet(subnetID string, cidr string, vni uint32) error {
	a.subnetMu.Lock()
	a.subnetVNI[subnetID] = vni
	a.subnetMu.Unlock()

	// Install ARP suppression flow for this subnet
	if a.ovsClient != nil {
		if err := a.installARPSuppressionFlow(cidr, vni); err != nil {
			return fmt.Errorf("failed to install ARP suppression: %w", err)
		}
	}

	a.logger.Info("registered subnet for ARP proxy",
		zap.String("subnet_id", subnetID),
		zap.String("cidr", cidr),
		zap.Uint32("vni", vni),
	)

	return nil
}

// installARPResponderFlow installs an OpenFlow rule to respond to ARP requests.
func (a *ARPProxy) installARPResponderFlow(ip, mac string, vni uint32) error {
	// Parse IP to uint32 for matching
	ipAddr := net.ParseIP(ip)
	if ipAddr == nil {
		return fmt.Errorf("invalid IP: %s", ip)
	}

	cookie := a.generateCookie(ip, vni)

	// Flow to respond to ARP requests for this IP
	// Match: ARP request (opcode=1), arp_tpa=ip, tun_id=vni
	// Action: construct ARP reply, output to in_port
	flow := &network.FlowRule{
		TableID:  22, // ARP responder table
		Priority: 100,
		Cookie:   cookie,
		Match: network.FlowMatch{
			DLType:   0x0806, // ARP
			NWDst:    ip,
			TunnelID: vni,
		},
		Actions: []network.FlowAction{
			// Move eth_src to eth_dst
			{Type: network.FlowActionSetField, Value: map[string]string{"dl_dst": "dl_src"}},
			// Set eth_src to known MAC
			{Type: network.FlowActionSetField, Value: map[string]string{"dl_src": mac}},
			// Set ARP opcode to reply (2)
			{Type: network.FlowActionSetField, Value: map[string]string{"arp_op": "2"}},
			// Swap ARP sender/target
			{Type: network.FlowActionSetField, Value: map[string]string{"arp_tha": "arp_sha"}},
			{Type: network.FlowActionSetField, Value: map[string]string{"arp_sha": mac}},
			{Type: network.FlowActionSetField, Value: map[string]string{"arp_tpa": "arp_spa"}},
			{Type: network.FlowActionSetField, Value: map[string]string{"arp_spa": ip}},
			// Output to in_port
			{Type: network.FlowActionOutput, Value: "in_port"},
		},
	}

	return a.ovsClient.AddFlow("br-tun", flow)
}

// installARPSuppressionFlow installs a flow to suppress ARP flooding in the overlay.
func (a *ARPProxy) installARPSuppressionFlow(cidr string, vni uint32) error {
	// Flow to redirect ARP to responder table instead of flooding
	flow := &network.FlowRule{
		TableID:  21, // Before flood table
		Priority: 100,
		Cookie:   a.generateSubnetCookie(cidr, vni),
		Match: network.FlowMatch{
			DLType:   0x0806, // ARP
			TunnelID: vni,
		},
		Actions: []network.FlowAction{
			{Type: network.FlowActionGotoTable, Value: uint8(22)}, // Go to ARP responder table
		},
	}

	return a.ovsClient.AddFlow("br-tun", flow)
}

// generateCookie creates a unique cookie for an IP/VNI pair.
func (a *ARPProxy) generateCookie(ip string, vni uint32) uint64 {
	var h uint32
	for _, c := range ip {
		h = 31*h + uint32(c)
	}
	return uint64(vni)<<32 | uint64(h)
}

// generateSubnetCookie creates a unique cookie for a subnet/VNI pair.
func (a *ARPProxy) generateSubnetCookie(cidr string, vni uint32) uint64 {
	var h uint32
	for _, c := range cidr {
		h = 31*h + uint32(c)
	}
	return uint64(vni)<<32 | uint64(h) | 0x80000000
}

// SyncMACTable synchronizes the MAC table from a source (e.g., controller).
func (a *ARPProxy) SyncMACTable(ctx context.Context, entries map[string]MACEntry) error {
	a.macTableMu.Lock()
	defer a.macTableMu.Unlock()

	// Find entries to remove
	for ip := range a.macTable {
		if _, exists := entries[ip]; !exists {
			delete(a.macTable, ip)
			// Remove flow
			if a.ovsClient != nil {
				// Note: would need VNI to properly delete
				a.logger.Debug("removed stale MAC entry", zap.String("ip", ip))
			}
		}
	}

	// Add/update entries
	for ip, entry := range entries {
		a.macTable[ip] = entry.MAC
		if a.ovsClient != nil {
			if err := a.installARPResponderFlow(ip, entry.MAC, entry.VNI); err != nil {
				a.logger.Warn("failed to install ARP flow",
					zap.String("ip", ip),
					zap.Error(err),
				)
			}
		}
	}

	a.logger.Info("synchronized MAC table", zap.Int("entries", len(entries)))
	return nil
}

// MACEntry represents a MAC table entry.
type MACEntry struct {
	MAC string
	VNI uint32
}

// GetMACTable returns a copy of the current MAC table.
func (a *ARPProxy) GetMACTable() map[string]string {
	a.macTableMu.RLock()
	defer a.macTableMu.RUnlock()

	table := make(map[string]string, len(a.macTable))
	for ip, mac := range a.macTable {
		table[ip] = mac
	}
	return table
}
