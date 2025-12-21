# Hypervisor 架构设计

本文档详细描述 Hypervisor 项目的系统架构、核心组件、通信协议和设计决策。

## 目录

- [系统概述](#系统概述)
- [架构图](#架构图)
- [核心组件](#核心组件)
- [通信协议](#通信协议)
- [数据模型](#数据模型)
- [多运行时驱动架构](#多运行时驱动架构)
- [状态管理](#状态管理)
- [高可用设计](#高可用设计)
- [安全架构](#安全架构)
- [扩展性设计](#扩展性设计)

## 系统概述

Hypervisor 是一个分布式虚拟化管理平台，采用三层架构设计，支持多种计算运行时（虚拟机、容器、MicroVM）。

### 设计目标

- **统一管理**: 通过单一平台管理 VM、容器和 MicroVM
- **高可用**: 支持控制平面和计算节点的高可用部署
- **可扩展**: 插件化驱动架构，易于扩展新的计算运行时
- **云原生**: 使用 gRPC、etcd 等云原生技术栈

### 技术选型

| 组件 | 技术 | 选择理由 |
|------|------|----------|
| 编程语言 | Go | 高性能、原生并发、静态编译 |
| RPC 框架 | gRPC | 高效二进制协议、强类型、双向流 |
| 状态存储 | etcd | 强一致性、分布式锁、Watch 机制 |
| 日志 | zap | 高性能结构化日志 |
| 配置 | Viper | 多格式支持、环境变量覆盖 |
| 前端 | Vue 3 + TypeScript | 响应式、类型安全、生态丰富 |

## 架构图

### 整体架构

```
                                    ┌─────────────────────────────────────────────────────┐
                                    │                   用户访问层                          │
                                    │                                                     │
                                    │    ┌──────────────┐    ┌──────────────────────┐    │
                                    │    │ hypervisor-  │    │   Web Management UI  │    │
                                    │    │     ctl      │    │   (Vue 3 + MDUI)     │    │
                                    │    │   (CLI)      │    │                      │    │
                                    │    └──────┬───────┘    └──────────┬───────────┘    │
                                    │           │                       │                 │
                                    └───────────┼───────────────────────┼─────────────────┘
                                                │                       │
                                           gRPC │              gRPC-Web │
                                                │                       │
                                    ┌───────────┴───────────────────────┴─────────────────┐
                                    │                    控制平面                           │
                                    │                                                     │
                                    │  ┌─────────────────────────────────────────────┐   │
                                    │  │           hypervisor-server                  │   │
                                    │  │                                              │   │
                                    │  │  ┌────────────────┐  ┌────────────────────┐ │   │
                                    │  │  │ ClusterService │  │  ComputeService    │ │   │
                                    │  │  │  - 节点管理     │  │  - 实例生命周期    │ │   │
                                    │  │  │  - 服务发现     │  │  - 资源调度        │ │   │
                                    │  │  └────────────────┘  └────────────────────┘ │   │
                                    │  │                                              │   │
                                    │  │  ┌────────────────────────────────────────┐ │   │
                                    │  │  │            Agent Client Pool            │ │   │
                                    │  │  │         (管理与 Agent 的连接)            │ │   │
                                    │  │  └────────────────────────────────────────┘ │   │
                                    │  └─────────────────────────────────────────────┘   │
                                    │                         │                          │
                                    └─────────────────────────┼──────────────────────────┘
                                                              │
                                              ┌───────────────┼───────────────┐
                                              │               │               │
                                              ▼               ▼               ▼
                                    ┌─────────────────────────────────────────────────────┐
                                    │                     计算平面                         │
                                    │                                                     │
                                    │  ┌───────────────┐  ┌───────────────┐  ┌─────────┐ │
                                    │  │hypervisor-    │  │hypervisor-    │  │   ...   │ │
                                    │  │   agent       │  │   agent       │  │         │ │
                                    │  │               │  │               │  │         │ │
                                    │  │ ┌───────────┐ │  │ ┌───────────┐ │  │         │ │
                                    │  │ │  Driver   │ │  │ │  Driver   │ │  │         │ │
                                    │  │ │ Manager   │ │  │ │ Manager   │ │  │         │ │
                                    │  │ └───────────┘ │  │ └───────────┘ │  │         │ │
                                    │  │      │        │  │      │        │  │         │ │
                                    │  │ ┌────┴────┐   │  │ ┌────┴────┐   │  │         │ │
                                    │  │ │libvirt  │   │  │ │containerd│  │  │         │ │
                                    │  │ │container│   │  │ │firecracker│ │  │         │ │
                                    │  │ │firecracker│ │  │ │         │   │  │         │ │
                                    │  │ └─────────┘   │  │ └─────────┘   │  │         │ │
                                    │  └───────────────┘  └───────────────┘  └─────────┘ │
                                    │                                                     │
                                    └─────────────────────────────────────────────────────┘
                                                              │
                                                              ▼
                                    ┌─────────────────────────────────────────────────────┐
                                    │                     存储平面                         │
                                    │                                                     │
                                    │         ┌─────────────────────────────┐            │
                                    │         │           etcd              │            │
                                    │         │                             │            │
                                    │         │  - 节点注册信息              │            │
                                    │         │  - 实例状态                  │            │
                                    │         │  - 配置数据                  │            │
                                    │         │  - 分布式锁                  │            │
                                    │         └─────────────────────────────┘            │
                                    │                                                     │
                                    └─────────────────────────────────────────────────────┘
```

### 组件交互流程

```
用户请求实例创建流程:

┌──────┐      ┌──────────────┐      ┌────────────────┐      ┌──────────────┐
│ User │      │hypervisor-   │      │ hypervisor-    │      │ hypervisor-  │
│      │      │   ctl/UI     │      │    server      │      │    agent     │
└──┬───┘      └──────┬───────┘      └───────┬────────┘      └──────┬───────┘
   │                 │                      │                      │
   │ create instance │                      │                      │
   │────────────────▶│                      │                      │
   │                 │                      │                      │
   │                 │ CreateInstance(spec) │                      │
   │                 │─────────────────────▶│                      │
   │                 │                      │                      │
   │                 │                      │ 1. 验证请求           │
   │                 │                      │ 2. 选择目标节点       │
   │                 │                      │ 3. 分配资源           │
   │                 │                      │                      │
   │                 │                      │ CreateInstance(spec)  │
   │                 │                      │─────────────────────▶│
   │                 │                      │                      │
   │                 │                      │                      │ 4. 调用 Driver
   │                 │                      │                      │    创建实例
   │                 │                      │                      │
   │                 │                      │      Instance        │
   │                 │                      │◀─────────────────────│
   │                 │                      │                      │
   │                 │                      │ 5. 更新 etcd 状态     │
   │                 │                      │                      │
   │                 │    Instance          │                      │
   │                 │◀─────────────────────│                      │
   │                 │                      │                      │
   │   instance info │                      │                      │
   │◀────────────────│                      │                      │
   │                 │                      │                      │
```

## 核心组件

### hypervisor-server (控制平面)

控制平面服务器负责集群管理、请求调度和 API 网关功能。

**主要职责:**
- 接收和处理客户端 API 请求
- 节点注册和服务发现
- 实例调度决策
- 资源分配和配额管理
- 与 Agent 通信协调

**核心模块:**

```
internal/server/
├── server.go              # gRPC 服务器初始化
├── cluster_service.go     # 集群管理业务逻辑
├── cluster_grpc_handler.go # ClusterService gRPC 适配器
├── compute_service.go     # 计算资源管理业务逻辑
├── compute_grpc_handler.go # ComputeService gRPC 适配器
└── agent_client.go        # Agent 连接池管理
```

**配置选项:**

```yaml
# server.yaml
server:
  grpc_address: ":50051"      # gRPC 服务地址
  http_address: ":8080"       # HTTP/REST 网关地址

etcd:
  endpoints:
    - "localhost:2379"
  dial_timeout: 5s

heartbeat:
  interval: 10s               # 心跳检查间隔
  timeout: 30s                # 节点超时时间
```

### hypervisor-agent (计算节点)

计算节点代理负责管理本地计算资源和实例生命周期。

**主要职责:**
- 向控制平面注册和心跳
- 管理本地实例生命周期
- 资源使用监控和上报
- 调用底层驱动执行操作

**核心模块:**

```
internal/agent/
├── agent.go          # Agent 核心逻辑
└── grpc_service.go   # AgentService gRPC 实现
```

**配置选项:**

```yaml
# agent.yaml
node:
  hostname: ""                # 自动检测
  region: "default"
  zone: "default"
  role: "worker"

agent:
  port: 50052
  server_address: "localhost:50051"

drivers:
  enabled:
    - vm
    - container
    - microvm

libvirt:
  uri: "qemu:///system"

containerd:
  address: "/run/containerd/containerd.sock"
```

### hypervisor-ctl (命令行工具)

CLI 工具用于集群管理和运维操作。

**命令结构:**

```
hypervisor-ctl
├── node
│   ├── list              # 列出所有节点
│   ├── get <id>          # 获取节点详情
│   ├── drain <id>        # 排空节点（维护模式）
│   ├── cordon <id>       # 标记节点不可调度
│   └── uncordon <id>     # 恢复节点可调度
├── instance
│   ├── list              # 列出实例
│   ├── get <id>          # 获取实例详情
│   ├── create            # 创建实例
│   ├── start <id>        # 启动实例
│   ├── stop <id>         # 停止实例
│   └── delete <id>       # 删除实例
├── cluster
│   └── info              # 集群信息
└── version               # 版本信息
```

## 通信协议

### gRPC 服务定义

项目定义了三个核心 gRPC 服务：

#### ClusterService (集群管理)

```protobuf
service ClusterService {
  // 节点管理
  rpc RegisterNode(RegisterNodeRequest) returns (RegisterNodeResponse);
  rpc DeregisterNode(DeregisterNodeRequest) returns (DeregisterNodeResponse);
  rpc ListNodes(ListNodesRequest) returns (ListNodesResponse);
  rpc GetNode(GetNodeRequest) returns (GetNodeResponse);
  rpc UpdateNodeStatus(UpdateNodeStatusRequest) returns (UpdateNodeStatusResponse);

  // 心跳
  rpc Heartbeat(HeartbeatRequest) returns (HeartbeatResponse);

  // 集群信息
  rpc GetClusterInfo(GetClusterInfoRequest) returns (GetClusterInfoResponse);
}
```

#### ComputeService (计算资源管理)

```protobuf
service ComputeService {
  // 实例生命周期
  rpc CreateInstance(CreateInstanceRequest) returns (CreateInstanceResponse);
  rpc GetInstance(GetInstanceRequest) returns (GetInstanceResponse);
  rpc ListInstances(ListInstancesRequest) returns (ListInstancesResponse);
  rpc StartInstance(StartInstanceRequest) returns (StartInstanceResponse);
  rpc StopInstance(StopInstanceRequest) returns (StopInstanceResponse);
  rpc DeleteInstance(DeleteInstanceRequest) returns (DeleteInstanceResponse);

  // 实例操作
  rpc RestartInstance(RestartInstanceRequest) returns (RestartInstanceResponse);
  rpc GetInstanceStats(GetInstanceStatsRequest) returns (GetInstanceStatsResponse);

  // 控制台访问
  rpc AttachConsole(stream ConsoleInput) returns (stream ConsoleOutput);
}
```

#### AgentService (Agent 内部通信)

```protobuf
service AgentService {
  // Server -> Agent 的实例操作
  rpc CreateInstance(CreateInstanceRequest) returns (CreateInstanceResponse);
  rpc StartInstance(StartInstanceRequest) returns (StartInstanceResponse);
  rpc StopInstance(StopInstanceRequest) returns (StopInstanceResponse);
  rpc DeleteInstance(DeleteInstanceRequest) returns (DeleteInstanceResponse);

  // 资源查询
  rpc GetNodeResources(GetNodeResourcesRequest) returns (GetNodeResourcesResponse);
  rpc ListLocalInstances(ListLocalInstancesRequest) returns (ListLocalInstancesResponse);
}
```

### 消息流转

```
Client API 调用:
  Client ──gRPC──▶ Server ──gRPC──▶ Agent ──Driver API──▶ Runtime

状态同步:
  Agent ──Heartbeat──▶ Server ──Update──▶ etcd

事件通知:
  Agent ──gRPC Stream──▶ Server ──gRPC-Web──▶ Web UI
```

## 数据模型

### 节点 (Node)

```go
type Node struct {
    ID        string            `json:"id"`
    Hostname  string            `json:"hostname"`
    Address   string            `json:"address"`
    Port      int               `json:"port"`
    Region    string            `json:"region"`
    Zone      string            `json:"zone"`
    Role      NodeRole          `json:"role"`      // master/worker
    Status    NodeStatus        `json:"status"`    // ready/offline/maintenance
    Resources NodeResources     `json:"resources"`
    Labels    map[string]string `json:"labels"`
    CreatedAt time.Time         `json:"created_at"`
    UpdatedAt time.Time         `json:"updated_at"`
}

type NodeResources struct {
    CPUCores     int64 `json:"cpu_cores"`
    CPUUsedCores int64 `json:"cpu_used_cores"`
    MemoryBytes  int64 `json:"memory_bytes"`
    MemoryUsed   int64 `json:"memory_used"`
    DiskBytes    int64 `json:"disk_bytes"`
    DiskUsed     int64 `json:"disk_used"`
}
```

### 实例 (Instance)

```go
type Instance struct {
    ID         string          `json:"id"`
    Name       string          `json:"name"`
    Type       InstanceType    `json:"type"`  // vm/container/microvm
    Status     InstanceStatus  `json:"status"`
    NodeID     string          `json:"node_id"`
    Spec       InstanceSpec    `json:"spec"`
    Stats      InstanceStats   `json:"stats"`
    Labels     map[string]string `json:"labels"`
    CreatedAt  time.Time       `json:"created_at"`
    StartedAt  *time.Time      `json:"started_at,omitempty"`
}

type InstanceSpec struct {
    Image     string `json:"image"`
    CPUCores  int    `json:"cpu_cores"`
    MemoryMB  int    `json:"memory_mb"`
    DiskGB    int    `json:"disk_gb"`

    // VM 特定
    KernelPath string `json:"kernel_path,omitempty"`
    InitrdPath string `json:"initrd_path,omitempty"`

    // 容器特定
    Command    []string          `json:"command,omitempty"`
    Env        map[string]string `json:"env,omitempty"`
}
```

### etcd 数据布局

```
/hypervisor/
├── nodes/
│   ├── {node-id-1}           # 节点信息 JSON
│   ├── {node-id-2}
│   └── ...
├── instances/
│   ├── {instance-id-1}       # 实例信息 JSON
│   ├── {instance-id-2}
│   └── ...
├── leases/
│   └── nodes/
│       ├── {node-id-1}       # 节点租约（心跳）
│       └── ...
└── config/
    └── cluster               # 集群配置
```

## 多运行时驱动架构

### Driver 接口

所有计算运行时实现统一的 Driver 接口：

```go
// pkg/compute/driver/driver.go

type Driver interface {
    // 生命周期管理
    Create(ctx context.Context, spec *InstanceSpec) (*Instance, error)
    Start(ctx context.Context, id string) error
    Stop(ctx context.Context, id string) error
    Delete(ctx context.Context, id string) error

    // 状态查询
    Get(ctx context.Context, id string) (*Instance, error)
    List(ctx context.Context) ([]*Instance, error)
    Stats(ctx context.Context, id string) (*InstanceStats, error)

    // 控制台
    Attach(ctx context.Context, id string) (Console, error)

    // 驱动信息
    Name() string
    Type() InstanceType
}

type Console interface {
    io.ReadWriteCloser
    Resize(rows, cols int) error
}
```

### 驱动实现

```
pkg/compute/
├── driver/
│   ├── driver.go         # 接口定义
│   ├── errors.go         # 错误类型
│   └── manager.go        # 驱动管理器
├── libvirt/
│   ├── libvirt.go        # KVM/QEMU 实现
│   └── libvirt_stub.go   # 无 CGO 时的 stub
├── containerd/
│   └── containerd.go     # containerd 实现
└── firecracker/
    └── firecracker.go    # Firecracker 实现
```

### 驱动选择

```go
// 驱动管理器根据实例类型选择合适的驱动
type DriverManager struct {
    drivers map[InstanceType]Driver
}

func (m *DriverManager) GetDriver(instanceType InstanceType) (Driver, error) {
    driver, ok := m.drivers[instanceType]
    if !ok {
        return nil, ErrDriverNotFound
    }
    return driver, nil
}
```

### 添加新驱动

1. 实现 `Driver` 接口
2. 在 `internal/agent/agent.go` 中注册驱动
3. 添加配置支持

```go
// 示例：添加新的运行时驱动
type MyDriver struct {
    config MyDriverConfig
}

func NewMyDriver(config MyDriverConfig) *MyDriver {
    return &MyDriver{config: config}
}

func (d *MyDriver) Create(ctx context.Context, spec *InstanceSpec) (*Instance, error) {
    // 实现创建逻辑
}

// ... 实现其他接口方法
```

## 状态管理

### 状态分类

| 状态类型 | 存储位置 | 一致性要求 | 示例 |
|----------|----------|------------|------|
| 持久状态 | etcd | 强一致性 | 节点注册、实例配置 |
| 运行时状态 | 内存 | 最终一致性 | 连接状态、缓存 |
| 本地状态 | Agent 本地 | 本地一致 | 实例运行状态 |

### 状态同步机制

```
┌─────────────────────────────────────────────────────────────┐
│                        状态同步                              │
│                                                             │
│  Agent                    Server                   etcd     │
│    │                        │                        │      │
│    │──── Heartbeat ────────▶│                        │      │
│    │     (包含资源使用)      │                        │      │
│    │                        │                        │      │
│    │                        │──── Put(node_state) ──▶│      │
│    │                        │                        │      │
│    │                        │◀─── Watch(instances) ──│      │
│    │                        │                        │      │
│    │◀─── Reconcile ─────────│                        │      │
│    │     (状态同步)          │                        │      │
│    │                        │                        │      │
└─────────────────────────────────────────────────────────────┘
```

### 一致性保证

1. **节点注册**: 使用 etcd 租约（Lease）机制
2. **实例操作**: 乐观锁 + 版本号
3. **故障恢复**: 定期同步 + 事件驱动

## 高可用设计

### 控制平面高可用

```
                    ┌─────────────────┐
                    │  Load Balancer  │
                    └────────┬────────┘
                             │
            ┌────────────────┼────────────────┐
            │                │                │
            ▼                ▼                ▼
    ┌───────────────┐ ┌───────────────┐ ┌───────────────┐
    │   Server 1    │ │   Server 2    │ │   Server 3    │
    │   (Active)    │ │   (Active)    │ │   (Active)    │
    └───────┬───────┘ └───────┬───────┘ └───────┬───────┘
            │                 │                 │
            └────────────────┬┴─────────────────┘
                             │
                             ▼
                    ┌─────────────────┐
                    │   etcd Cluster  │
                    │   (3+ nodes)    │
                    └─────────────────┘
```

**特点:**
- 无状态设计，多实例部署
- 所有状态存储在 etcd
- 负载均衡器分发请求

### 计算节点容错

```
节点故障检测流程:

1. Agent 心跳超时
   │
   ▼
2. Server 标记节点 Offline
   │
   ▼
3. 触发实例迁移（可选）
   │
   ▼
4. 更新调度策略排除故障节点
```

### 故障恢复

| 故障类型 | 检测方式 | 恢复策略 |
|----------|----------|----------|
| Agent 崩溃 | 心跳超时 | 自动重启/实例迁移 |
| Server 崩溃 | 负载均衡健康检查 | 请求转发到其他实例 |
| etcd 故障 | 连接超时 | 重试/failover |
| 网络分区 | 心跳丢失 | 防脑裂机制 |

## 安全架构

### 认证和授权

```
┌──────────────────────────────────────────────────────────┐
│                      安全层                               │
│                                                          │
│  ┌────────────────┐  ┌────────────────┐  ┌────────────┐ │
│  │   TLS 加密     │  │   JWT 认证     │  │  RBAC 授权  │ │
│  │   (传输层)     │  │   (身份验证)    │  │  (访问控制) │ │
│  └────────────────┘  └────────────────┘  └────────────┘ │
│                                                          │
└──────────────────────────────────────────────────────────┘
```

### TLS 配置

```yaml
# server.yaml
tls:
  enabled: true
  cert_file: "/etc/hypervisor/certs/server.crt"
  key_file: "/etc/hypervisor/certs/server.key"
  ca_file: "/etc/hypervisor/certs/ca.crt"
  client_auth: true  # 双向 TLS
```

### 安全最佳实践

1. **最小权限原则**: Agent 只能管理本地资源
2. **传输加密**: 所有 gRPC 通信使用 TLS
3. **证书轮换**: 支持证书热更新
4. **审计日志**: 记录所有管理操作

## 扩展性设计

### 水平扩展

```
                    负载增加
                        │
                        ▼
    ┌─────────────────────────────────────────┐
    │              扩展方向                    │
    │                                         │
    │  控制平面: 增加 Server 实例               │
    │     - 无状态设计                         │
    │     - 负载均衡分发                       │
    │                                         │
    │  计算平面: 增加 Agent 节点               │
    │     - 自动注册发现                       │
    │     - 动态调度                          │
    │                                         │
    │  存储平面: etcd 集群扩容                 │
    │     - 增加 etcd 成员                    │
    │     - 数据自动复制                       │
    │                                         │
    └─────────────────────────────────────────┘
```

### 插件机制

```go
// 调度器插件接口
type Scheduler interface {
    Schedule(ctx context.Context, spec *InstanceSpec, nodes []*Node) (*Node, error)
    Name() string
}

// 默认调度器实现
type DefaultScheduler struct{}

func (s *DefaultScheduler) Schedule(ctx context.Context, spec *InstanceSpec, nodes []*Node) (*Node, error) {
    // 资源匹配 + 负载均衡
    for _, node := range nodes {
        if node.CanFit(spec) {
            return node, nil
        }
    }
    return nil, ErrNoAvailableNode
}
```

### 扩展点

| 扩展点 | 接口 | 用途 |
|--------|------|------|
| 计算驱动 | `Driver` | 添加新的虚拟化技术 |
| 调度策略 | `Scheduler` | 自定义调度算法 |
| 存储后端 | `Storage` | 替换 etcd |
| 认证方式 | `Authenticator` | 自定义认证 |

## 相关文档

- [开发指南](./DEVELOPMENT.md) - 开发环境和代码规范
- [测试指南](./TESTING.md) - 测试策略和最佳实践
- [API 定义](../api/proto/) - gRPC 服务定义
- [README](../README.md) - 项目概述
