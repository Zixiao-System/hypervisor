// Package containerd provides a compute driver implementation using containerd.
package containerd

import (
	"context"
	"fmt"
	"io"
	"sync"
	"syscall"
	"time"

	"hypervisor/pkg/compute/driver"

	"github.com/containerd/containerd"
	"github.com/containerd/containerd/cio"
	"github.com/containerd/containerd/namespaces"
	"github.com/containerd/containerd/oci"
	"github.com/google/uuid"
	"github.com/opencontainers/runtime-spec/specs-go"
	"go.uber.org/zap"
)

// Config holds the containerd driver configuration.
type Config struct {
	// Address is the containerd socket address.
	Address string `mapstructure:"address"`

	// Namespace is the containerd namespace to use.
	Namespace string `mapstructure:"namespace"`

	// Snapshotter is the snapshotter to use.
	Snapshotter string `mapstructure:"snapshotter"`

	// DefaultRuntime is the default runtime to use.
	DefaultRuntime string `mapstructure:"default_runtime"`
}

// DefaultConfig returns the default containerd configuration.
func DefaultConfig() Config {
	return Config{
		Address:        "/run/containerd/containerd.sock",
		Namespace:      "hypervisor",
		Snapshotter:    "overlayfs",
		DefaultRuntime: "io.containerd.runc.v2",
	}
}

// Driver implements the compute driver interface using containerd.
type Driver struct {
	config Config
	logger *zap.Logger
	client *containerd.Client

	mu        sync.RWMutex
	connected bool
}

// New creates a new containerd driver.
func New(config Config, logger *zap.Logger) (*Driver, error) {
	if logger == nil {
		logger = zap.NewNop()
	}

	client, err := containerd.New(config.Address)
	if err != nil {
		return nil, fmt.Errorf("failed to connect to containerd: %w", err)
	}

	d := &Driver{
		config:    config,
		logger:    logger,
		client:    client,
		connected: true,
	}

	logger.Info("connected to containerd", zap.String("address", config.Address))
	return d, nil
}

// getContext returns a context with the containerd namespace.
func (d *Driver) getContext(ctx context.Context) context.Context {
	return namespaces.WithNamespace(ctx, d.config.Namespace)
}

// Name returns the name of the driver.
func (d *Driver) Name() string {
	return "containerd"
}

// Type returns the instance type this driver handles.
func (d *Driver) Type() driver.InstanceType {
	return driver.InstanceTypeContainer
}

// Create creates a new container.
func (d *Driver) Create(ctx context.Context, spec *driver.InstanceSpec) (*driver.Instance, error) {
	d.mu.Lock()
	defer d.mu.Unlock()

	if !d.connected {
		return nil, driver.ErrNotConnected
	}

	ctx = d.getContext(ctx)

	// Pull image if not exists
	image, err := d.client.GetImage(ctx, spec.Image)
	if err != nil {
		d.logger.Info("pulling image", zap.String("image", spec.Image))
		image, err = d.client.Pull(ctx, spec.Image, containerd.WithPullUnpack)
		if err != nil {
			return nil, fmt.Errorf("failed to pull image: %w", err)
		}
	}

	// Generate container ID
	containerID := uuid.New().String()

	// Build container spec
	ociOpts := []oci.SpecOpts{
		oci.WithImageConfig(image),
	}

	// Set command if provided
	if len(spec.Command) > 0 {
		ociOpts = append(ociOpts, oci.WithProcessArgs(append(spec.Command, spec.Args...)...))
	}

	// Set working directory
	if spec.WorkingDir != "" {
		ociOpts = append(ociOpts, oci.WithProcessCwd(spec.WorkingDir))
	}

	// Set environment variables
	if len(spec.Env) > 0 {
		envs := make([]string, 0, len(spec.Env))
		for k, v := range spec.Env {
			envs = append(envs, fmt.Sprintf("%s=%s", k, v))
		}
		ociOpts = append(ociOpts, oci.WithEnv(envs))
	}

	// Set resource limits
	if spec.Limits.MemoryLimit > 0 {
		ociOpts = append(ociOpts, oci.WithMemoryLimit(uint64(spec.Limits.MemoryLimit)))
	}

	if spec.Limits.CPUQuota > 0 && spec.Limits.CPUPeriod > 0 {
		ociOpts = append(ociOpts, withCPULimit(spec.Limits.CPUQuota, spec.Limits.CPUPeriod))
	}

	// Create container
	container, err := d.client.NewContainer(
		ctx,
		containerID,
		containerd.WithImage(image),
		containerd.WithNewSnapshot(containerID+"-snapshot", image),
		containerd.WithNewSpec(ociOpts...),
		containerd.WithRuntime(d.config.DefaultRuntime, nil),
	)
	if err != nil {
		return nil, fmt.Errorf("failed to create container: %w", err)
	}

	now := time.Now()
	instance := &driver.Instance{
		ID:        containerID,
		Name:      containerID,
		Type:      driver.InstanceTypeContainer,
		State:     driver.StateStopped,
		CreatedAt: now,
		Spec:      *spec,
	}

	d.logger.Info("container created",
		zap.String("id", containerID),
		zap.String("image", spec.Image),
	)

	// Clean up container reference (we'll look it up by ID later)
	_ = container

	return instance, nil
}

// Start starts a stopped container.
func (d *Driver) Start(ctx context.Context, id string) error {
	d.mu.Lock()
	defer d.mu.Unlock()

	if !d.connected {
		return driver.ErrNotConnected
	}

	ctx = d.getContext(ctx)

	container, err := d.client.LoadContainer(ctx, id)
	if err != nil {
		return driver.ErrInstanceNotFound
	}

	// Create a new task
	task, err := container.NewTask(ctx, cio.NewCreator(cio.WithStdio))
	if err != nil {
		return fmt.Errorf("failed to create task: %w", err)
	}

	// Start the task
	if err := task.Start(ctx); err != nil {
		task.Delete(ctx)
		return fmt.Errorf("failed to start task: %w", err)
	}

	d.logger.Info("container started", zap.String("id", id))
	return nil
}

// Stop stops a running container.
func (d *Driver) Stop(ctx context.Context, id string, force bool) error {
	d.mu.Lock()
	defer d.mu.Unlock()

	if !d.connected {
		return driver.ErrNotConnected
	}

	ctx = d.getContext(ctx)

	container, err := d.client.LoadContainer(ctx, id)
	if err != nil {
		return driver.ErrInstanceNotFound
	}

	task, err := container.Task(ctx, nil)
	if err != nil {
		// No running task
		return nil
	}

	// Send signal to stop
	var signal syscall.Signal
	if force {
		signal = syscall.SIGKILL
	} else {
		signal = syscall.SIGTERM
	}

	if err := task.Kill(ctx, signal); err != nil {
		return fmt.Errorf("failed to kill task: %w", err)
	}

	// Wait for task to exit
	exitCh, err := task.Wait(ctx)
	if err != nil {
		return fmt.Errorf("failed to wait for task: %w", err)
	}

	select {
	case <-exitCh:
	case <-time.After(30 * time.Second):
		// Force kill if timeout
		task.Kill(ctx, syscall.SIGKILL)
	}

	// Delete the task
	if _, err := task.Delete(ctx); err != nil {
		d.logger.Warn("failed to delete task", zap.Error(err))
	}

	d.logger.Info("container stopped", zap.String("id", id), zap.Bool("force", force))
	return nil
}

// Delete deletes a container.
func (d *Driver) Delete(ctx context.Context, id string) error {
	d.mu.Lock()
	defer d.mu.Unlock()

	if !d.connected {
		return driver.ErrNotConnected
	}

	ctx = d.getContext(ctx)

	container, err := d.client.LoadContainer(ctx, id)
	if err != nil {
		return driver.ErrInstanceNotFound
	}

	// Stop task if running
	task, err := container.Task(ctx, nil)
	if err == nil {
		task.Kill(ctx, syscall.SIGKILL)
		task.Delete(ctx)
	}

	// Delete container
	if err := container.Delete(ctx, containerd.WithSnapshotCleanup); err != nil {
		return fmt.Errorf("failed to delete container: %w", err)
	}

	d.logger.Info("container deleted", zap.String("id", id))
	return nil
}

// Get retrieves a container by ID.
func (d *Driver) Get(ctx context.Context, id string) (*driver.Instance, error) {
	d.mu.RLock()
	defer d.mu.RUnlock()

	if !d.connected {
		return nil, driver.ErrNotConnected
	}

	ctx = d.getContext(ctx)

	container, err := d.client.LoadContainer(ctx, id)
	if err != nil {
		return nil, driver.ErrInstanceNotFound
	}

	return d.containerToInstance(ctx, container)
}

func (d *Driver) containerToInstance(ctx context.Context, container containerd.Container) (*driver.Instance, error) {
	info, err := container.Info(ctx)
	if err != nil {
		return nil, err
	}

	state := driver.StateStopped
	var startedAt *time.Time

	// Check if task is running
	task, err := container.Task(ctx, nil)
	if err == nil {
		status, err := task.Status(ctx)
		if err == nil {
			switch status.Status {
			case containerd.Running:
				state = driver.StateRunning
			case containerd.Paused:
				state = driver.StatePaused
			case containerd.Stopped:
				state = driver.StateStopped
			}
		}
	}

	instance := &driver.Instance{
		ID:        container.ID(),
		Name:      container.ID(),
		Type:      driver.InstanceTypeContainer,
		State:     state,
		CreatedAt: info.CreatedAt,
		StartedAt: startedAt,
	}

	return instance, nil
}

// List lists all containers.
func (d *Driver) List(ctx context.Context) ([]*driver.Instance, error) {
	d.mu.RLock()
	defer d.mu.RUnlock()

	if !d.connected {
		return nil, driver.ErrNotConnected
	}

	ctx = d.getContext(ctx)

	containers, err := d.client.Containers(ctx)
	if err != nil {
		return nil, fmt.Errorf("failed to list containers: %w", err)
	}

	instances := make([]*driver.Instance, 0, len(containers))
	for _, container := range containers {
		instance, err := d.containerToInstance(ctx, container)
		if err != nil {
			d.logger.Warn("failed to get container info", zap.Error(err))
			continue
		}
		instances = append(instances, instance)
	}

	return instances, nil
}

// Stats returns runtime statistics for a container.
func (d *Driver) Stats(ctx context.Context, id string) (*driver.InstanceStats, error) {
	d.mu.RLock()
	defer d.mu.RUnlock()

	if !d.connected {
		return nil, driver.ErrNotConnected
	}

	ctx = d.getContext(ctx)

	container, err := d.client.LoadContainer(ctx, id)
	if err != nil {
		return nil, driver.ErrInstanceNotFound
	}

	task, err := container.Task(ctx, nil)
	if err != nil {
		return nil, fmt.Errorf("no running task: %w", err)
	}

	metrics, err := task.Metrics(ctx)
	if err != nil {
		return nil, fmt.Errorf("failed to get metrics: %w", err)
	}

	// Parse metrics (simplified - actual implementation would parse protobuf)
	_ = metrics

	stats := &driver.InstanceStats{
		InstanceID:  id,
		CollectedAt: time.Now(),
	}

	return stats, nil
}

// Attach attaches to a container's stdio.
func (d *Driver) Attach(ctx context.Context, id string, opts driver.AttachOptions) (io.ReadWriteCloser, error) {
	// Simplified implementation - real implementation would use cio
	return nil, driver.ErrNotSupported
}

// Restart restarts a container.
func (d *Driver) Restart(ctx context.Context, id string, force bool) error {
	if err := d.Stop(ctx, id, force); err != nil {
		return err
	}
	return d.Start(ctx, id)
}

// Close releases resources.
func (d *Driver) Close() error {
	d.mu.Lock()
	defer d.mu.Unlock()

	if d.connected {
		d.client.Close()
		d.connected = false
		d.logger.Info("disconnected from containerd")
	}

	return nil
}

// withCPULimit is a helper to set CPU limits.
func withCPULimit(quota, period int64) oci.SpecOpts {
	return func(_ context.Context, _ oci.Client, _ *containers.Container, s *oci.Spec) error {
		if s.Linux == nil {
			s.Linux = &specs.Linux{}
		}
		if s.Linux.Resources == nil {
			s.Linux.Resources = &specs.LinuxResources{}
		}
		if s.Linux.Resources.CPU == nil {
			s.Linux.Resources.CPU = &specs.LinuxCPU{}
		}
		s.Linux.Resources.CPU.Quota = &quota
		s.Linux.Resources.CPU.Period = uint64Ptr(uint64(period))
		return nil
	}
}

func uint64Ptr(v uint64) *uint64 {
	return &v
}

// Ensure we need this import for oci.Spec
type containers = containerd
