=# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Current Development Focus

> **Note (Issue #1):** UI/前端功能开发暂缓，等待 @HwlloChen 回归后再处理。当前开发重点：
> - 核心功能实现
> - API 开发与完善
> - 驱动程序开发 (libvirt, containerd, firecracker, VirtIO)
>
> 请勿修改 `control_and_manage_plane/`、`electron_client/`、`html-web-access/` 等前端相关代码。

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
- `pkg/network/sdn/` - SDN controller for network/port/security group management
- `pkg/network/overlay/` - VXLAN overlay with VTEP mesh management
- `pkg/network/ipam/` - IP Address Management (subnet/allocation)
- `pkg/network/router/` - Distributed Virtual Router (DVR) and ARP proxy
- `pkg/network/cgo/` - OVS and eBPF CGO bindings
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

### Management Web UI Structure (`control_and_manage_plane/src/`)

```
src/
├── api/                    # gRPC-Web client layer
│   ├── transport.ts        # gRPC-Web transport configuration
│   ├── clients.ts          # Service client instances (clusterClient, computeClient)
│   └── converters.ts       # Protobuf ↔ TypeScript type converters
├── gen/                    # Generated protobuf TypeScript code (via buf)
│   ├── cluster_pb.ts       # Cluster message types
│   ├── cluster_connect.ts  # ClusterService client
│   ├── compute_pb.ts       # Compute message types
│   ├── compute_connect.ts  # ComputeService client
│   └── common_pb.ts        # Shared enums and types
├── stores/                 # Pinia state management
│   ├── cluster.ts          # Node and cluster state
│   ├── compute.ts          # Instance state
│   ├── metrics.ts          # Real-time metrics history
│   └── theme.ts            # Dark/light theme
├── components/
│   ├── charts/             # Chart.js visualizations
│   │   ├── ResourceGaugeChart.vue      # Doughnut chart (CPU/Memory/Disk)
│   │   ├── ResourceTrendChart.vue      # Line chart (historical metrics)
│   │   └── InstanceDistributionChart.vue # Pie chart (VM/Container/MicroVM)
│   └── TerminalConsole.vue # xterm.js WebSocket terminal
├── views/
│   ├── DashboardView.vue   # Overview with charts
│   ├── ConsoleView.vue     # Full-screen instance console
│   └── ...
├── styles/
│   └── theme.css           # Zixiao theme (purple primary, gold accent)
├── buf.yaml                # Buf configuration
└── buf.gen.yaml            # Buf code generation config
```

**Proto Generation:**
```bash
cd control_and_manage_plane/src
npm run proto:gen           # Generate TypeScript from api/proto/
```

## C Libraries (clib/)

Used via CGO for performance-critical paths:
- `libvirt-wrapper/` - C wrapper for libvirt API
- `guest-drivers/` - VirtIO ring and memory balloon implementations
- `ebpf-network-accel/` - eBPF/XDP network acceleration
- `ovs-dpdk-network/` - OVS-DPDK integration

### Windows Host Components (clib/windows-host/)

- `windows-hypervisor-agent/` - Windows host hypervisor agent (Visual Studio 2026 C++ project, ARM64/x64)

### Windows Guest Drivers (clib/guest-drivers/windows/)

- `Windows-VirtIO-Driver/` - Windows VirtIO kernel driver (KMDF, requires VS2022 + WDK)

## VDI Guest Drivers (pkg/virtual-apps-and-desktop/guest_drivers/)

Guest-side agents for Virtual Desktop Infrastructure:
- `windows/webrtc/Windows-VDI-WebRTC-Agent/` - Windows WebRTC streaming agent for VDI (Visual Studio 2026 C++ project)
- `linux/zixiao-vdi-agent/` - Linux VDI Agent with:
  - Audio capture (PulseAudio)
  - Display capture (X11/XRandR)
  - Clipboard management (X11 selections)
  - Input handling (uinput virtual devices)
  - SPICE agent protocol
  - WebRTC streaming

## Code Style

- Go imports: local imports use prefix `hypervisor` (configured in `.golangci.yml`)
- Package comments are required (revive linter)
- Context must be first argument in functions
- Use structured logging via `go.uber.org/zap`
- Error strings should not be capitalized or end with punctuation