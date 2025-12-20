package server

import (
	"context"
	"fmt"
	"sync"

	v1 "hypervisor/api/gen"
	"hypervisor/pkg/cluster/registry"

	"go.uber.org/zap"
	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials/insecure"
)

// AgentClientPool manages gRPC connections to agent nodes.
type AgentClientPool struct {
	registry *registry.EtcdRegistry
	logger   *zap.Logger

	mu      sync.RWMutex
	clients map[string]*agentConnection
}

// agentConnection holds a gRPC connection and client to an agent.
type agentConnection struct {
	conn   *grpc.ClientConn
	client v1.AgentServiceClient
}

// NewAgentClientPool creates a new agent client pool.
func NewAgentClientPool(reg *registry.EtcdRegistry, logger *zap.Logger) *AgentClientPool {
	if logger == nil {
		logger = zap.NewNop()
	}

	return &AgentClientPool{
		registry: reg,
		logger:   logger,
		clients:  make(map[string]*agentConnection),
	}
}

// GetClient returns an AgentServiceClient for the given node.
// It caches connections for reuse.
func (p *AgentClientPool) GetClient(ctx context.Context, nodeID string) (v1.AgentServiceClient, error) {
	// Try to get from cache first
	p.mu.RLock()
	if ac, ok := p.clients[nodeID]; ok {
		p.mu.RUnlock()
		return ac.client, nil
	}
	p.mu.RUnlock()

	// Get node info from registry
	node, err := p.registry.Get(ctx, nodeID)
	if err != nil {
		return nil, fmt.Errorf("failed to get node %s: %w", nodeID, err)
	}

	// Build agent address
	addr := fmt.Sprintf("%s:%d", node.IP, node.Port)

	// Create gRPC connection
	conn, err := grpc.NewClient(addr,
		grpc.WithTransportCredentials(insecure.NewCredentials()),
	)
	if err != nil {
		return nil, fmt.Errorf("failed to connect to agent %s at %s: %w", nodeID, addr, err)
	}

	// Create client
	client := v1.NewAgentServiceClient(conn)

	// Cache the connection
	p.mu.Lock()
	// Double-check in case another goroutine created it
	if ac, ok := p.clients[nodeID]; ok {
		p.mu.Unlock()
		conn.Close() // Close our new connection since we'll use the cached one
		return ac.client, nil
	}
	p.clients[nodeID] = &agentConnection{
		conn:   conn,
		client: client,
	}
	p.mu.Unlock()

	p.logger.Debug("created agent connection",
		zap.String("node_id", nodeID),
		zap.String("addr", addr),
	)

	return client, nil
}

// RemoveClient removes a cached client connection.
// This should be called when a node is deregistered or unhealthy.
func (p *AgentClientPool) RemoveClient(nodeID string) {
	p.mu.Lock()
	defer p.mu.Unlock()

	if ac, ok := p.clients[nodeID]; ok {
		ac.conn.Close()
		delete(p.clients, nodeID)
		p.logger.Debug("removed agent connection", zap.String("node_id", nodeID))
	}
}

// Close closes all cached connections.
func (p *AgentClientPool) Close() error {
	p.mu.Lock()
	defer p.mu.Unlock()

	for nodeID, ac := range p.clients {
		if err := ac.conn.Close(); err != nil {
			p.logger.Warn("failed to close agent connection",
				zap.String("node_id", nodeID),
				zap.Error(err),
			)
		}
	}

	p.clients = make(map[string]*agentConnection)
	return nil
}

// Ping tests connectivity to an agent.
func (p *AgentClientPool) Ping(ctx context.Context, nodeID string) error {
	client, err := p.GetClient(ctx, nodeID)
	if err != nil {
		return err
	}

	// Try to list instances as a health check
	_, err = client.ListInstances(ctx, nil)
	return err
}
