// Package firecracker provides a compute driver implementation using Firecracker microVMs.
package firecracker

import (
	"context"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"sync"
	"time"

	"hypervisor/pkg/compute/driver"

	firecracker "github.com/firecracker-microvm/firecracker-go-sdk"
	"github.com/firecracker-microvm/firecracker-go-sdk/client/models"
	"github.com/google/uuid"
	"go.uber.org/zap"
)

// Config holds the Firecracker driver configuration.
type Config struct {
	// BinaryPath is the path to the Firecracker binary.
	BinaryPath string `mapstructure:"binary_path"`

	// KernelPath is the default kernel image path.
	KernelPath string `mapstructure:"kernel_path"`

	// RootDrivePath is the path where root drives are stored.
	RootDrivePath string `mapstructure:"root_drive_path"`

	// SocketPath is the base path for VM sockets.
	SocketPath string `mapstructure:"socket_path"`

	// LogPath is the path for VM logs.
	LogPath string `mapstructure:"log_path"`

	// DefaultVCPUs is the default number of vCPUs.
	DefaultVCPUs int64 `mapstructure:"default_vcpus"`

	// DefaultMemoryMB is the default memory in MB.
	DefaultMemoryMB int64 `mapstructure:"default_memory_mb"`
}

// DefaultConfig returns the default Firecracker configuration.
func DefaultConfig() Config {
	return Config{
		BinaryPath:      "/usr/bin/firecracker",
		KernelPath:      "/var/lib/hypervisor/kernels/vmlinux",
		RootDrivePath:   "/var/lib/hypervisor/rootfs",
		SocketPath:      "/var/run/hypervisor/firecracker",
		LogPath:         "/var/log/hypervisor/firecracker",
		DefaultVCPUs:    1,
		DefaultMemoryMB: 512,
	}
}

// VMInstance represents a running Firecracker VM.
type VMInstance struct {
	ID        string
	Machine   *firecracker.Machine
	Spec      driver.InstanceSpec
	CreatedAt time.Time
	StartedAt *time.Time
}

// Driver implements the compute driver interface using Firecracker.
type Driver struct {
	config Config
	logger *zap.Logger

	mu        sync.RWMutex
	instances map[string]*VMInstance
}

// New creates a new Firecracker driver.
func New(config Config, logger *zap.Logger) (*Driver, error) {
	if logger == nil {
		logger = zap.NewNop()
	}

	// Verify Firecracker binary exists
	if _, err := os.Stat(config.BinaryPath); err != nil {
		return nil, fmt.Errorf("firecracker binary not found at %s: %w", config.BinaryPath, err)
	}

	// Create directories if they don't exist
	for _, dir := range []string{config.SocketPath, config.LogPath} {
		if err := os.MkdirAll(dir, 0755); err != nil {
			return nil, fmt.Errorf("failed to create directory %s: %w", dir, err)
		}
	}

	d := &Driver{
		config:    config,
		logger:    logger,
		instances: make(map[string]*VMInstance),
	}

	logger.Info("firecracker driver initialized",
		zap.String("binary", config.BinaryPath),
	)

	return d, nil
}

// Name returns the name of the driver.
func (d *Driver) Name() string {
	return "firecracker"
}

// Type returns the instance type this driver handles.
func (d *Driver) Type() driver.InstanceType {
	return driver.InstanceTypeMicroVM
}

// Create creates a new Firecracker microVM.
func (d *Driver) Create(ctx context.Context, spec *driver.InstanceSpec) (*driver.Instance, error) {
	d.mu.Lock()
	defer d.mu.Unlock()

	// Generate VM ID
	vmID := uuid.New().String()

	// Determine resources
	vcpus := int64(spec.CPUCores)
	if vcpus == 0 {
		vcpus = d.config.DefaultVCPUs
	}

	memMB := spec.MemoryMB
	if memMB == 0 {
		memMB = d.config.DefaultMemoryMB
	}

	// Determine kernel and rootfs paths
	kernelPath := spec.Kernel
	if kernelPath == "" {
		kernelPath = d.config.KernelPath
	}

	rootfsPath := spec.Image
	if rootfsPath == "" {
		return nil, driver.ErrInvalidSpec
	}

	// Socket and log paths
	socketPath := filepath.Join(d.config.SocketPath, vmID+".sock")
	logPath := filepath.Join(d.config.LogPath, vmID+".log")

	// Create log file
	logFile, err := os.Create(logPath)
	if err != nil {
		return nil, fmt.Errorf("failed to create log file: %w", err)
	}

	// Build Firecracker configuration
	fcCfg := firecracker.Config{
		SocketPath:      socketPath,
		KernelImagePath: kernelPath,
		KernelArgs:      spec.KernelArgs,
		Drives: []models.Drive{
			{
				DriveID:      firecracker.String("rootfs"),
				PathOnHost:   firecracker.String(rootfsPath),
				IsRootDevice: firecracker.Bool(true),
				IsReadOnly:   firecracker.Bool(false),
			},
		},
		MachineCfg: models.MachineConfiguration{
			VcpuCount:  firecracker.Int64(vcpus),
			MemSizeMib: firecracker.Int64(memMB),
			Smt:        firecracker.Bool(false),
		},
		LogPath:  logPath,
		LogLevel: "Warning",
	}

	// Add network interface if specified
	if spec.Network.NetworkID != "" {
		// For simplicity, we'll use a TAP device
		// In production, this would integrate with the networking layer
		fcCfg.NetworkInterfaces = []firecracker.NetworkInterface{
			{
				StaticConfiguration: &firecracker.StaticNetworkConfiguration{
					MacAddress:  spec.Network.MACAddress,
					HostDevName: spec.Network.NetworkID,
				},
			},
		}
	}

	// Set default kernel args if not provided
	if fcCfg.KernelArgs == "" {
		fcCfg.KernelArgs = "console=ttyS0 reboot=k panic=1 pci=off"
	}

	// Create the machine
	cmd := firecracker.VMCommandBuilder{}.
		WithBin(d.config.BinaryPath).
		WithSocketPath(socketPath).
		WithStdout(logFile).
		WithStderr(logFile).
		Build(ctx)

	machineOpts := []firecracker.Opt{
		firecracker.WithProcessRunner(cmd),
	}

	machine, err := firecracker.NewMachine(ctx, fcCfg, machineOpts...)
	if err != nil {
		logFile.Close()
		return nil, fmt.Errorf("failed to create machine: %w", err)
	}

	now := time.Now()
	vmInstance := &VMInstance{
		ID:        vmID,
		Machine:   machine,
		Spec:      *spec,
		CreatedAt: now,
	}

	d.instances[vmID] = vmInstance

	instance := &driver.Instance{
		ID:        vmID,
		Name:      vmID,
		Type:      driver.InstanceTypeMicroVM,
		State:     driver.StateStopped,
		CreatedAt: now,
		Spec:      *spec,
	}

	d.logger.Info("microVM created",
		zap.String("id", vmID),
		zap.Int64("vcpus", vcpus),
		zap.Int64("memory_mb", memMB),
	)

	return instance, nil
}

// Start starts a stopped microVM.
func (d *Driver) Start(ctx context.Context, id string) error {
	d.mu.Lock()
	defer d.mu.Unlock()

	vmInstance, ok := d.instances[id]
	if !ok {
		return driver.ErrInstanceNotFound
	}

	if err := vmInstance.Machine.Start(ctx); err != nil {
		return fmt.Errorf("failed to start machine: %w", err)
	}

	now := time.Now()
	vmInstance.StartedAt = &now

	d.logger.Info("microVM started", zap.String("id", id))
	return nil
}

// Stop stops a running microVM.
func (d *Driver) Stop(ctx context.Context, id string, force bool) error {
	d.mu.Lock()
	defer d.mu.Unlock()

	vmInstance, ok := d.instances[id]
	if !ok {
		return driver.ErrInstanceNotFound
	}

	if force {
		if err := vmInstance.Machine.StopVMM(); err != nil {
			return fmt.Errorf("failed to stop VMM: %w", err)
		}
	} else {
		if err := vmInstance.Machine.Shutdown(ctx); err != nil {
			return fmt.Errorf("failed to shutdown machine: %w", err)
		}
	}

	vmInstance.StartedAt = nil

	d.logger.Info("microVM stopped", zap.String("id", id), zap.Bool("force", force))
	return nil
}

// Delete deletes a microVM.
func (d *Driver) Delete(ctx context.Context, id string) error {
	d.mu.Lock()
	defer d.mu.Unlock()

	vmInstance, ok := d.instances[id]
	if !ok {
		return driver.ErrInstanceNotFound
	}

	// Stop if running
	if vmInstance.StartedAt != nil {
		vmInstance.Machine.StopVMM()
	}

	// Clean up socket file
	socketPath := filepath.Join(d.config.SocketPath, id+".sock")
	os.Remove(socketPath)

	delete(d.instances, id)

	d.logger.Info("microVM deleted", zap.String("id", id))
	return nil
}

// Get retrieves a microVM by ID.
func (d *Driver) Get(ctx context.Context, id string) (*driver.Instance, error) {
	d.mu.RLock()
	defer d.mu.RUnlock()

	vmInstance, ok := d.instances[id]
	if !ok {
		return nil, driver.ErrInstanceNotFound
	}

	state := driver.StateStopped
	if vmInstance.StartedAt != nil {
		state = driver.StateRunning
	}

	return &driver.Instance{
		ID:        vmInstance.ID,
		Name:      vmInstance.ID,
		Type:      driver.InstanceTypeMicroVM,
		State:     state,
		CreatedAt: vmInstance.CreatedAt,
		StartedAt: vmInstance.StartedAt,
		Spec:      vmInstance.Spec,
	}, nil
}

// List lists all microVMs.
func (d *Driver) List(ctx context.Context) ([]*driver.Instance, error) {
	d.mu.RLock()
	defer d.mu.RUnlock()

	instances := make([]*driver.Instance, 0, len(d.instances))
	for _, vmInstance := range d.instances {
		state := driver.StateStopped
		if vmInstance.StartedAt != nil {
			state = driver.StateRunning
		}

		instances = append(instances, &driver.Instance{
			ID:        vmInstance.ID,
			Name:      vmInstance.ID,
			Type:      driver.InstanceTypeMicroVM,
			State:     state,
			CreatedAt: vmInstance.CreatedAt,
			StartedAt: vmInstance.StartedAt,
			Spec:      vmInstance.Spec,
		})
	}

	return instances, nil
}

// Stats returns runtime statistics for a microVM.
func (d *Driver) Stats(ctx context.Context, id string) (*driver.InstanceStats, error) {
	d.mu.RLock()
	defer d.mu.RUnlock()

	_, ok := d.instances[id]
	if !ok {
		return nil, driver.ErrInstanceNotFound
	}

	// Firecracker doesn't expose metrics directly through the Go SDK
	// In production, you would get metrics from the metrics fifo
	return &driver.InstanceStats{
		InstanceID:  id,
		CollectedAt: time.Now(),
	}, nil
}

// Attach attaches to a microVM's serial console.
func (d *Driver) Attach(ctx context.Context, id string, opts driver.AttachOptions) (io.ReadWriteCloser, error) {
	// Firecracker serial console access would require connecting to the PTY
	return nil, driver.ErrNotSupported
}

// Restart restarts a microVM.
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

	// Stop all running VMs
	for id, vmInstance := range d.instances {
		if vmInstance.StartedAt != nil {
			if err := vmInstance.Machine.StopVMM(); err != nil {
				d.logger.Warn("failed to stop VM", zap.String("id", id), zap.Error(err))
			}
		}
	}

	d.instances = make(map[string]*VMInstance)
	d.logger.Info("firecracker driver closed")
	return nil
}
