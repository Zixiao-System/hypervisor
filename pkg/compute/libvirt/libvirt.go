//go:build libvirt
// +build libvirt

// Package libvirt provides a compute driver implementation using libvirt.
package libvirt

/*
#cgo CFLAGS: -I${SRCDIR}/../../../clib/libvirt-wrapper
#cgo LDFLAGS: -L${SRCDIR}/../../../clib/libvirt-wrapper -lvirt

#include "libvirt_wrapper.h"
#include <stdlib.h>
*/
import "C"

import (
	"context"
	"fmt"
	"io"
	"sync"
	"time"
	"unsafe"

	"hypervisor/pkg/compute/driver"

	"go.uber.org/zap"
)

// Config holds the libvirt driver configuration.
type Config struct {
	// URI is the libvirt connection URI.
	// Examples:
	//   - "qemu:///system" for local system QEMU
	//   - "qemu:///session" for local user QEMU
	//   - "qemu+ssh://user@host/system" for remote
	URI string `mapstructure:"uri"`

	// DefaultNetwork is the default network for VMs.
	DefaultNetwork string `mapstructure:"default_network"`

	// DefaultStoragePool is the default storage pool for VM disks.
	DefaultStoragePool string `mapstructure:"default_storage_pool"`

	// ImagePath is the path where VM images are stored.
	ImagePath string `mapstructure:"image_path"`
}

// DefaultConfig returns the default libvirt configuration.
func DefaultConfig() Config {
	return Config{
		URI:                "qemu:///system",
		DefaultNetwork:     "default",
		DefaultStoragePool: "default",
		ImagePath:          "/var/lib/hypervisor/images",
	}
}

// Driver implements the compute driver interface using libvirt.
type Driver struct {
	config    Config
	logger    *zap.Logger
	mu        sync.RWMutex
	connected bool
}

// New creates a new libvirt driver.
func New(config Config, logger *zap.Logger) (*Driver, error) {
	if logger == nil {
		logger = zap.NewNop()
	}

	d := &Driver{
		config: config,
		logger: logger,
	}

	// Connect to libvirt
	if err := d.connect(); err != nil {
		return nil, err
	}

	return d, nil
}

func (d *Driver) connect() error {
	d.mu.Lock()
	defer d.mu.Unlock()

	if d.connected {
		return nil
	}

	var uri *C.char
	if d.config.URI != "" {
		uri = C.CString(d.config.URI)
		defer C.free(unsafe.Pointer(uri))
	}

	ret := C.lv_connect(uri)
	if ret != C.LV_OK {
		return fmt.Errorf("failed to connect to libvirt: %s", d.getLastError())
	}

	d.connected = true
	d.logger.Info("connected to libvirt", zap.String("uri", d.config.URI))
	return nil
}

func (d *Driver) getLastError() string {
	return C.GoString(C.lv_get_last_error())
}

// Name returns the name of the driver.
func (d *Driver) Name() string {
	return "libvirt"
}

// Type returns the instance type this driver handles.
func (d *Driver) Type() driver.InstanceType {
	return driver.InstanceTypeVM
}

// Create creates a new VM.
func (d *Driver) Create(ctx context.Context, spec *driver.InstanceSpec) (*driver.Instance, error) {
	d.mu.Lock()
	defer d.mu.Unlock()

	if !d.connected {
		return nil, driver.ErrNotConnected
	}

	// Generate VM XML
	xml := d.generateDomainXML(spec)

	cXML := C.CString(xml)
	defer C.free(unsafe.Pointer(cXML))

	// Define the domain (persistent)
	ret := C.lv_domain_define(cXML)
	if ret != C.LV_OK {
		return nil, fmt.Errorf("failed to define domain: %s", d.getLastError())
	}

	// Get domain info
	name := spec.Image // Using image name as domain name for now
	instance, err := d.getDomainInfo(name)
	if err != nil {
		return nil, err
	}

	d.logger.Info("VM created", zap.String("name", name))
	return instance, nil
}

// Start starts a stopped VM.
func (d *Driver) Start(ctx context.Context, id string) error {
	d.mu.Lock()
	defer d.mu.Unlock()

	if !d.connected {
		return driver.ErrNotConnected
	}

	cName := C.CString(id)
	defer C.free(unsafe.Pointer(cName))

	ret := C.lv_domain_start(cName)
	if ret != C.LV_OK {
		return fmt.Errorf("failed to start domain: %s", d.getLastError())
	}

	d.logger.Info("VM started", zap.String("id", id))
	return nil
}

// Stop stops a running VM.
func (d *Driver) Stop(ctx context.Context, id string, force bool) error {
	d.mu.Lock()
	defer d.mu.Unlock()

	if !d.connected {
		return driver.ErrNotConnected
	}

	cName := C.CString(id)
	defer C.free(unsafe.Pointer(cName))

	var ret C.int
	if force {
		ret = C.lv_domain_destroy(cName)
	} else {
		ret = C.lv_domain_shutdown(cName)
	}

	if ret != C.LV_OK {
		return fmt.Errorf("failed to stop domain: %s", d.getLastError())
	}

	d.logger.Info("VM stopped", zap.String("id", id), zap.Bool("force", force))
	return nil
}

// Delete deletes a VM.
func (d *Driver) Delete(ctx context.Context, id string) error {
	d.mu.Lock()
	defer d.mu.Unlock()

	if !d.connected {
		return driver.ErrNotConnected
	}

	cName := C.CString(id)
	defer C.free(unsafe.Pointer(cName))

	// First, try to destroy if running
	state := C.lv_domain_get_state(cName)
	if state == C.LV_DOMAIN_RUNNING {
		C.lv_domain_destroy(cName)
	}

	// Undefine the domain
	ret := C.lv_domain_undefine(cName)
	if ret != C.LV_OK {
		return fmt.Errorf("failed to undefine domain: %s", d.getLastError())
	}

	d.logger.Info("VM deleted", zap.String("id", id))
	return nil
}

// Get retrieves a VM by ID.
func (d *Driver) Get(ctx context.Context, id string) (*driver.Instance, error) {
	d.mu.RLock()
	defer d.mu.RUnlock()

	if !d.connected {
		return nil, driver.ErrNotConnected
	}

	return d.getDomainInfo(id)
}

func (d *Driver) getDomainInfo(name string) (*driver.Instance, error) {
	cName := C.CString(name)
	defer C.free(unsafe.Pointer(cName))

	var info C.lv_domain_info_t
	ret := C.lv_domain_get_info(cName, &info)
	if ret != C.LV_OK {
		if ret == C.LV_ERR_NOT_FOUND {
			return nil, driver.ErrInstanceNotFound
		}
		return nil, fmt.Errorf("failed to get domain info: %s", d.getLastError())
	}
	defer C.lv_free_domain_info(&info)

	instance := &driver.Instance{
		ID:        C.GoString(info.uuid),
		Name:      C.GoString(info.name),
		Type:      driver.InstanceTypeVM,
		State:     d.mapState(int(info.state)),
		CreatedAt: time.Now(), // libvirt doesn't track creation time
		Spec: driver.InstanceSpec{
			CPUCores: int(info.vcpus),
			MemoryMB: int64(info.memory_kb) / 1024,
		},
	}

	return instance, nil
}

// List lists all VMs.
func (d *Driver) List(ctx context.Context) ([]*driver.Instance, error) {
	d.mu.RLock()
	defer d.mu.RUnlock()

	if !d.connected {
		return nil, driver.ErrNotConnected
	}

	var names **C.char
	var count C.int

	ret := C.lv_domain_list(&names, &count)
	if ret != C.LV_OK {
		return nil, fmt.Errorf("failed to list domains: %s", d.getLastError())
	}

	if count == 0 {
		return []*driver.Instance{}, nil
	}

	defer C.lv_free_string_list(names, count)

	// Convert C array to Go slice
	nameSlice := (*[1 << 30]*C.char)(unsafe.Pointer(names))[:count:count]

	instances := make([]*driver.Instance, 0, int(count))
	for i := 0; i < int(count); i++ {
		name := C.GoString(nameSlice[i])
		instance, err := d.getDomainInfo(name)
		if err != nil {
			d.logger.Warn("failed to get domain info", zap.String("name", name), zap.Error(err))
			continue
		}
		instances = append(instances, instance)
	}

	return instances, nil
}

// Stats returns runtime statistics for a VM.
func (d *Driver) Stats(ctx context.Context, id string) (*driver.InstanceStats, error) {
	d.mu.RLock()
	defer d.mu.RUnlock()

	if !d.connected {
		return nil, driver.ErrNotConnected
	}

	cName := C.CString(id)
	defer C.free(unsafe.Pointer(cName))

	var stats C.lv_domain_stats_t
	ret := C.lv_domain_get_stats(cName, &stats)
	if ret != C.LV_OK {
		return nil, fmt.Errorf("failed to get domain stats: %s", d.getLastError())
	}

	return &driver.InstanceStats{
		InstanceID:      id,
		CPUTimeNs:       uint64(stats.cpu_time_ns),
		MemoryUsedBytes: uint64(stats.memory_used_kb) * 1024,
		DiskReadBytes:   uint64(stats.disk_read_bytes),
		DiskWriteBytes:  uint64(stats.disk_write_bytes),
		NetworkRxBytes:  uint64(stats.net_rx_bytes),
		NetworkTxBytes:  uint64(stats.net_tx_bytes),
		CollectedAt:     time.Now(),
	}, nil
}

// Attach attaches to a VM's console.
func (d *Driver) Attach(ctx context.Context, id string, opts driver.AttachOptions) (io.ReadWriteCloser, error) {
	// libvirt console attachment requires virsh or VNC/SPICE
	// This is a simplified implementation
	return nil, driver.ErrNotSupported
}

// Restart restarts a VM.
func (d *Driver) Restart(ctx context.Context, id string, force bool) error {
	d.mu.Lock()
	defer d.mu.Unlock()

	if !d.connected {
		return driver.ErrNotConnected
	}

	cName := C.CString(id)
	defer C.free(unsafe.Pointer(cName))

	if force {
		// Force restart: destroy then start
		C.lv_domain_destroy(cName)
		ret := C.lv_domain_start(cName)
		if ret != C.LV_OK {
			return fmt.Errorf("failed to start domain: %s", d.getLastError())
		}
	} else {
		// Graceful restart
		ret := C.lv_domain_reboot(cName)
		if ret != C.LV_OK {
			return fmt.Errorf("failed to reboot domain: %s", d.getLastError())
		}
	}

	d.logger.Info("VM restarted", zap.String("id", id), zap.Bool("force", force))
	return nil
}

// Close releases resources and disconnects from libvirt.
func (d *Driver) Close() error {
	d.mu.Lock()
	defer d.mu.Unlock()

	if d.connected {
		C.lv_disconnect()
		d.connected = false
		d.logger.Info("disconnected from libvirt")
	}

	return nil
}

// GetHostInfo returns information about the host.
func (d *Driver) GetHostInfo(ctx context.Context) (*driver.HostInfo, error) {
	d.mu.RLock()
	defer d.mu.RUnlock()

	if !d.connected {
		return nil, driver.ErrNotConnected
	}

	var info C.lv_host_info_t
	ret := C.lv_get_host_info(&info)
	if ret != C.LV_OK {
		return nil, fmt.Errorf("failed to get host info: %s", d.getLastError())
	}
	defer C.lv_free_host_info(&info)

	return &driver.HostInfo{
		Hostname:          C.GoString(info.hostname),
		CPUCores:          int(info.cpus),
		MemoryBytes:       int64(info.memory_kb) * 1024,
		FreeMemoryBytes:   int64(info.free_memory_kb) * 1024,
		HypervisorType:    C.GoString(info.hypervisor_type),
		HypervisorVersion: fmt.Sprintf("%d", info.hypervisor_version),
	}, nil
}

// mapState maps libvirt domain state to driver instance state.
func (d *Driver) mapState(state int) driver.InstanceState {
	switch state {
	case C.LV_DOMAIN_RUNNING:
		return driver.StateRunning
	case C.LV_DOMAIN_BLOCKED:
		return driver.StateRunning
	case C.LV_DOMAIN_PAUSED:
		return driver.StatePaused
	case C.LV_DOMAIN_SHUTDOWN:
		return driver.StateStopped
	case C.LV_DOMAIN_SHUTOFF:
		return driver.StateStopped
	case C.LV_DOMAIN_CRASHED:
		return driver.StateFailed
	default:
		return driver.StateUnknown
	}
}

// generateDomainXML generates libvirt domain XML from spec.
func (d *Driver) generateDomainXML(spec *driver.InstanceSpec) string {
	// This is a simplified XML template
	// Production code should use proper XML templating
	memoryKB := spec.MemoryMB * 1024

	xml := fmt.Sprintf(`<domain type='kvm'>
  <name>%s</name>
  <memory unit='KiB'>%d</memory>
  <vcpu placement='static'>%d</vcpu>
  <os>
    <type arch='x86_64' machine='pc'>hvm</type>
    <boot dev='hd'/>
  </os>
  <features>
    <acpi/>
    <apic/>
  </features>
  <cpu mode='host-model'/>
  <clock offset='utc'>
    <timer name='rtc' tickpolicy='catchup'/>
    <timer name='pit' tickpolicy='delay'/>
    <timer name='hpet' present='no'/>
  </clock>
  <devices>
    <emulator>/usr/bin/qemu-system-x86_64</emulator>
    <disk type='file' device='disk'>
      <driver name='qemu' type='qcow2'/>
      <source file='%s/%s.qcow2'/>
      <target dev='vda' bus='virtio'/>
    </disk>
    <interface type='network'>
      <source network='%s'/>
      <model type='virtio'/>
    </interface>
    <console type='pty'>
      <target type='serial' port='0'/>
    </console>
    <graphics type='vnc' port='-1' autoport='yes' listen='127.0.0.1'>
      <listen type='address' address='127.0.0.1'/>
    </graphics>
  </devices>
</domain>`,
		spec.Image,
		memoryKB,
		spec.CPUCores,
		d.config.ImagePath, spec.Image,
		d.config.DefaultNetwork,
	)

	return xml
}
