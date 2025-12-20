# Zixiao Distributed Cloud Hypervisor

A distributed cloud operating system built with Go and C, supporting VMs (KVM/libvirt), containers (containerd), and microVMs (Firecracker).

[![CI](https://github.com/Zixiao-System/hypervisor/actions/workflows/ci.yml/badge.svg)](https://github.com/Zixiao-System/hypervisor/actions/workflows/ci.yml)
[![Go Report Card](https://goreportcard.com/badge/github.com/Zixiao-System/hypervisor)](https://goreportcard.com/report/github.com/Zixiao-System/hypervisor)
[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE)

## Features

- **Multi-runtime Support**: VMs (KVM/QEMU via libvirt), containers (containerd), and microVMs (Firecracker)
- **Distributed Architecture**: Built for 100+ node clusters with etcd-based coordination
- **High Availability**: Heartbeat monitoring, automatic failover, and node health tracking
- **Flexible Scheduling**: Region/zone-aware scheduling with bin-packing and spread strategies
- **gRPC API**: Full-featured API with streaming support for real-time updates
- **CLI Tool**: Comprehensive command-line interface for cluster management

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    hypervisor-ctl (CLI)                     │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                  hypervisor-server (Control Plane)          │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────┐  │
│  │  Cluster    │  │  Compute    │  │     Scheduler       │  │
│  │  Service    │  │  Service    │  │                     │  │
│  └─────────────┘  └─────────────┘  └─────────────────────┘  │
│         │               │                    │              │
│         └───────────────┼────────────────────┘              │
│                         ▼                                   │
│              ┌─────────────────────┐                        │
│              │  Agent Client Pool  │                        │
│              └─────────────────────┘                        │
└─────────────────────────────────────────────────────────────┘
                              │ gRPC
              ┌───────────────┼───────────────┐
              ▼               ▼               ▼
┌─────────────────┐ ┌─────────────────┐ ┌─────────────────┐
│ hypervisor-agent│ │ hypervisor-agent│ │ hypervisor-agent│
│   (Worker 1)    │ │   (Worker 2)    │ │   (Worker N)    │
│  ┌───────────┐  │ │  ┌───────────┐  │ │  ┌───────────┐  │
│  │  libvirt  │  │ │  │containerd │  │ │  │firecracker│  │
│  │  driver   │  │ │  │  driver   │  │ │  │  driver   │  │
│  └───────────┘  │ │  └───────────┘  │ │  └───────────┘  │
└─────────────────┘ └─────────────────┘ └─────────────────┘
              │               │               │
              └───────────────┼───────────────┘
                              ▼
                    ┌─────────────────┐
                    │      etcd       │
                    │ (Nodes + Instances) │
                    └─────────────────┘
```

### Data Flow

1. **Instance Creation**: CLI/Web UI → Server (scheduling) → Agent (execution) → etcd (persistence)
2. **Instance Operations**: Server → Agent Client Pool → Target Agent → Driver
3. **State Storage**: Nodes and Instances are persisted in etcd with watch support

## Quick Start

### Prerequisites

- Go 1.21+
- etcd 3.5+
- protoc (Protocol Buffers compiler)
- For VM support: libvirt-dev

### Build

```bash
# Clone the repository
git clone https://github.com/Zixiao-System/hypervisor.git
cd hypervisor

# Install dependencies and build
make deps
make proto
make build

# Build with libvirt support (requires libvirt-dev)
make all-with-libvirt
```

### Run

**1. Start etcd**
```bash
docker run -d --name etcd -p 2379:2379 \
  quay.io/coreos/etcd:v3.5.9 \
  /usr/local/bin/etcd \
  --advertise-client-urls=http://0.0.0.0:2379 \
  --listen-client-urls=http://0.0.0.0:2379
```

**2. Start the Control Plane**
```bash
./bin/hypervisor-server --config configs/server.example.yaml
```

**3. Start an Agent (on compute nodes)**
```bash
./bin/hypervisor-agent --config configs/agent.example.yaml
```

**4. Use the CLI**
```bash
# List nodes
./bin/hypervisor-ctl node list

# Create an instance
./bin/hypervisor-ctl instance create --name my-vm --type vm --image ubuntu --cpus 2 --memory 2048

# List instances
./bin/hypervisor-ctl instance list

# Get cluster info
./bin/hypervisor-ctl cluster info
```

## Project Structure

```
hypervisor/
├── api/
│   ├── proto/                    # gRPC protocol definitions
│   │   ├── common.proto          # Shared types
│   │   ├── cluster.proto         # ClusterService definition
│   │   ├── compute.proto         # ComputeService definition
│   │   └── agent.proto           # AgentService (internal) definition
│   └── gen/                      # Generated Go code
├── cmd/
│   ├── hypervisor-server/        # Control plane binary
│   ├── hypervisor-agent/         # Node agent binary
│   └── hypervisor-ctl/           # CLI tool
├── internal/
│   ├── server/                      # Control plane implementation
│   │   ├── server.go                # gRPC server setup
│   │   ├── cluster_service.go       # Node management logic
│   │   ├── cluster_grpc_handler.go  # ClusterService proto adapter
│   │   ├── compute_service.go       # Instance management logic
│   │   ├── compute_grpc_handler.go  # ComputeService proto adapter
│   │   └── agent_client.go          # Agent connection pool
│   └── agent/                       # Node agent implementation
│       ├── agent.go                 # Agent core logic
│       └── grpc_service.go          # AgentService implementation
├── pkg/
│   ├── cluster/
│   │   ├── etcd/                 # etcd client wrapper
│   │   ├── registry/             # Node and Instance registration
│   │   │   ├── registry.go       # Node registry (etcd)
│   │   │   ├── instance.go       # Instance registry (etcd)
│   │   │   └── instance_types.go # Instance type definitions
│   │   └── heartbeat/            # Health monitoring
│   ├── compute/
│   │   ├── driver/               # Driver interface
│   │   ├── libvirt/              # KVM/QEMU driver
│   │   ├── containerd/           # Container driver
│   │   └── firecracker/          # MicroVM driver
│   └── virtual-apps-and-desktop/
│       ├── electron_client/      # VDI desktop client (Electron + Vue)
│       ├── html-web-access/      # Browser-based desktop access
│       └── guest_drivers/        # VDI guest agents (WebRTC streaming)
├── control_and_manage_plane/
│   └── src/                      # Management Web UI (Vue 3 + MDUI)
│       ├── src/
│       │   ├── api/              # gRPC-Web transport and clients
│       │   ├── gen/              # Generated TypeScript protobuf code
│       │   ├── stores/           # Pinia state (cluster, compute, metrics)
│       │   ├── components/       # Reusable components (charts, terminal)
│       │   ├── views/            # Page views (Dashboard, Console, etc.)
│       │   └── styles/           # Theme customization (Zixiao purple)
│       ├── buf.yaml              # Buf protobuf configuration
│       └── buf.gen.yaml          # Buf code generation config
├── clib/
│   ├── libvirt-wrapper/          # C wrapper for libvirt
│   ├── guest-drivers/
│   │   ├── balloon/              # Memory balloon driver
│   │   ├── virtio/               # VirtIO ring implementation
│   │   └── windows/              # Windows VirtIO kernel driver (KMDF)
│   ├── windows-host/             # Windows hypervisor host components
│   ├── ebpf-network-accel/       # eBPF/XDP network acceleration
│   └── ovs-dpdk-network/         # OVS-DPDK integration
├── configs/                      # Configuration templates
└── deployments/                  # Docker & systemd files
```

## Configuration

### Server Configuration

```yaml
# configs/server.yaml
grpc_addr: ":50051"
http_addr: ":8080"

etcd:
  endpoints:
    - "localhost:2379"
  dial_timeout: 5s

heartbeat:
  interval: 10s
  timeout: 30s
```

### Agent Configuration

```yaml
# configs/agent.yaml
hostname: worker-1
port: 50052
role: worker
region: us-west
zone: zone-a
server_addr: "localhost:50051"

supported_instance_types:
  - vm
  - container
  - microvm

libvirt:
  uri: "qemu:///system"
```

## API

The hypervisor exposes a gRPC API with the following services:

### ClusterService
- `RegisterNode` - Register a new node
- `DeregisterNode` - Remove a node
- `ListNodes` - List all nodes
- `WatchNodes` - Stream node events
- `Heartbeat` - Node heartbeat

### ComputeService
- `CreateInstance` - Create a new instance (schedules to agent, persists to etcd)
- `DeleteInstance` - Delete an instance
- `StartInstance` / `StopInstance` / `RestartInstance` - Lifecycle management
- `ListInstances` - List instances with filtering
- `GetInstanceStats` - Get runtime statistics (CPU, memory, disk, network)

### AgentService (Internal)
- Server-to-Agent gRPC communication for instance lifecycle operations
- Console attach via bidirectional streaming

## Development

### Run Tests
```bash
make test
```

### Lint
```bash
make lint
```

### Generate Proto
```bash
make proto
```

### Cross-compile for Linux
```bash
make build-linux
```

## Roadmap

### Phase 1: Core Infrastructure (Completed)
- [x] Distributed cluster architecture with etcd
- [x] Multi-runtime support (VM/Container/MicroVM)
- [x] gRPC API and CLI tools
- [x] Node registration and heartbeat monitoring
- [x] Server-Agent gRPC communication
- [x] Instance etcd persistence with CRUD operations
- [x] Instance scheduling with region/zone awareness

### Phase 2: Web & Desktop Clients (Completed)

#### Management Plane Web UI (`control_and_manage_plane/`)
- [x] Vue 3 + TypeScript + Vite build system
- [x] MDUI 2.x (Material Design 3) component library
- [x] ConnectRPC/gRPC-Web for backend communication
- [x] Pinia state management (cluster, compute, metrics, theme stores)
- [x] Chart.js integration for resource monitoring dashboards
  - ResourceGaugeChart: Doughnut chart for CPU/Memory/Disk usage
  - ResourceTrendChart: Line chart for historical metrics
  - InstanceDistributionChart: Pie chart for VM/Container/MicroVM breakdown
- [x] xterm.js terminal emulator for instance console access (WebSocket)
- [x] Dark/Light theme switching with system preference detection
- [x] Zixiao theme customization (purple primary, gold accent colors)
- [x] Buf-based protobuf code generation for TypeScript
- [x] Runtime statistics display with auto-refresh (5s interval)
- [x] Instance lifecycle management (start/stop/restart/delete)

**Proto Generation:**
```bash
cd control_and_manage_plane/src
npm run proto:gen  # Generates TypeScript code from api/proto/
```

**Icon Usage (MDUI):**
```bash
npm install @mdui/icons --save
```
```js
// Import individual icons for tree-shaking
import '@mdui/icons/dashboard.js'
import '@mdui/icons/dns.js'
import '@mdui/icons/memory.js'
```
```html
<mdui-icon-dashboard></mdui-icon-dashboard>
```

#### Electron VDI Desktop Client (`pkg/virtual-apps-and-desktop/electron_client/`)
- [x] Electron + Vue 3 + MDUI desktop application
- [x] Navigation rail UI pattern for compact layout
- [x] Desktop/Settings/About views with routing
- [x] Native theme integration (light/dark/auto)
- [x] IPC bridge for Electron main/renderer communication
- [x] Cross-platform packaging (Windows/macOS/Linux)

#### HTML5 Web Access (`pkg/virtual-apps-and-desktop/html-web-access/`)
- [x] Browser-based remote desktop access
- [x] noVNC integration for VNC protocol
- [x] SPICE HTML5 client support
- [x] WebSocket proxy for protocol bridging
- [x] Responsive design for mobile/tablet access

#### Envoy gRPC-Web Proxy (`control_and_manage_plane/nginx_web/`)
- [x] gRPC-Web to gRPC protocol translation
- [x] CORS configuration for cross-origin requests
- [x] Routing for ClusterService and ComputeService
- [x] Static file serving for Vue SPA
- [x] Docker Compose deployment configuration

### Phase 3: Network Acceleration (In Progress)
- [x] eBPF/XDP network acceleration library
- [x] OVS-DPDK integration with vhost-user
- [ ] SDN/VXLAN overlay networking
- [ ] Distributed virtual router

### Phase 4: Guest Drivers (Planned)
- [x] VirtIO ring library (virtio_ring.c)
- [x] Memory balloon driver
- [ ] **VDI Guest Agent** (Linux)
  - Display resize and multi-monitor support
  - Clipboard sharing (text/image/file)
  - Audio redirection
  - USB device redirection
  - Seamless window mode
- [ ] **Windows Guest Drivers** (Priority: High)
  - Windows VirtIO drivers (storage, network, balloon)
  - SPICE guest agent for Windows
  - QXL/VirtIO-GPU display driver
  - QEMU Guest Agent (qga) for Windows
  - Signed drivers for Windows 10/11/Server

### Phase 5: Advanced Features (Planned)
- [ ] Live migration support
- [ ] GPU passthrough (NVIDIA/AMD)
- [ ] vGPU support (NVIDIA GRID, Intel GVT-g)
- [ ] SR-IOV network virtualization
- [ ] Distributed File System integration
- [ ] Multi-tenancy and RBAC
- [ ] Kubernetes integration (Virtual Kubelet)

### Phase 6: Enterprise Features (Future)
- [ ] High Availability clustering
- [ ] Disaster Recovery (DR) automation
- [ ] Backup and snapshot management
- [ ] Resource quota and chargeback
- [ ] Audit logging and compliance

## Contributing

Contributions are welcome! Please read our contributing guidelines before submitting PRs.

## License

Apache License 2.0