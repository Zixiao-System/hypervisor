// Package driver defines the compute driver interface.
package driver

import (
	"context"
	"io"
	"time"
)

// InstanceType represents the type of compute instance.
type InstanceType string

const (
	InstanceTypeVM        InstanceType = "vm"
	InstanceTypeContainer InstanceType = "container"
	InstanceTypeMicroVM   InstanceType = "microvm"
)

// InstanceState represents the state of an instance.
type InstanceState string

const (
	StateUnknown  InstanceState = "unknown"
	StatePending  InstanceState = "pending"
	StateCreating InstanceState = "creating"
	StateRunning  InstanceState = "running"
	StateStopped  InstanceState = "stopped"
	StatePaused   InstanceState = "paused"
	StateFailed   InstanceState = "failed"
)

// Instance represents a compute instance (VM, container, or microVM).
type Instance struct {
	ID          string            `json:"id"`
	Name        string            `json:"name"`
	Type        InstanceType      `json:"type"`
	State       InstanceState     `json:"state"`
	StateReason string            `json:"state_reason,omitempty"`
	Spec        InstanceSpec      `json:"spec"`
	IPAddress   string            `json:"ip_address,omitempty"`
	Metadata    map[string]string `json:"metadata,omitempty"`
	CreatedAt   time.Time         `json:"created_at"`
	StartedAt   *time.Time        `json:"started_at,omitempty"`
}

// InstanceSpec defines the specification for creating an instance.
type InstanceSpec struct {
	// Common fields
	Image    string `json:"image"`
	CPUCores int    `json:"cpu_cores"`
	MemoryMB int64  `json:"memory_mb"`
	DiskGB   int64  `json:"disk_gb"`

	// VM-specific
	Kernel     string `json:"kernel,omitempty"`
	Initrd     string `json:"initrd,omitempty"`
	KernelArgs string `json:"kernel_args,omitempty"`

	// Container-specific
	Command    []string          `json:"command,omitempty"`
	Args       []string          `json:"args,omitempty"`
	Env        map[string]string `json:"env,omitempty"`
	WorkingDir string            `json:"working_dir,omitempty"`

	// Network
	Network NetworkSpec `json:"network"`

	// Disks
	Disks []DiskSpec `json:"disks,omitempty"`

	// Resource limits
	Limits ResourceLimits `json:"limits,omitempty"`
}

// NetworkSpec defines network configuration.
type NetworkSpec struct {
	NetworkID      string   `json:"network_id,omitempty"`
	SubnetID       string   `json:"subnet_id,omitempty"`
	SecurityGroups []string `json:"security_groups,omitempty"`
	AssignPublicIP bool     `json:"assign_public_ip,omitempty"`
	MACAddress     string   `json:"mac_address,omitempty"`
	IPAddress      string   `json:"ip_address,omitempty"`

	// VXLAN overlay networking
	OverlayType OverlayType `json:"overlay_type,omitempty"` // vxlan, vlan, bridge
	VNI         uint32      `json:"vni,omitempty"`          // VXLAN Network Identifier
	GatewayIP   string      `json:"gateway_ip,omitempty"`   // Subnet gateway
	Subnet      string      `json:"subnet,omitempty"`       // CIDR notation
	MTU         uint16      `json:"mtu,omitempty"`          // Network MTU

	// Port binding configuration
	PortID      string          `json:"port_id,omitempty"`      // Pre-created port ID
	BindingType PortBindingType `json:"binding_type,omitempty"` // ovs, vhost-user, sriov
	DeviceName  string          `json:"device_name,omitempty"`  // tap0, veth0, etc.
}

// OverlayType represents the type of network overlay.
type OverlayType string

const (
	OverlayTypeNone   OverlayType = ""
	OverlayTypeVXLAN  OverlayType = "vxlan"
	OverlayTypeVLAN   OverlayType = "vlan"
	OverlayTypeBridge OverlayType = "bridge"
)

// PortBindingType represents how a port is bound to an instance.
type PortBindingType string

const (
	PortBindingOVS         PortBindingType = "ovs"
	PortBindingLinuxBridge PortBindingType = "linuxbridge"
	PortBindingVhostUser   PortBindingType = "vhost-user"
	PortBindingSRIOV       PortBindingType = "sriov"
)

// DiskSpec defines disk configuration.
type DiskSpec struct {
	Name       string `json:"name"`
	SizeGB     int64  `json:"size_gb"`
	Type       string `json:"type"` // ssd, hdd
	SourcePath string `json:"source_path,omitempty"`
	Boot       bool   `json:"boot,omitempty"`
}

// ResourceLimits defines resource limits for an instance.
type ResourceLimits struct {
	CPUQuota    int64 `json:"cpu_quota,omitempty"`    // CPU quota in microseconds
	CPUPeriod   int64 `json:"cpu_period,omitempty"`   // CPU period in microseconds
	MemoryLimit int64 `json:"memory_limit,omitempty"` // Memory limit in bytes
	IOReadBPS   int64 `json:"io_read_bps,omitempty"`  // IO read bytes per second
	IOWriteBPS  int64 `json:"io_write_bps,omitempty"` // IO write bytes per second
}

// InstanceStats contains runtime statistics for an instance.
type InstanceStats struct {
	InstanceID       string    `json:"instance_id"`
	CPUUsagePercent  float64   `json:"cpu_usage_percent"`
	CPUTimeNs        uint64    `json:"cpu_time_ns"`
	MemoryUsedBytes  uint64    `json:"memory_used_bytes"`
	MemoryCacheBytes uint64    `json:"memory_cache_bytes"`
	DiskReadBytes    uint64    `json:"disk_read_bytes"`
	DiskWriteBytes   uint64    `json:"disk_write_bytes"`
	NetworkRxBytes   uint64    `json:"network_rx_bytes"`
	NetworkTxBytes   uint64    `json:"network_tx_bytes"`
	CollectedAt      time.Time `json:"collected_at"`
}

// AttachOptions defines options for attaching to an instance console.
type AttachOptions struct {
	TTY    bool `json:"tty"`
	Stdin  bool `json:"stdin"`
	Stdout bool `json:"stdout"`
	Stderr bool `json:"stderr"`
	Width  int  `json:"width,omitempty"`
	Height int  `json:"height,omitempty"`
}

// Driver is the interface that all compute drivers must implement.
type Driver interface {
	// Name returns the name of the driver.
	Name() string

	// Type returns the instance type this driver handles.
	Type() InstanceType

	// Create creates a new instance.
	Create(ctx context.Context, spec *InstanceSpec) (*Instance, error)

	// Start starts a stopped instance.
	Start(ctx context.Context, id string) error

	// Stop stops a running instance.
	Stop(ctx context.Context, id string, force bool) error

	// Delete deletes an instance.
	Delete(ctx context.Context, id string) error

	// Get retrieves an instance by ID.
	Get(ctx context.Context, id string) (*Instance, error)

	// List lists all instances.
	List(ctx context.Context) ([]*Instance, error)

	// Stats returns runtime statistics for an instance.
	Stats(ctx context.Context, id string) (*InstanceStats, error)

	// Attach attaches to an instance's console.
	Attach(ctx context.Context, id string, opts AttachOptions) (io.ReadWriteCloser, error)

	// Restart restarts an instance.
	Restart(ctx context.Context, id string, force bool) error

	// Close releases any resources held by the driver.
	Close() error
}

// HostInfo contains information about the host.
type HostInfo struct {
	Hostname          string `json:"hostname"`
	CPUCores          int    `json:"cpu_cores"`
	MemoryBytes       int64  `json:"memory_bytes"`
	FreeMemoryBytes   int64  `json:"free_memory_bytes"`
	HypervisorType    string `json:"hypervisor_type"`
	HypervisorVersion string `json:"hypervisor_version"`
}

// HostDriver extends Driver with host information capabilities.
type HostDriver interface {
	Driver

	// GetHostInfo returns information about the host.
	GetHostInfo(ctx context.Context) (*HostInfo, error)
}
