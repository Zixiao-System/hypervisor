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
└─────────────────────────────────────────────────────────────┘
                              │
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
                    │   (Cluster DB)  │
                    └─────────────────┘
```

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
│   ├── proto/           # gRPC protocol definitions
│   └── gen/             # Generated Go code
├── cmd/
│   ├── hypervisor-server/   # Control plane binary
│   ├── hypervisor-agent/    # Node agent binary
│   └── hypervisor-ctl/      # CLI tool
├── pkg/
│   ├── cluster/
│   │   ├── etcd/        # etcd client wrapper
│   │   ├── registry/    # Node registration
│   │   └── heartbeat/   # Health monitoring
│   └── compute/
│       ├── driver/      # Driver interface
│       ├── libvirt/     # KVM/QEMU driver
│       ├── containerd/  # Container driver
│       └── firecracker/ # MicroVM driver
├── internal/
│   ├── server/          # Server implementation
│   └── agent/           # Agent implementation
├── clib/
│   └── libvirt-wrapper/ # C wrapper for libvirt
├── configs/             # Configuration templates
└── deployments/         # Docker & systemd files
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
- `CreateInstance` - Create a new instance
- `DeleteInstance` - Delete an instance
- `StartInstance` / `StopInstance` - Lifecycle management
- `ListInstances` - List instances
- `GetInstanceStats` - Get runtime statistics

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

- [ ] Web UI (MDUI-based management interface)
- [ ] Distributed File System integration
- [ ] SDN/VXLAN networking
- [ ] Live migration support
- [ ] GPU passthrough
- [ ] Multi-tenancy and RBAC

## Contributing

Contributions are welcome! Please read our contributing guidelines before submitting PRs.

## License

Apache License 2.0