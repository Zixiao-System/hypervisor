// Package registry provides node and instance registration and discovery functionality.
package registry

import (
	"time"

	"hypervisor/pkg/compute/driver"
)

// Instance represents a compute instance stored in the registry.
// It extends driver.Instance with cluster-specific fields.
type Instance struct {
	// Core fields from driver.Instance
	ID          string               `json:"id"`
	Name        string               `json:"name"`
	Type        driver.InstanceType  `json:"type"`
	State       driver.InstanceState `json:"state"`
	StateReason string               `json:"state_reason,omitempty"`
	Spec        driver.InstanceSpec  `json:"spec"`
	IPAddress   string               `json:"ip_address,omitempty"`

	// Cluster-specific fields
	NodeID string `json:"node_id"` // ID of the node where instance is running

	// Metadata
	Labels      map[string]string `json:"labels,omitempty"`
	Annotations map[string]string `json:"annotations,omitempty"`

	// Timestamps
	CreatedAt time.Time  `json:"created_at"`
	StartedAt *time.Time `json:"started_at,omitempty"`
	UpdatedAt time.Time  `json:"updated_at"`
}

// InstanceEvent represents an event related to an instance.
type InstanceEvent struct {
	Type     EventType `json:"type"`
	Instance *Instance `json:"instance"`
}

// IsRunning returns true if the instance is in running state.
func (i *Instance) IsRunning() bool {
	return i.State == driver.StateRunning
}

// IsStopped returns true if the instance is in stopped state.
func (i *Instance) IsStopped() bool {
	return i.State == driver.StateStopped
}

// IsFailed returns true if the instance is in failed state.
func (i *Instance) IsFailed() bool {
	return i.State == driver.StateFailed
}

// MatchesLabels checks if the instance has all the specified labels.
func (i *Instance) MatchesLabels(selector map[string]string) bool {
	if len(selector) == 0 {
		return true
	}
	for k, v := range selector {
		if i.Labels[k] != v {
			return false
		}
	}
	return true
}

// ToDriverInstance converts registry Instance to driver.Instance.
func (i *Instance) ToDriverInstance() *driver.Instance {
	return &driver.Instance{
		ID:          i.ID,
		Name:        i.Name,
		Type:        i.Type,
		State:       i.State,
		StateReason: i.StateReason,
		Spec:        i.Spec,
		IPAddress:   i.IPAddress,
		Metadata:    i.Labels,
		CreatedAt:   i.CreatedAt,
		StartedAt:   i.StartedAt,
	}
}

// NewInstanceFromDriver creates a registry Instance from a driver.Instance.
func NewInstanceFromDriver(d *driver.Instance, nodeID string) *Instance {
	now := time.Now()
	return &Instance{
		ID:          d.ID,
		Name:        d.Name,
		Type:        d.Type,
		State:       d.State,
		StateReason: d.StateReason,
		Spec:        d.Spec,
		IPAddress:   d.IPAddress,
		NodeID:      nodeID,
		Labels:      d.Metadata,
		CreatedAt:   d.CreatedAt,
		StartedAt:   d.StartedAt,
		UpdatedAt:   now,
	}
}
