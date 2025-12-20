package registry

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"strings"
	"sync"
	"time"

	"hypervisor/pkg/cluster/etcd"
	"hypervisor/pkg/compute/driver"

	clientv3 "go.etcd.io/etcd/client/v3"
	"go.uber.org/zap"
)

const (
	// Key prefixes in etcd
	instancePrefix       = "/hypervisor/instances/"
	instanceByNodePrefix = "/hypervisor/instances-by-node/"
)

// Common errors
var (
	ErrInstanceNotFound = errors.New("instance not found")
	ErrInstanceExists   = errors.New("instance already exists")
)

// InstanceRegistry provides instance registration and discovery.
type InstanceRegistry interface {
	// Create creates a new instance in the registry.
	Create(ctx context.Context, instance *Instance) error

	// Get retrieves an instance by ID.
	Get(ctx context.Context, instanceID string) (*Instance, error)

	// List returns all instances.
	List(ctx context.Context) ([]*Instance, error)

	// ListByNode returns all instances on a specific node.
	ListByNode(ctx context.Context, nodeID string) ([]*Instance, error)

	// ListByType returns all instances of a specific type.
	ListByType(ctx context.Context, instanceType driver.InstanceType) ([]*Instance, error)

	// ListByState returns all instances in a specific state.
	ListByState(ctx context.Context, state driver.InstanceState) ([]*Instance, error)

	// Update updates an instance's information.
	Update(ctx context.Context, instance *Instance) error

	// UpdateState updates an instance's state.
	UpdateState(ctx context.Context, instanceID string, state driver.InstanceState, reason string) error

	// Delete removes an instance from the registry.
	Delete(ctx context.Context, instanceID string) error

	// Watch watches for instance changes.
	Watch(ctx context.Context) (<-chan InstanceEvent, error)

	// Close closes the registry.
	Close() error
}

// EtcdInstanceRegistry implements InstanceRegistry using etcd.
type EtcdInstanceRegistry struct {
	client *etcd.Client
	logger *zap.Logger

	// Watch cancel function
	mu          sync.RWMutex
	watchCancel context.CancelFunc
}

// NewEtcdInstanceRegistry creates a new etcd-based instance registry.
func NewEtcdInstanceRegistry(client *etcd.Client, logger *zap.Logger) *EtcdInstanceRegistry {
	if logger == nil {
		logger = zap.NewNop()
	}

	return &EtcdInstanceRegistry{
		client: client,
		logger: logger,
	}
}

// Create creates a new instance in the registry.
func (r *EtcdInstanceRegistry) Create(ctx context.Context, instance *Instance) error {
	// Check if instance already exists
	_, err := r.Get(ctx, instance.ID)
	if err == nil {
		return ErrInstanceExists
	}
	if err != ErrInstanceNotFound {
		return err
	}

	// Set timestamps
	now := time.Now()
	if instance.CreatedAt.IsZero() {
		instance.CreatedAt = now
	}
	instance.UpdatedAt = now

	// Serialize instance
	data, err := json.Marshal(instance)
	if err != nil {
		return fmt.Errorf("failed to marshal instance: %w", err)
	}

	// Store in etcd (main key)
	key := instancePrefix + instance.ID
	if err := r.client.Put(ctx, key, string(data)); err != nil {
		return fmt.Errorf("failed to create instance: %w", err)
	}

	// Store node index (for quick lookup by node)
	if instance.NodeID != "" {
		nodeIndexKey := instanceByNodePrefix + instance.NodeID + "/" + instance.ID
		if err := r.client.Put(ctx, nodeIndexKey, instance.ID); err != nil {
			r.logger.Warn("failed to create node index", zap.Error(err))
		}
	}

	r.logger.Info("instance created",
		zap.String("instance_id", instance.ID),
		zap.String("name", instance.Name),
		zap.String("type", string(instance.Type)),
		zap.String("node_id", instance.NodeID),
	)

	return nil
}

// Get retrieves an instance by ID.
func (r *EtcdInstanceRegistry) Get(ctx context.Context, instanceID string) (*Instance, error) {
	key := instancePrefix + instanceID
	data, err := r.client.Get(ctx, key)
	if err != nil {
		if err == etcd.ErrKeyNotFound {
			return nil, ErrInstanceNotFound
		}
		return nil, fmt.Errorf("failed to get instance: %w", err)
	}

	var instance Instance
	if err := json.Unmarshal([]byte(data), &instance); err != nil {
		return nil, fmt.Errorf("failed to unmarshal instance: %w", err)
	}

	return &instance, nil
}

// List returns all instances.
func (r *EtcdInstanceRegistry) List(ctx context.Context) ([]*Instance, error) {
	data, err := r.client.GetWithPrefix(ctx, instancePrefix)
	if err != nil {
		return nil, fmt.Errorf("failed to list instances: %w", err)
	}

	instances := make([]*Instance, 0, len(data))
	for _, v := range data {
		var instance Instance
		if err := json.Unmarshal([]byte(v), &instance); err != nil {
			r.logger.Warn("failed to unmarshal instance", zap.Error(err))
			continue
		}
		instances = append(instances, &instance)
	}

	return instances, nil
}

// ListByNode returns all instances on a specific node.
func (r *EtcdInstanceRegistry) ListByNode(ctx context.Context, nodeID string) ([]*Instance, error) {
	// Get instance IDs from node index
	indexPrefix := instanceByNodePrefix + nodeID + "/"
	data, err := r.client.GetWithPrefix(ctx, indexPrefix)
	if err != nil {
		return nil, fmt.Errorf("failed to list instances by node: %w", err)
	}

	instances := make([]*Instance, 0, len(data))
	for _, instanceID := range data {
		instance, err := r.Get(ctx, instanceID)
		if err != nil {
			if err == ErrInstanceNotFound {
				// Instance was deleted, clean up stale index
				continue
			}
			r.logger.Warn("failed to get instance", zap.String("id", instanceID), zap.Error(err))
			continue
		}
		instances = append(instances, instance)
	}

	return instances, nil
}

// ListByType returns all instances of a specific type.
func (r *EtcdInstanceRegistry) ListByType(ctx context.Context, instanceType driver.InstanceType) ([]*Instance, error) {
	instances, err := r.List(ctx)
	if err != nil {
		return nil, err
	}

	filtered := make([]*Instance, 0)
	for _, instance := range instances {
		if instance.Type == instanceType {
			filtered = append(filtered, instance)
		}
	}

	return filtered, nil
}

// ListByState returns all instances in a specific state.
func (r *EtcdInstanceRegistry) ListByState(ctx context.Context, state driver.InstanceState) ([]*Instance, error) {
	instances, err := r.List(ctx)
	if err != nil {
		return nil, err
	}

	filtered := make([]*Instance, 0)
	for _, instance := range instances {
		if instance.State == state {
			filtered = append(filtered, instance)
		}
	}

	return filtered, nil
}

// Update updates an instance's information.
func (r *EtcdInstanceRegistry) Update(ctx context.Context, instance *Instance) error {
	// Get existing instance to check node change
	existing, err := r.Get(ctx, instance.ID)
	if err != nil {
		return err
	}

	// Update timestamp
	instance.UpdatedAt = time.Now()

	// Serialize instance
	data, err := json.Marshal(instance)
	if err != nil {
		return fmt.Errorf("failed to marshal instance: %w", err)
	}

	// Store in etcd
	key := instancePrefix + instance.ID
	if err := r.client.Put(ctx, key, string(data)); err != nil {
		return fmt.Errorf("failed to update instance: %w", err)
	}

	// Handle node change (update indexes)
	if existing.NodeID != instance.NodeID {
		// Remove old index
		if existing.NodeID != "" {
			oldIndexKey := instanceByNodePrefix + existing.NodeID + "/" + instance.ID
			if err := r.client.Delete(ctx, oldIndexKey); err != nil {
				r.logger.Warn("failed to delete old node index", zap.Error(err))
			}
		}

		// Create new index
		if instance.NodeID != "" {
			newIndexKey := instanceByNodePrefix + instance.NodeID + "/" + instance.ID
			if err := r.client.Put(ctx, newIndexKey, instance.ID); err != nil {
				r.logger.Warn("failed to create new node index", zap.Error(err))
			}
		}
	}

	return nil
}

// UpdateState updates an instance's state.
func (r *EtcdInstanceRegistry) UpdateState(ctx context.Context, instanceID string, state driver.InstanceState, reason string) error {
	instance, err := r.Get(ctx, instanceID)
	if err != nil {
		return err
	}

	instance.State = state
	instance.StateReason = reason

	// Update StartedAt if transitioning to running
	if state == driver.StateRunning && instance.StartedAt == nil {
		now := time.Now()
		instance.StartedAt = &now
	}

	return r.Update(ctx, instance)
}

// Delete removes an instance from the registry.
func (r *EtcdInstanceRegistry) Delete(ctx context.Context, instanceID string) error {
	// Get instance first to clean up indexes
	instance, err := r.Get(ctx, instanceID)
	if err != nil {
		if err == ErrInstanceNotFound {
			return nil // Already deleted
		}
		return err
	}

	// Delete main key
	key := instancePrefix + instanceID
	if err := r.client.Delete(ctx, key); err != nil {
		return fmt.Errorf("failed to delete instance: %w", err)
	}

	// Delete node index
	if instance.NodeID != "" {
		indexKey := instanceByNodePrefix + instance.NodeID + "/" + instanceID
		if err := r.client.Delete(ctx, indexKey); err != nil {
			r.logger.Warn("failed to delete node index", zap.Error(err))
		}
	}

	r.logger.Info("instance deleted", zap.String("instance_id", instanceID))
	return nil
}

// Watch watches for instance changes.
func (r *EtcdInstanceRegistry) Watch(ctx context.Context) (<-chan InstanceEvent, error) {
	events := make(chan InstanceEvent, 100)

	watchCtx, cancel := context.WithCancel(ctx)
	r.mu.Lock()
	r.watchCancel = cancel
	r.mu.Unlock()

	watchChan := r.client.WatchWithPrefix(watchCtx, instancePrefix)

	go func() {
		defer close(events)

		for resp := range watchChan {
			for _, ev := range resp.Events {
				// Skip node index keys
				if strings.HasPrefix(string(ev.Kv.Key), instanceByNodePrefix) {
					continue
				}

				var eventType EventType
				var instance *Instance

				switch ev.Type {
				case clientv3.EventTypePut:
					if ev.IsCreate() {
						eventType = EventAdded
					} else {
						eventType = EventModified
					}

					var i Instance
					if err := json.Unmarshal(ev.Kv.Value, &i); err != nil {
						r.logger.Warn("failed to unmarshal instance event", zap.Error(err))
						continue
					}
					instance = &i

				case clientv3.EventTypeDelete:
					eventType = EventDeleted
					// Extract instance ID from key
					instanceID := strings.TrimPrefix(string(ev.Kv.Key), instancePrefix)
					instance = &Instance{ID: instanceID}
				}

				select {
				case events <- InstanceEvent{Type: eventType, Instance: instance}:
				case <-watchCtx.Done():
					return
				}
			}
		}
	}()

	return events, nil
}

// Close closes the registry.
func (r *EtcdInstanceRegistry) Close() error {
	r.mu.Lock()
	defer r.mu.Unlock()

	if r.watchCancel != nil {
		r.watchCancel()
	}
	return nil
}
