package registry

import (
	"context"
	"encoding/json"
	"fmt"
	"sync"
	"time"

	"hypervisor/pkg/cluster/etcd"

	"github.com/google/uuid"
	clientv3 "go.etcd.io/etcd/client/v3"
	"go.uber.org/zap"
)

const (
	// Key prefixes in etcd
	nodePrefix = "/hypervisor/nodes/"

	// Default lease TTL
	defaultLeaseTTL = 30 // seconds
)

// Registry provides node registration and discovery.
type Registry interface {
	// Register registers a node and returns its ID.
	Register(ctx context.Context, node *Node) (string, error)

	// Deregister removes a node from the registry.
	Deregister(ctx context.Context, nodeID string) error

	// Get retrieves a node by ID.
	Get(ctx context.Context, nodeID string) (*Node, error)

	// List returns all registered nodes.
	List(ctx context.Context) ([]*Node, error)

	// ListByRole returns all nodes with the given role.
	ListByRole(ctx context.Context, role NodeRole) ([]*Node, error)

	// ListByRegion returns all nodes in the given region.
	ListByRegion(ctx context.Context, region string) ([]*Node, error)

	// Update updates a node's information.
	Update(ctx context.Context, node *Node) error

	// UpdateStatus updates a node's status.
	UpdateStatus(ctx context.Context, nodeID string, status NodeStatus, conditions []NodeCondition) error

	// Watch watches for node changes.
	Watch(ctx context.Context) (<-chan NodeEvent, error)

	// Close closes the registry.
	Close() error
}

// EtcdRegistry implements Registry using etcd.
type EtcdRegistry struct {
	client *etcd.Client
	logger *zap.Logger

	// Lease management for registered nodes
	mu       sync.RWMutex
	leases   map[string]clientv3.LeaseID
	leaseTTL int64

	// Watch cancel function
	watchCancel context.CancelFunc
}

// NewEtcdRegistry creates a new etcd-based registry.
func NewEtcdRegistry(client *etcd.Client, logger *zap.Logger) *EtcdRegistry {
	if logger == nil {
		logger = zap.NewNop()
	}

	return &EtcdRegistry{
		client:   client,
		logger:   logger,
		leases:   make(map[string]clientv3.LeaseID),
		leaseTTL: defaultLeaseTTL,
	}
}

// Register registers a node and returns its ID.
func (r *EtcdRegistry) Register(ctx context.Context, node *Node) (string, error) {
	// Generate node ID if not provided
	if node.ID == "" {
		node.ID = uuid.New().String()
	}

	// Set timestamps
	now := time.Now()
	node.CreatedAt = now
	node.LastSeen = now

	// Create lease
	lease, err := r.client.Grant(ctx, r.leaseTTL)
	if err != nil {
		return "", fmt.Errorf("failed to create lease: %w", err)
	}

	// Store lease
	r.mu.Lock()
	r.leases[node.ID] = lease.ID
	r.mu.Unlock()

	// Serialize node
	data, err := json.Marshal(node)
	if err != nil {
		return "", fmt.Errorf("failed to marshal node: %w", err)
	}

	// Store in etcd with lease
	key := nodePrefix + node.ID
	if err := r.client.PutWithLease(ctx, key, string(data), lease.ID); err != nil {
		return "", fmt.Errorf("failed to register node: %w", err)
	}

	r.logger.Info("node registered",
		zap.String("node_id", node.ID),
		zap.String("hostname", node.Hostname),
		zap.String("role", string(node.Role)),
	)

	return node.ID, nil
}

// Deregister removes a node from the registry.
func (r *EtcdRegistry) Deregister(ctx context.Context, nodeID string) error {
	// Revoke lease if exists
	r.mu.Lock()
	leaseID, exists := r.leases[nodeID]
	if exists {
		delete(r.leases, nodeID)
	}
	r.mu.Unlock()

	if exists {
		if err := r.client.Revoke(ctx, leaseID); err != nil {
			r.logger.Warn("failed to revoke lease", zap.Error(err))
		}
	}

	// Delete from etcd
	key := nodePrefix + nodeID
	if err := r.client.Delete(ctx, key); err != nil {
		return fmt.Errorf("failed to deregister node: %w", err)
	}

	r.logger.Info("node deregistered", zap.String("node_id", nodeID))
	return nil
}

// Get retrieves a node by ID.
func (r *EtcdRegistry) Get(ctx context.Context, nodeID string) (*Node, error) {
	key := nodePrefix + nodeID
	data, err := r.client.Get(ctx, key)
	if err != nil {
		if err == etcd.ErrKeyNotFound {
			return nil, ErrNodeNotFound
		}
		return nil, fmt.Errorf("failed to get node: %w", err)
	}

	var node Node
	if err := json.Unmarshal([]byte(data), &node); err != nil {
		return nil, fmt.Errorf("failed to unmarshal node: %w", err)
	}

	return &node, nil
}

// List returns all registered nodes.
func (r *EtcdRegistry) List(ctx context.Context) ([]*Node, error) {
	data, err := r.client.GetWithPrefix(ctx, nodePrefix)
	if err != nil {
		return nil, fmt.Errorf("failed to list nodes: %w", err)
	}

	nodes := make([]*Node, 0, len(data))
	for _, v := range data {
		var node Node
		if err := json.Unmarshal([]byte(v), &node); err != nil {
			r.logger.Warn("failed to unmarshal node", zap.Error(err))
			continue
		}
		nodes = append(nodes, &node)
	}

	return nodes, nil
}

// ListByRole returns all nodes with the given role.
func (r *EtcdRegistry) ListByRole(ctx context.Context, role NodeRole) ([]*Node, error) {
	nodes, err := r.List(ctx)
	if err != nil {
		return nil, err
	}

	filtered := make([]*Node, 0)
	for _, node := range nodes {
		if node.Role == role {
			filtered = append(filtered, node)
		}
	}

	return filtered, nil
}

// ListByRegion returns all nodes in the given region.
func (r *EtcdRegistry) ListByRegion(ctx context.Context, region string) ([]*Node, error) {
	nodes, err := r.List(ctx)
	if err != nil {
		return nil, err
	}

	filtered := make([]*Node, 0)
	for _, node := range nodes {
		if node.Region == region {
			filtered = append(filtered, node)
		}
	}

	return filtered, nil
}

// Update updates a node's information.
func (r *EtcdRegistry) Update(ctx context.Context, node *Node) error {
	node.LastSeen = time.Now()

	data, err := json.Marshal(node)
	if err != nil {
		return fmt.Errorf("failed to marshal node: %w", err)
	}

	key := nodePrefix + node.ID

	r.mu.RLock()
	leaseID, hasLease := r.leases[node.ID]
	r.mu.RUnlock()

	if hasLease {
		if err := r.client.PutWithLease(ctx, key, string(data), leaseID); err != nil {
			return fmt.Errorf("failed to update node: %w", err)
		}
	} else {
		if err := r.client.Put(ctx, key, string(data)); err != nil {
			return fmt.Errorf("failed to update node: %w", err)
		}
	}

	return nil
}

// UpdateStatus updates a node's status.
func (r *EtcdRegistry) UpdateStatus(ctx context.Context, nodeID string, status NodeStatus, conditions []NodeCondition) error {
	node, err := r.Get(ctx, nodeID)
	if err != nil {
		return err
	}

	node.Status = status
	node.Conditions = conditions

	return r.Update(ctx, node)
}

// Watch watches for node changes.
func (r *EtcdRegistry) Watch(ctx context.Context) (<-chan NodeEvent, error) {
	events := make(chan NodeEvent, 100)

	watchCtx, cancel := context.WithCancel(ctx)
	r.watchCancel = cancel

	watchChan := r.client.WatchWithPrefix(watchCtx, nodePrefix)

	go func() {
		defer close(events)

		for resp := range watchChan {
			for _, ev := range resp.Events {
				var eventType EventType
				var node *Node

				switch ev.Type {
				case clientv3.EventTypePut:
					if ev.IsCreate() {
						eventType = EventAdded
					} else {
						eventType = EventModified
					}

					var n Node
					if err := json.Unmarshal(ev.Kv.Value, &n); err != nil {
						r.logger.Warn("failed to unmarshal node event", zap.Error(err))
						continue
					}
					node = &n

				case clientv3.EventTypeDelete:
					eventType = EventDeleted
					// Extract node ID from key
					nodeID := string(ev.Kv.Key)[len(nodePrefix):]
					node = &Node{ID: nodeID}
				}

				select {
				case events <- NodeEvent{Type: eventType, Node: node}:
				case <-watchCtx.Done():
					return
				}
			}
		}
	}()

	return events, nil
}

// Close closes the registry.
func (r *EtcdRegistry) Close() error {
	if r.watchCancel != nil {
		r.watchCancel()
	}
	return nil
}

// GetLeaseID returns the lease ID for a node.
func (r *EtcdRegistry) GetLeaseID(nodeID string) (clientv3.LeaseID, bool) {
	r.mu.RLock()
	defer r.mu.RUnlock()
	id, ok := r.leases[nodeID]
	return id, ok
}
