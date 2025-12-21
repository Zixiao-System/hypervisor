// Package network provides SDN/VXLAN overlay networking for the hypervisor.
package network

import (
	"net"
	"time"
)

// NetworkType represents the type of virtual network.
type NetworkType string

const (
	NetworkTypeVXLAN  NetworkType = "vxlan"
	NetworkTypeVLAN   NetworkType = "vlan"
	NetworkTypeBridge NetworkType = "bridge"
	NetworkTypeFlat   NetworkType = "flat"
)

// Network represents a virtual network with overlay capabilities.
type Network struct {
	ID          string            `json:"id"`
	Name        string            `json:"name"`
	Type        NetworkType       `json:"type"`
	VNI         uint32            `json:"vni,omitempty"`         // VXLAN Network Identifier (1-16777215)
	VLANID      uint16            `json:"vlan_id,omitempty"`     // VLAN ID (1-4094)
	MTU         uint16            `json:"mtu"`                   // Network MTU (default 1450 for VXLAN)
	AdminState  bool              `json:"admin_state"`           // Administrative state
	Shared      bool              `json:"shared"`                // Shared across tenants
	External    bool              `json:"external"`              // Connected to external network
	TenantID    string            `json:"tenant_id,omitempty"`   // Owner tenant
	Labels      map[string]string `json:"labels,omitempty"`      // Custom labels
	Annotations map[string]string `json:"annotations,omitempty"` // Custom annotations
	CreatedAt   time.Time         `json:"created_at"`
	UpdatedAt   time.Time         `json:"updated_at"`
}

// Subnet represents an IP subnet within a network.
type Subnet struct {
	ID              string    `json:"id"`
	Name            string    `json:"name"`
	NetworkID       string    `json:"network_id"`
	CIDR            string    `json:"cidr"`            // e.g., "10.0.0.0/24"
	GatewayIP       string    `json:"gateway_ip"`      // e.g., "10.0.0.1"
	DNSServers      []string  `json:"dns_servers"`     // e.g., ["8.8.8.8", "8.8.4.4"]
	AllocationPools []IPPool  `json:"allocation_pools"` // IP ranges for allocation
	EnableDHCP      bool      `json:"enable_dhcp"`
	IPv6            bool      `json:"ipv6"`
	CreatedAt       time.Time `json:"created_at"`
	UpdatedAt       time.Time `json:"updated_at"`
}

// IPPool represents a range of IP addresses available for allocation.
type IPPool struct {
	Start string `json:"start"` // e.g., "10.0.0.10"
	End   string `json:"end"`   // e.g., "10.0.0.254"
}

// IPAllocation represents an allocated IP address.
type IPAllocation struct {
	ID         string    `json:"id"`
	SubnetID   string    `json:"subnet_id"`
	IPAddress  string    `json:"ip_address"`
	MACAddress string    `json:"mac_address"`
	InstanceID string    `json:"instance_id,omitempty"` // VM/container using this IP
	PortID     string    `json:"port_id,omitempty"`     // Virtual port ID
	Hostname   string    `json:"hostname,omitempty"`
	Status     string    `json:"status"` // allocated, reserved, dhcp
	CreatedAt  time.Time `json:"created_at"`
}

// Port represents a virtual network port attached to an instance.
type Port struct {
	ID             string         `json:"id"`
	Name           string         `json:"name,omitempty"`
	NetworkID      string         `json:"network_id"`
	SubnetID       string         `json:"subnet_id"`
	MACAddress     string         `json:"mac_address"`
	IPAddress      string         `json:"ip_address"`
	InstanceID     string         `json:"instance_id,omitempty"`
	NodeID         string         `json:"node_id"` // Which node this port is on
	DeviceName     string         `json:"device_name,omitempty"` // tap0, veth0, etc.
	SecurityGroups []string       `json:"security_groups,omitempty"`
	AdminState     bool           `json:"admin_state"`
	Status         string         `json:"status"` // active, down, build
	BindingType    PortBindingType `json:"binding_type"`
	CreatedAt      time.Time      `json:"created_at"`
	UpdatedAt      time.Time      `json:"updated_at"`
}

// PortBindingType represents how a port is bound to an instance.
type PortBindingType string

const (
	PortBindingOVS       PortBindingType = "ovs"        // Open vSwitch
	PortBindingLinuxBridge PortBindingType = "linuxbridge" // Linux bridge
	PortBindingVhostUser PortBindingType = "vhost-user" // DPDK vhost-user
	PortBindingSRIOV     PortBindingType = "sriov"      // SR-IOV passthrough
)

// VTEP represents a VXLAN Tunnel Endpoint on a compute node.
type VTEP struct {
	NodeID    string    `json:"node_id"`
	IP        net.IP    `json:"ip"`         // Tunnel endpoint IP
	Port      uint16    `json:"port"`       // UDP port (default 4789)
	Interface string    `json:"interface"`  // VXLAN interface name
	Status    string    `json:"status"`     // active, inactive
	UpdatedAt time.Time `json:"updated_at"`
}

// Tunnel represents a VXLAN tunnel between two VTEPs.
type Tunnel struct {
	ID        string    `json:"id"`
	VNI       uint32    `json:"vni"`
	LocalVTEP  string   `json:"local_vtep"`  // Local node ID
	RemoteVTEP string   `json:"remote_vtep"` // Remote node ID
	RemoteIP  net.IP    `json:"remote_ip"`
	Status    string    `json:"status"`
	CreatedAt time.Time `json:"created_at"`
}

// SecurityGroup represents a set of network security rules.
type SecurityGroup struct {
	ID          string              `json:"id"`
	Name        string              `json:"name"`
	Description string              `json:"description,omitempty"`
	TenantID    string              `json:"tenant_id,omitempty"`
	Rules       []SecurityGroupRule `json:"rules"`
	CreatedAt   time.Time           `json:"created_at"`
	UpdatedAt   time.Time           `json:"updated_at"`
}

// SecurityGroupRule represents a single security rule.
type SecurityGroupRule struct {
	ID              string `json:"id"`
	SecurityGroupID string `json:"security_group_id"`
	Direction       string `json:"direction"` // ingress, egress
	EtherType       string `json:"ether_type"` // IPv4, IPv6
	Protocol        string `json:"protocol,omitempty"` // tcp, udp, icmp, any
	PortRangeMin    uint16 `json:"port_range_min,omitempty"`
	PortRangeMax    uint16 `json:"port_range_max,omitempty"`
	RemoteIPPrefix  string `json:"remote_ip_prefix,omitempty"` // CIDR
	RemoteGroupID   string `json:"remote_group_id,omitempty"`  // Reference to another SG
}

// FloatingIP represents a public IP associated with a private IP.
type FloatingIP struct {
	ID               string    `json:"id"`
	FloatingIP       string    `json:"floating_ip"`       // Public IP
	FloatingNetworkID string   `json:"floating_network_id"` // External network
	FixedIP          string    `json:"fixed_ip,omitempty"` // Private IP
	PortID           string    `json:"port_id,omitempty"`  // Associated port
	TenantID         string    `json:"tenant_id,omitempty"`
	Status           string    `json:"status"` // active, down
	CreatedAt        time.Time `json:"created_at"`
	UpdatedAt        time.Time `json:"updated_at"`
}

// Router represents a virtual router for L3 routing.
type Router struct {
	ID                  string           `json:"id"`
	Name                string           `json:"name"`
	TenantID            string           `json:"tenant_id,omitempty"`
	AdminState          bool             `json:"admin_state"`
	Status              string           `json:"status"`
	ExternalGatewayInfo *ExternalGateway `json:"external_gateway_info,omitempty"`
	Routes              []Route          `json:"routes,omitempty"`
	Distributed         bool             `json:"distributed"` // DVR mode
	CreatedAt           time.Time        `json:"created_at"`
	UpdatedAt           time.Time        `json:"updated_at"`
}

// ExternalGateway represents the external network connection of a router.
type ExternalGateway struct {
	NetworkID        string   `json:"network_id"`
	EnableSNAT       bool     `json:"enable_snat"`
	ExternalFixedIPs []FixedIP `json:"external_fixed_ips,omitempty"`
}

// FixedIP represents a fixed IP address assignment.
type FixedIP struct {
	SubnetID  string `json:"subnet_id"`
	IPAddress string `json:"ip_address"`
}

// Route represents a static route in a router.
type Route struct {
	Destination string `json:"destination"` // CIDR
	NextHop     string `json:"nexthop"`     // IP address
}

// RouterInterface represents a connection between a router and a subnet.
type RouterInterface struct {
	ID       string `json:"id"`
	RouterID string `json:"router_id"`
	SubnetID string `json:"subnet_id"`
	PortID   string `json:"port_id"`
}

// FlowRule represents an OpenFlow rule for the SDN controller.
type FlowRule struct {
	ID       string        `json:"id"`
	TableID  uint8         `json:"table_id"`
	Priority uint16        `json:"priority"`
	Cookie   uint64        `json:"cookie"`
	Match    FlowMatch     `json:"match"`
	Actions  []FlowAction  `json:"actions"`
	IdleTimeout uint16     `json:"idle_timeout,omitempty"`
	HardTimeout uint16     `json:"hard_timeout,omitempty"`
}

// FlowMatch represents OpenFlow match criteria.
type FlowMatch struct {
	InPort     uint32 `json:"in_port,omitempty"`
	DLSrc      string `json:"dl_src,omitempty"`      // Source MAC
	DLDst      string `json:"dl_dst,omitempty"`      // Dest MAC
	DLType     uint16 `json:"dl_type,omitempty"`     // EtherType
	DLVlan     uint16 `json:"dl_vlan,omitempty"`     // VLAN ID
	NWSrc      string `json:"nw_src,omitempty"`      // Source IP
	NWDst      string `json:"nw_dst,omitempty"`      // Dest IP
	NWProto    uint8  `json:"nw_proto,omitempty"`    // IP protocol
	TPSrc      uint16 `json:"tp_src,omitempty"`      // TCP/UDP src port
	TPDst      uint16 `json:"tp_dst,omitempty"`      // TCP/UDP dst port
	TunnelID   uint32 `json:"tunnel_id,omitempty"`   // VXLAN VNI
	Metadata   uint64 `json:"metadata,omitempty"`
}

// FlowAction represents an OpenFlow action.
type FlowAction struct {
	Type   FlowActionType `json:"type"`
	Value  interface{}    `json:"value,omitempty"`
}

// FlowActionType represents the type of OpenFlow action.
type FlowActionType string

const (
	FlowActionOutput     FlowActionType = "output"
	FlowActionSetField   FlowActionType = "set_field"
	FlowActionPushVLAN   FlowActionType = "push_vlan"
	FlowActionPopVLAN    FlowActionType = "pop_vlan"
	FlowActionPushVXLAN  FlowActionType = "push_vxlan"
	FlowActionPopVXLAN   FlowActionType = "pop_vxlan"
	FlowActionGotoTable  FlowActionType = "goto_table"
	FlowActionDrop       FlowActionType = "drop"
	FlowActionController FlowActionType = "controller"
	FlowActionGroup      FlowActionType = "group"
	FlowActionSetTunnel  FlowActionType = "set_tunnel"
)

// NetworkConfig holds configuration for the network subsystem.
type NetworkConfig struct {
	// OVS configuration
	OVSBridge        string `yaml:"ovs_bridge" json:"ovs_bridge"`                 // Default: "br-int"
	OVSTunnelBridge  string `yaml:"ovs_tunnel_bridge" json:"ovs_tunnel_bridge"`   // Default: "br-tun"

	// VXLAN configuration
	VXLANPort        uint16 `yaml:"vxlan_port" json:"vxlan_port"`                 // Default: 4789
	VXLANLocalIP     string `yaml:"vxlan_local_ip" json:"vxlan_local_ip"`         // Tunnel endpoint IP
	VXLANMTU         uint16 `yaml:"vxlan_mtu" json:"vxlan_mtu"`                   // Default: 1450

	// SDN controller configuration
	ControllerEnabled bool   `yaml:"controller_enabled" json:"controller_enabled"`
	OpenFlowVersion   string `yaml:"openflow_version" json:"openflow_version"`     // Default: "1.3"

	// IPAM configuration
	DefaultSubnetCIDR string `yaml:"default_subnet_cidr" json:"default_subnet_cidr"` // Default: "10.0.0.0/8"

	// DVR configuration
	DVREnabled       bool   `yaml:"dvr_enabled" json:"dvr_enabled"`
	DVRNamespace     string `yaml:"dvr_namespace" json:"dvr_namespace"`           // Default: "qrouter"
}

// DefaultNetworkConfig returns the default network configuration.
func DefaultNetworkConfig() *NetworkConfig {
	return &NetworkConfig{
		OVSBridge:         "br-int",
		OVSTunnelBridge:   "br-tun",
		VXLANPort:         4789,
		VXLANMTU:          1450,
		ControllerEnabled: true,
		OpenFlowVersion:   "1.3",
		DefaultSubnetCIDR: "10.0.0.0/8",
		DVREnabled:        true,
		DVRNamespace:      "qrouter",
	}
}
