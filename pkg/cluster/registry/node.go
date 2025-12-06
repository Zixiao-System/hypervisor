// Package registry provides node registration and discovery functionality.
package registry

import (
	"time"
)

// NodeRole represents the role of a node in the cluster.
type NodeRole string

const (
	NodeRoleMaster NodeRole = "master"
	NodeRoleWorker NodeRole = "worker"
)

// NodeStatus represents the current status of a node.
type NodeStatus string

const (
	NodeStatusReady       NodeStatus = "ready"
	NodeStatusNotReady    NodeStatus = "not_ready"
	NodeStatusMaintenance NodeStatus = "maintenance"
	NodeStatusDraining    NodeStatus = "draining"
)

// ConditionType represents the type of node condition.
type ConditionType string

const (
	ConditionReady              ConditionType = "Ready"
	ConditionDiskPressure       ConditionType = "DiskPressure"
	ConditionMemoryPressure     ConditionType = "MemoryPressure"
	ConditionNetworkUnavailable ConditionType = "NetworkUnavailable"
)

// ConditionStatus represents the status of a condition.
type ConditionStatus string

const (
	ConditionTrue    ConditionStatus = "True"
	ConditionFalse   ConditionStatus = "False"
	ConditionUnknown ConditionStatus = "Unknown"
)

// InstanceType represents the type of compute instance.
type InstanceType string

const (
	InstanceTypeVM        InstanceType = "vm"
	InstanceTypeContainer InstanceType = "container"
	InstanceTypeMicroVM   InstanceType = "microvm"
)

// Node represents a node in the cluster.
type Node struct {
	// Identity
	ID       string `json:"id"`
	Hostname string `json:"hostname"`
	IP       string `json:"ip"`
	Port     int    `json:"port"`

	// Role and status
	Role   NodeRole   `json:"role"`
	Status NodeStatus `json:"status"`

	// Location
	Region string `json:"region"`
	Zone   string `json:"zone"`

	// Resources
	Capacity    Resources `json:"capacity"`
	Allocatable Resources `json:"allocatable"`
	Allocated   Resources `json:"allocated"`

	// Health conditions
	Conditions []NodeCondition `json:"conditions"`

	// Metadata
	Labels      map[string]string `json:"labels"`
	Annotations map[string]string `json:"annotations"`

	// Supported instance types
	SupportedInstanceTypes []InstanceType `json:"supported_instance_types"`

	// Timestamps
	CreatedAt time.Time `json:"created_at"`
	LastSeen  time.Time `json:"last_seen"`
}

// Resources represents compute resources.
type Resources struct {
	CPUCores    int   `json:"cpu_cores"`
	MemoryBytes int64 `json:"memory_bytes"`
	DiskBytes   int64 `json:"disk_bytes"`
	GPUCount    int   `json:"gpu_count"`
}

// NodeCondition represents a condition of a node.
type NodeCondition struct {
	Type               ConditionType   `json:"type"`
	Status             ConditionStatus `json:"status"`
	Reason             string          `json:"reason"`
	Message            string          `json:"message"`
	LastTransitionTime time.Time       `json:"last_transition_time"`
}

// EventType represents the type of node event.
type EventType string

const (
	EventAdded    EventType = "added"
	EventModified EventType = "modified"
	EventDeleted  EventType = "deleted"
)

// NodeEvent represents an event related to a node.
type NodeEvent struct {
	Type EventType `json:"type"`
	Node *Node     `json:"node"`
}

// IsReady returns true if the node is ready.
func (n *Node) IsReady() bool {
	if n.Status != NodeStatusReady {
		return false
	}

	for _, cond := range n.Conditions {
		if cond.Type == ConditionReady && cond.Status == ConditionTrue {
			return true
		}
	}

	return false
}

// AvailableResources returns the resources available for scheduling.
func (n *Node) AvailableResources() Resources {
	return Resources{
		CPUCores:    n.Allocatable.CPUCores - n.Allocated.CPUCores,
		MemoryBytes: n.Allocatable.MemoryBytes - n.Allocated.MemoryBytes,
		DiskBytes:   n.Allocatable.DiskBytes - n.Allocated.DiskBytes,
		GPUCount:    n.Allocatable.GPUCount - n.Allocated.GPUCount,
	}
}

// CanSchedule returns true if the node can schedule the given resources.
func (n *Node) CanSchedule(required Resources) bool {
	avail := n.AvailableResources()

	return avail.CPUCores >= required.CPUCores &&
		avail.MemoryBytes >= required.MemoryBytes &&
		avail.DiskBytes >= required.DiskBytes &&
		avail.GPUCount >= required.GPUCount
}

// SupportsInstanceType returns true if the node supports the given instance type.
func (n *Node) SupportsInstanceType(t InstanceType) bool {
	for _, supported := range n.SupportedInstanceTypes {
		if supported == t {
			return true
		}
	}
	return false
}
