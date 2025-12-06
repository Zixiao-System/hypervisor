// Package heartbeat provides heartbeat service for node health monitoring.
package heartbeat

import (
	"context"
	"sync"
	"time"

	"hypervisor/pkg/cluster/etcd"
	"hypervisor/pkg/cluster/registry"

	clientv3 "go.etcd.io/etcd/client/v3"
	"go.uber.org/zap"
)

// Config holds the heartbeat service configuration.
type Config struct {
	// Interval between heartbeats
	Interval time.Duration `mapstructure:"interval"`

	// Timeout for considering a node dead
	Timeout time.Duration `mapstructure:"timeout"`

	// RetryInterval for failed heartbeats
	RetryInterval time.Duration `mapstructure:"retry_interval"`
}

// DefaultConfig returns the default heartbeat configuration.
func DefaultConfig() Config {
	return Config{
		Interval:      10 * time.Second,
		Timeout:       30 * time.Second,
		RetryInterval: 2 * time.Second,
	}
}

// Service provides heartbeat functionality.
type Service interface {
	// Start starts sending heartbeats.
	Start(ctx context.Context) error

	// Stop stops sending heartbeats.
	Stop() error

	// SendHeartbeat sends a single heartbeat.
	SendHeartbeat(ctx context.Context) error
}

// NodeCallback is called when a node's status changes.
type NodeCallback func(nodeID string, alive bool)

// HeartbeatService implements Service for sending heartbeats.
type HeartbeatService struct {
	client   *etcd.Client
	registry *registry.EtcdRegistry
	nodeID   string
	config   Config
	logger   *zap.Logger

	mu       sync.RWMutex
	running  bool
	cancel   context.CancelFunc
	leaseID  clientv3.LeaseID
	keepAlive <-chan *clientv3.LeaseKeepAliveResponse
}

// NewHeartbeatService creates a new heartbeat service.
func NewHeartbeatService(
	client *etcd.Client,
	reg *registry.EtcdRegistry,
	nodeID string,
	config Config,
	logger *zap.Logger,
) *HeartbeatService {
	if logger == nil {
		logger = zap.NewNop()
	}

	return &HeartbeatService{
		client:   client,
		registry: reg,
		nodeID:   nodeID,
		config:   config,
		logger:   logger,
	}
}

// Start starts sending heartbeats.
func (s *HeartbeatService) Start(ctx context.Context) error {
	s.mu.Lock()
	if s.running {
		s.mu.Unlock()
		return nil
	}
	s.running = true
	s.mu.Unlock()

	// Get the lease ID from registry
	leaseID, ok := s.registry.GetLeaseID(s.nodeID)
	if !ok {
		return ErrNoLease
	}
	s.leaseID = leaseID

	// Start keep-alive
	keepAlive, err := s.client.KeepAlive(ctx, leaseID)
	if err != nil {
		return err
	}
	s.keepAlive = keepAlive

	ctx, cancel := context.WithCancel(ctx)
	s.cancel = cancel

	go s.run(ctx)

	s.logger.Info("heartbeat service started",
		zap.String("node_id", s.nodeID),
		zap.Duration("interval", s.config.Interval),
	)

	return nil
}

// Stop stops sending heartbeats.
func (s *HeartbeatService) Stop() error {
	s.mu.Lock()
	defer s.mu.Unlock()

	if !s.running {
		return nil
	}

	s.running = false
	if s.cancel != nil {
		s.cancel()
	}

	s.logger.Info("heartbeat service stopped", zap.String("node_id", s.nodeID))
	return nil
}

// SendHeartbeat sends a single heartbeat.
func (s *HeartbeatService) SendHeartbeat(ctx context.Context) error {
	_, err := s.client.KeepAliveOnce(ctx, s.leaseID)
	if err != nil {
		return err
	}

	// Update node's last seen timestamp
	node, err := s.registry.Get(ctx, s.nodeID)
	if err != nil {
		return err
	}

	node.LastSeen = time.Now()
	return s.registry.Update(ctx, node)
}

func (s *HeartbeatService) run(ctx context.Context) {
	ticker := time.NewTicker(s.config.Interval)
	defer ticker.Stop()

	for {
		select {
		case <-ctx.Done():
			return

		case resp, ok := <-s.keepAlive:
			if !ok {
				s.logger.Warn("keep-alive channel closed, attempting to reconnect")
				s.handleKeepAliveFailure(ctx)
				return
			}

			if resp == nil {
				s.logger.Warn("received nil keep-alive response")
				continue
			}

			s.logger.Debug("keep-alive response received",
				zap.Int64("ttl", resp.TTL),
			)

		case <-ticker.C:
			if err := s.SendHeartbeat(ctx); err != nil {
				s.logger.Warn("failed to send heartbeat",
					zap.Error(err),
					zap.String("node_id", s.nodeID),
				)
			}
		}
	}
}

func (s *HeartbeatService) handleKeepAliveFailure(ctx context.Context) {
	s.mu.Lock()
	s.running = false
	s.mu.Unlock()

	// Attempt to re-register
	for i := 0; i < 3; i++ {
		select {
		case <-ctx.Done():
			return
		case <-time.After(s.config.RetryInterval):
		}

		node, err := s.registry.Get(ctx, s.nodeID)
		if err != nil {
			s.logger.Error("failed to get node for re-registration", zap.Error(err))
			continue
		}

		if _, err := s.registry.Register(ctx, node); err != nil {
			s.logger.Error("failed to re-register node", zap.Error(err))
			continue
		}

		if err := s.Start(ctx); err != nil {
			s.logger.Error("failed to restart heartbeat service", zap.Error(err))
			continue
		}

		s.logger.Info("successfully re-registered and restarted heartbeat")
		return
	}

	s.logger.Error("failed to recover from keep-alive failure after 3 attempts")
}

// Monitor monitors the health of nodes in the cluster.
type Monitor struct {
	registry *registry.EtcdRegistry
	config   Config
	logger   *zap.Logger
	callback NodeCallback

	mu      sync.RWMutex
	running bool
	cancel  context.CancelFunc
}

// NewMonitor creates a new heartbeat monitor.
func NewMonitor(reg *registry.EtcdRegistry, config Config, callback NodeCallback, logger *zap.Logger) *Monitor {
	if logger == nil {
		logger = zap.NewNop()
	}

	return &Monitor{
		registry: reg,
		config:   config,
		logger:   logger,
		callback: callback,
	}
}

// Start starts monitoring node heartbeats.
func (m *Monitor) Start(ctx context.Context) error {
	m.mu.Lock()
	if m.running {
		m.mu.Unlock()
		return nil
	}
	m.running = true
	m.mu.Unlock()

	ctx, cancel := context.WithCancel(ctx)
	m.cancel = cancel

	go m.run(ctx)

	m.logger.Info("heartbeat monitor started")
	return nil
}

// Stop stops monitoring.
func (m *Monitor) Stop() error {
	m.mu.Lock()
	defer m.mu.Unlock()

	if !m.running {
		return nil
	}

	m.running = false
	if m.cancel != nil {
		m.cancel()
	}

	m.logger.Info("heartbeat monitor stopped")
	return nil
}

func (m *Monitor) run(ctx context.Context) {
	ticker := time.NewTicker(m.config.Interval)
	defer ticker.Stop()

	for {
		select {
		case <-ctx.Done():
			return

		case <-ticker.C:
			m.checkNodes(ctx)
		}
	}
}

func (m *Monitor) checkNodes(ctx context.Context) {
	nodes, err := m.registry.List(ctx)
	if err != nil {
		m.logger.Error("failed to list nodes for health check", zap.Error(err))
		return
	}

	now := time.Now()
	for _, node := range nodes {
		alive := now.Sub(node.LastSeen) < m.config.Timeout

		if !alive && node.Status == registry.NodeStatusReady {
			m.logger.Warn("node appears to be dead",
				zap.String("node_id", node.ID),
				zap.Time("last_seen", node.LastSeen),
			)

			// Update node status
			node.Status = registry.NodeStatusNotReady
			if err := m.registry.Update(ctx, node); err != nil {
				m.logger.Error("failed to update node status", zap.Error(err))
			}

			if m.callback != nil {
				m.callback(node.ID, false)
			}
		}
	}
}
