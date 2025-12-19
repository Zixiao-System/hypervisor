// Package agent provides the hypervisor agent implementation.
package agent

import (
	"context"
	"fmt"
	"os"
	"sync"
	"time"

	"hypervisor/pkg/cluster/etcd"
	"hypervisor/pkg/cluster/heartbeat"
	"hypervisor/pkg/cluster/registry"
	"hypervisor/pkg/compute/driver"
	"hypervisor/pkg/compute/libvirt"

	"go.uber.org/zap"
	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials/insecure"
)

// Config holds the agent configuration.
type Config struct {
	// NodeID is the unique identifier for this node (auto-generated if empty).
	NodeID string `mapstructure:"node_id"`

	// Hostname is the hostname of this node.
	Hostname string `mapstructure:"hostname"`

	// IP is the IP address of this node.
	IP string `mapstructure:"ip"`

	// Port is the port for the agent gRPC server.
	Port int `mapstructure:"port"`

	// Role is the role of this node.
	Role string `mapstructure:"role"`

	// Region is the region where this node is located.
	Region string `mapstructure:"region"`

	// Zone is the availability zone.
	Zone string `mapstructure:"zone"`

	// ServerAddr is the address of the hypervisor server.
	ServerAddr string `mapstructure:"server_addr"`

	// Labels are custom labels for this node.
	Labels map[string]string `mapstructure:"labels"`

	// Etcd configuration
	Etcd etcd.Config `mapstructure:"etcd"`

	// Heartbeat configuration
	Heartbeat heartbeat.Config `mapstructure:"heartbeat"`

	// Libvirt configuration
	Libvirt libvirt.Config `mapstructure:"libvirt"`

	// SupportedInstanceTypes lists the instance types this node supports.
	SupportedInstanceTypes []string `mapstructure:"supported_instance_types"`
}

// DefaultConfig returns the default agent configuration.
func DefaultConfig() Config {
	hostname, _ := os.Hostname()

	return Config{
		Hostname:               hostname,
		Port:                   50052,
		Role:                   "worker",
		Region:                 "default",
		Zone:                   "default",
		ServerAddr:             "localhost:50051",
		Labels:                 make(map[string]string),
		Etcd:                   etcd.DefaultConfig(),
		Heartbeat:              heartbeat.DefaultConfig(),
		Libvirt:                libvirt.DefaultConfig(),
		SupportedInstanceTypes: []string{"vm", "container", "microvm"},
	}
}

// Agent is the hypervisor node agent.
type Agent struct {
	config Config
	logger *zap.Logger

	// Cluster components
	etcdClient       *etcd.Client
	nodeRegistry     *registry.EtcdRegistry
	heartbeatService *heartbeat.HeartbeatService

	// Node information
	nodeID string
	node   *registry.Node

	// Compute drivers
	drivers map[driver.InstanceType]driver.Driver

	// gRPC connection to server
	serverConn *grpc.ClientConn

	// Instance tracking
	instances   map[string]*driver.Instance
	instancesMu sync.RWMutex

	mu      sync.RWMutex
	running bool
	stopCh  chan struct{}
}

// New creates a new hypervisor agent.
func New(config Config, logger *zap.Logger) (*Agent, error) {
	if logger == nil {
		logger = zap.NewNop()
	}

	// Connect to etcd
	etcdClient, err := etcd.New(config.Etcd, logger.Named("etcd"))
	if err != nil {
		return nil, fmt.Errorf("failed to connect to etcd: %w", err)
	}

	// Create registry
	reg := registry.NewEtcdRegistry(etcdClient, logger.Named("registry"))

	// Initialize compute drivers
	drivers := make(map[driver.InstanceType]driver.Driver)

	// Initialize libvirt driver if supported
	for _, t := range config.SupportedInstanceTypes {
		if t == "vm" {
			lvDriver, err := libvirt.New(config.Libvirt, logger.Named("libvirt"))
			if err != nil {
				logger.Warn("failed to initialize libvirt driver", zap.Error(err))
			} else {
				drivers[driver.InstanceTypeVM] = lvDriver
			}
		}
		// TODO: Initialize containerd and firecracker drivers
	}

	a := &Agent{
		config:       config,
		logger:       logger,
		etcdClient:   etcdClient,
		nodeRegistry: reg,
		drivers:      drivers,
		instances:    make(map[string]*driver.Instance),
		stopCh:       make(chan struct{}),
	}

	return a, nil
}

// Start starts the agent.
func (a *Agent) Start(ctx context.Context) error {
	a.mu.Lock()
	if a.running {
		a.mu.Unlock()
		return nil
	}
	a.running = true
	a.mu.Unlock()

	// Get host resources
	resources, err := a.getHostResources()
	if err != nil {
		return fmt.Errorf("failed to get host resources: %w", err)
	}

	// Build node information
	supportedTypes := make([]registry.InstanceType, 0, len(a.config.SupportedInstanceTypes))
	for _, t := range a.config.SupportedInstanceTypes {
		supportedTypes = append(supportedTypes, registry.InstanceType(t))
	}

	node := &registry.Node{
		ID:                     a.config.NodeID,
		Hostname:               a.config.Hostname,
		IP:                     a.config.IP,
		Port:                   a.config.Port,
		Role:                   registry.NodeRole(a.config.Role),
		Status:                 registry.NodeStatusReady,
		Region:                 a.config.Region,
		Zone:                   a.config.Zone,
		Capacity:               resources,
		Allocatable:            resources,
		Labels:                 a.config.Labels,
		SupportedInstanceTypes: supportedTypes,
		Conditions: []registry.NodeCondition{
			{
				Type:               registry.ConditionReady,
				Status:             registry.ConditionTrue,
				Reason:             "AgentStarted",
				Message:            "Agent has started successfully",
				LastTransitionTime: time.Now(),
			},
		},
	}

	// Register node
	nodeID, err := a.nodeRegistry.Register(ctx, node)
	if err != nil {
		return fmt.Errorf("failed to register node: %w", err)
	}

	a.nodeID = nodeID
	a.node = node

	a.logger.Info("node registered",
		zap.String("node_id", nodeID),
		zap.String("hostname", a.config.Hostname),
		zap.String("role", a.config.Role),
	)

	// Start heartbeat service
	a.heartbeatService = heartbeat.NewHeartbeatService(
		a.etcdClient,
		a.nodeRegistry,
		nodeID,
		a.config.Heartbeat,
		a.logger.Named("heartbeat"),
	)

	if err := a.heartbeatService.Start(ctx); err != nil {
		return fmt.Errorf("failed to start heartbeat service: %w", err)
	}

	// Connect to server
	if a.config.ServerAddr != "" {
		conn, err := grpc.Dial(
			a.config.ServerAddr,
			grpc.WithTransportCredentials(insecure.NewCredentials()),
		)
		if err != nil {
			a.logger.Warn("failed to connect to server", zap.Error(err))
		} else {
			a.serverConn = conn
		}
	}

	// Start background tasks
	go a.runReconcileLoop(ctx)
	go a.runResourceCollector(ctx)

	a.logger.Info("agent started")
	return nil
}

// Stop stops the agent.
func (a *Agent) Stop() error {
	a.mu.Lock()
	defer a.mu.Unlock()

	if !a.running {
		return nil
	}

	a.running = false
	close(a.stopCh)

	// Stop heartbeat service
	if a.heartbeatService != nil {
		a.heartbeatService.Stop()
	}

	// Deregister node
	if a.nodeID != "" {
		ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
		defer cancel()
		if err := a.nodeRegistry.Deregister(ctx, a.nodeID); err != nil {
			a.logger.Warn("failed to deregister node", zap.Error(err))
		}
	}

	// Close server connection
	if a.serverConn != nil {
		a.serverConn.Close()
	}

	// Close drivers
	for _, d := range a.drivers {
		d.Close()
	}

	// Close etcd client
	a.etcdClient.Close()

	a.logger.Info("agent stopped")
	return nil
}

// getHostResources collects host resource information.
func (a *Agent) getHostResources() (registry.Resources, error) {
	// Try to get resources from libvirt driver
	if lvDriver, ok := a.drivers[driver.InstanceTypeVM]; ok {
		if hostDriver, ok := lvDriver.(driver.HostDriver); ok {
			ctx := context.Background()
			info, err := hostDriver.GetHostInfo(ctx)
			if err == nil {
				return registry.Resources{
					CPUCores:    info.CPUCores,
					MemoryBytes: info.MemoryBytes,
					// Disk would need to be collected separately
				}, nil
			}
		}
	}

	// Fallback to defaults
	return registry.Resources{
		CPUCores:    4,
		MemoryBytes: 8 * 1024 * 1024 * 1024,   // 8GB
		DiskBytes:   100 * 1024 * 1024 * 1024, // 100GB
	}, nil
}

// runReconcileLoop periodically reconciles instance state.
func (a *Agent) runReconcileLoop(ctx context.Context) {
	ticker := time.NewTicker(30 * time.Second)
	defer ticker.Stop()

	for {
		select {
		case <-ctx.Done():
			return
		case <-a.stopCh:
			return
		case <-ticker.C:
			a.reconcileInstances(ctx)
		}
	}
}

// reconcileInstances checks and updates instance states.
func (a *Agent) reconcileInstances(ctx context.Context) {
	a.instancesMu.Lock()
	defer a.instancesMu.Unlock()

	for _, d := range a.drivers {
		instances, err := d.List(ctx)
		if err != nil {
			a.logger.Warn("failed to list instances", zap.Error(err))
			continue
		}

		for _, instance := range instances {
			// Update local cache
			a.instances[instance.ID] = instance
		}
	}
}

// runResourceCollector periodically collects and reports resource usage.
func (a *Agent) runResourceCollector(ctx context.Context) {
	ticker := time.NewTicker(10 * time.Second)
	defer ticker.Stop()

	for {
		select {
		case <-ctx.Done():
			return
		case <-a.stopCh:
			return
		case <-ticker.C:
			a.collectAndReportResources(ctx)
		}
	}
}

// collectAndReportResources collects resource usage and updates node status.
func (a *Agent) collectAndReportResources(ctx context.Context) {
	if a.node == nil {
		return
	}

	// Calculate allocated resources from running instances
	var allocated registry.Resources

	a.instancesMu.RLock()
	for _, instance := range a.instances {
		if instance.State == driver.StateRunning {
			allocated.CPUCores += instance.Spec.CPUCores
			allocated.MemoryBytes += instance.Spec.MemoryMB * 1024 * 1024
		}
	}
	a.instancesMu.RUnlock()

	// Update node status
	a.node.Allocated = allocated
	a.node.LastSeen = time.Now()

	if err := a.nodeRegistry.Update(ctx, a.node); err != nil {
		a.logger.Warn("failed to update node status", zap.Error(err))
	}
}

// CreateInstance creates an instance on this node.
func (a *Agent) CreateInstance(ctx context.Context, spec *driver.InstanceSpec, instanceType driver.InstanceType) (*driver.Instance, error) {
	d, ok := a.drivers[instanceType]
	if !ok {
		return nil, fmt.Errorf("unsupported instance type: %s", instanceType)
	}

	instance, err := d.Create(ctx, spec)
	if err != nil {
		return nil, err
	}

	a.instancesMu.Lock()
	a.instances[instance.ID] = instance
	a.instancesMu.Unlock()

	return instance, nil
}

// StartInstance starts an instance.
func (a *Agent) StartInstance(ctx context.Context, id string) error {
	instance, err := a.getInstance(id)
	if err != nil {
		return err
	}

	d, ok := a.drivers[instance.Type]
	if !ok {
		return fmt.Errorf("unsupported instance type: %s", instance.Type)
	}

	return d.Start(ctx, id)
}

// StopInstance stops an instance.
func (a *Agent) StopInstance(ctx context.Context, id string, force bool) error {
	instance, err := a.getInstance(id)
	if err != nil {
		return err
	}

	d, ok := a.drivers[instance.Type]
	if !ok {
		return fmt.Errorf("unsupported instance type: %s", instance.Type)
	}

	return d.Stop(ctx, id, force)
}

// DeleteInstance deletes an instance.
func (a *Agent) DeleteInstance(ctx context.Context, id string) error {
	instance, err := a.getInstance(id)
	if err != nil {
		return err
	}

	d, ok := a.drivers[instance.Type]
	if !ok {
		return fmt.Errorf("unsupported instance type: %s", instance.Type)
	}

	if err := d.Delete(ctx, id); err != nil {
		return err
	}

	a.instancesMu.Lock()
	delete(a.instances, id)
	a.instancesMu.Unlock()

	return nil
}

// GetInstance retrieves an instance.
func (a *Agent) GetInstance(ctx context.Context, id string) (*driver.Instance, error) {
	return a.getInstance(id)
}

// ListInstances lists all instances on this node.
func (a *Agent) ListInstances(ctx context.Context) ([]*driver.Instance, error) {
	a.instancesMu.RLock()
	defer a.instancesMu.RUnlock()

	instances := make([]*driver.Instance, 0, len(a.instances))
	for _, instance := range a.instances {
		instances = append(instances, instance)
	}

	return instances, nil
}

func (a *Agent) getInstance(id string) (*driver.Instance, error) {
	a.instancesMu.RLock()
	defer a.instancesMu.RUnlock()

	instance, ok := a.instances[id]
	if !ok {
		return nil, driver.ErrInstanceNotFound
	}

	return instance, nil
}
