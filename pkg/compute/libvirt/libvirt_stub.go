//go:build !libvirt
// +build !libvirt

// Package libvirt provides a compute driver implementation using libvirt.
// This is a stub file for when libvirt is not available.
package libvirt

import (
	"context"
	"errors"
	"io"

	"hypervisor/pkg/compute/driver"

	"go.uber.org/zap"
)

var ErrLibvirtNotAvailable = errors.New("libvirt support not compiled in; rebuild with -tags libvirt")

// Config holds the libvirt driver configuration.
type Config struct {
	URI                string `mapstructure:"uri"`
	DefaultNetwork     string `mapstructure:"default_network"`
	DefaultStoragePool string `mapstructure:"default_storage_pool"`
	ImagePath          string `mapstructure:"image_path"`
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

// Driver is a stub that returns an error when libvirt is not available.
type Driver struct{}

// New returns an error because libvirt is not compiled in.
func New(config Config, logger *zap.Logger) (*Driver, error) {
	return nil, ErrLibvirtNotAvailable
}

func (d *Driver) Name() string                                           { return "libvirt" }
func (d *Driver) Type() driver.InstanceType                              { return driver.InstanceTypeVM }
func (d *Driver) Create(ctx context.Context, spec *driver.InstanceSpec) (*driver.Instance, error) {
	return nil, ErrLibvirtNotAvailable
}
func (d *Driver) Start(ctx context.Context, id string) error             { return ErrLibvirtNotAvailable }
func (d *Driver) Stop(ctx context.Context, id string, force bool) error  { return ErrLibvirtNotAvailable }
func (d *Driver) Delete(ctx context.Context, id string) error            { return ErrLibvirtNotAvailable }
func (d *Driver) Get(ctx context.Context, id string) (*driver.Instance, error) {
	return nil, ErrLibvirtNotAvailable
}
func (d *Driver) List(ctx context.Context) ([]*driver.Instance, error) {
	return nil, ErrLibvirtNotAvailable
}
func (d *Driver) Stats(ctx context.Context, id string) (*driver.InstanceStats, error) {
	return nil, ErrLibvirtNotAvailable
}
func (d *Driver) Attach(ctx context.Context, id string, opts driver.AttachOptions) (io.ReadWriteCloser, error) {
	return nil, ErrLibvirtNotAvailable
}
func (d *Driver) Restart(ctx context.Context, id string, force bool) error {
	return ErrLibvirtNotAvailable
}
func (d *Driver) Close() error { return nil }
func (d *Driver) GetHostInfo(ctx context.Context) (*driver.HostInfo, error) {
	return nil, ErrLibvirtNotAvailable
}