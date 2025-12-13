=# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
# Full build (deps, proto, binaries)
make all

# Build with libvirt support (requires libvirt-dev)
make all-with-libvirt

# Individual targets
make build              # Build all three binaries
make build-server       # Build hypervisor-server only
make build-agent        # Build hypervisor-agent only
make build-ctl          # Build hypervisor-ctl only
make proto              # Regenerate protobuf code
make build-linux        # Cross-compile for Linux amd64
```

## Testing and Linting

```bash
make test               # Run all tests with race detection
make test-short         # Run tests without long-running tests
make test-coverage      # Generate coverage report (coverage.html)
make lint               # Run golangci-lint
make fmt                # Format code
```

Run a single test:
```bash
go test -v -run TestName ./path/to/package
```

## Running Locally

Requires etcd running (e.g., `docker run -d -p 2379:2379 quay.io/coreos/etcd:v3.5.9`):

```bash
make run-server         # Start control plane (uses configs/server.yaml)
make run-agent          # Start agent (uses configs/agent.yaml)
```

## Architecture

**Three-tier distributed system:**

1. **hypervisor-server** (Control Plane): Central cluster management, scheduling, API gateway
2. **hypervisor-agent** (Compute Nodes): Per-node agents managing local compute resources
3. **hypervisor-ctl** (CLI): Command-line client for cluster operations

**State coordination:** etcd for distributed state, node registration, and service discovery

**Inter-component communication:** gRPC with Protocol Buffers (`api/proto/` → `api/gen/`)

## Key Packages

- `pkg/cluster/etcd/` - etcd client wrapper with connection pooling
- `pkg/cluster/registry/` - Node registration and discovery
- `pkg/cluster/heartbeat/` - Node health monitoring with configurable TTL
- `pkg/compute/driver/` - Pluggable driver interface for compute runtimes
- `pkg/compute/libvirt/` - KVM/QEMU driver via libvirt (CGO)
- `pkg/compute/containerd/` - Container runtime driver
- `pkg/compute/firecracker/` - MicroVM driver
- `internal/server/` - Server implementation (gRPC services)
- `internal/agent/` - Agent implementation (runtime management)

## Multi-Runtime Driver Pattern

The compute subsystem uses a unified driver interface (`pkg/compute/driver/driver.go`) supporting:
- VMs (libvirt/KVM)
- Containers (containerd)
- MicroVMs (Firecracker)

All drivers implement the same interface with `Instance` and `InstanceSpec` abstractions.

## Frontend Applications

- `control_and_manage_plane/` - Vue 3 + MDUI web management UI (uses ConnectRPC/gRPC-Web)
- `pkg/virtual-apps-and-desktop/electron_client/` - Electron VDI desktop client
- `pkg/virtual-apps-and-desktop/html-web-access/` - HTML5 browser-based desktop access

## C Libraries (clib/)

Used via CGO for performance-critical paths:
- `libvirt-wrapper/` - C wrapper for libvirt API
- `guest-drivers/` - VirtIO ring and memory balloon implementations
- `ebpf-network-accel/` - eBPF/XDP network acceleration
- `ovs-dpdk-network/` - OVS-DPDK integration

## Code Style

- Go imports: local imports use prefix `hypervisor` (configured in `.golangci.yml`)
- Package comments are required (revive linter)
- Context must be first argument in functions
- Use structured logging via `go.uber.org/zap`
- Error strings should not be capitalized or end with punctuation