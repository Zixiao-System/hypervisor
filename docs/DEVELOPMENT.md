# Hypervisor 开发指南

本文档提供了 Hypervisor 项目的完整开发指南，包括环境搭建、项目构建、代码规范和日常开发流程。

## 目录

- [环境要求](#环境要求)
- [开发环境搭建](#开发环境搭建)
- [项目构建](#项目构建)
- [项目结构](#项目结构)
- [代码规范](#代码规范)
- [开发流程](#开发流程)
- [前端开发](#前端开发)
- [C 库开发](#c-库开发)
- [常见问题](#常见问题)

## 环境要求

### 必需依赖

| 依赖 | 版本要求 | 说明 |
|------|----------|------|
| Go | >= 1.23 | 编程语言 |
| etcd | >= 3.5.x | 分布式状态存储 |
| protoc | >= 3.x | Protocol Buffers 编译器 |
| make | - | 构建工具 |
| git | - | 版本控制 |

### 可选依赖

| 依赖 | 版本要求 | 说明 |
|------|----------|------|
| libvirt-dev | >= 8.0 | KVM/QEMU 虚拟化支持（CGO） |
| containerd | >= 1.7 | 容器运行时 |
| firecracker | >= 1.0 | MicroVM 支持 |
| Docker | >= 24.x | 容器化部署 |
| Node.js | >= 18.x | 前端开发 |

### 平台支持

- **开发**: macOS (arm64/amd64), Linux (amd64/arm64)
- **生产**: Linux (amd64/arm64)
- **Windows**: 仅支持 Windows 宿主机代理和 VDI 客户端组件

## 开发环境搭建

### 1. 克隆代码

```bash
git clone https://github.com/your-org/hypervisor.git
cd hypervisor
```

### 2. 安装 Go 依赖

```bash
make deps
```

### 3. 安装 Protocol Buffers 工具

```bash
make proto-deps
```

这将安装：
- `protoc-gen-go` - Go 代码生成插件
- `protoc-gen-go-grpc` - gRPC 代码生成插件

### 4. 启动 etcd（开发模式）

使用 Docker 快速启动 etcd：

```bash
docker run -d \
  --name etcd-dev \
  -p 2379:2379 \
  -p 2380:2380 \
  quay.io/coreos/etcd:v3.5.9 \
  etcd \
  --advertise-client-urls http://0.0.0.0:2379 \
  --listen-client-urls http://0.0.0.0:2379
```

验证 etcd 运行状态：

```bash
etcdctl --endpoints=http://localhost:2379 endpoint health
```

### 5. 配置文件准备

```bash
# 复制配置模板
cp configs/server.example.yaml configs/server.yaml
cp configs/agent.example.yaml configs/agent.yaml

# 根据本地环境修改配置
vim configs/server.yaml
vim configs/agent.yaml
```

### 6. 安装 libvirt（可选，用于 VM 支持）

**Ubuntu/Debian:**
```bash
sudo apt-get install libvirt-dev libvirt-daemon-system qemu-kvm
```

**macOS (仅用于交叉编译):**
```bash
brew install libvirt
```

## 项目构建

### 完整构建

```bash
# 构建所有组件（不含 libvirt）
make all

# 构建所有组件（包含 libvirt CGO 支持）
make all-with-libvirt
```

### 单独构建

```bash
# 构建控制平面服务器
make build-server

# 构建计算节点代理
make build-agent

# 构建命令行工具
make build-ctl

# 构建所有二进制文件
make build
```

### 交叉编译

```bash
# 为 Linux amd64 编译
make build-linux

# 手动交叉编译
GOOS=linux GOARCH=arm64 CGO_ENABLED=0 go build -o bin/hypervisor-server-linux-arm64 ./cmd/hypervisor-server
```

### 生成 Protocol Buffers 代码

```bash
# 生成 Go 代码
make proto

# 仅生成代码（跳过依赖检查）
make proto-gen
```

### 构建 Docker 镜像

```bash
# 构建所有镜像
make docker

# 单独构建
make docker-server
make docker-agent
```

### 清理构建产物

```bash
make clean
```

## 项目结构

```
hypervisor/
├── api/                    # gRPC API 定义
│   ├── proto/              # .proto 源文件
│   │   ├── agent.proto     # Agent 服务定义
│   │   ├── cluster.proto   # 集群服务定义
│   │   ├── compute.proto   # 计算服务定义
│   │   └── common.proto    # 共享类型
│   └── gen/                # 生成的 Go 代码
│
├── cmd/                    # 可执行文件入口
│   ├── hypervisor-server/  # 控制平面服务器
│   ├── hypervisor-agent/   # 计算节点代理
│   └── hypervisor-ctl/     # 命令行工具
│
├── internal/               # 内部包（不对外暴露）
│   ├── server/             # 服务器实现
│   │   ├── server.go       # gRPC 服务器设置
│   │   ├── cluster_service.go
│   │   ├── cluster_grpc_handler.go
│   │   ├── compute_service.go
│   │   ├── compute_grpc_handler.go
│   │   └── agent_client.go # Agent 连接池
│   └── agent/              # 代理实现
│       ├── agent.go        # Agent 核心逻辑
│       └── grpc_service.go # AgentService 实现
│
├── pkg/                    # 公共包
│   ├── cluster/            # 集群管理
│   │   ├── etcd/           # etcd 客户端封装
│   │   ├── registry/       # 节点和实例注册
│   │   └── heartbeat/      # 心跳监控
│   ├── compute/            # 计算驱动
│   │   ├── driver/         # 驱动接口定义
│   │   ├── libvirt/        # KVM/QEMU 驱动
│   │   ├── containerd/     # 容器运行时驱动
│   │   └── firecracker/    # MicroVM 驱动
│   └── virtual-apps-and-desktop/  # VDI 组件
│
├── control_and_manage_plane/  # Web 管理界面
│   └── src/                # Vue 3 + TypeScript
│
├── clib/                   # C 库（CGO 集成）
│   ├── libvirt-wrapper/    # libvirt C 封装
│   ├── guest-drivers/      # 客户机驱动
│   ├── ebpf-network-accel/ # eBPF 网络加速
│   └── ovs-dpdk-network/   # OVS-DPDK 集成
│
├── configs/                # 配置模板
├── deployments/            # 部署文件
│   ├── docker/             # Dockerfile
│   └── systemd/            # systemd 服务文件
│
└── docs/                   # 文档
```

### 核心包说明

| 包路径 | 功能 |
|--------|------|
| `pkg/cluster/etcd` | etcd 客户端封装，连接池管理 |
| `pkg/cluster/registry` | 节点注册、实例注册、服务发现 |
| `pkg/cluster/heartbeat` | 节点健康监控，可配置 TTL |
| `pkg/compute/driver` | 计算驱动接口定义 |
| `pkg/compute/libvirt` | KVM/QEMU 虚拟机驱动（需要 CGO） |
| `pkg/compute/containerd` | 容器运行时驱动 |
| `pkg/compute/firecracker` | Firecracker MicroVM 驱动 |
| `internal/server` | 控制平面 gRPC 服务实现 |
| `internal/agent` | 计算节点代理实现 |

## 代码规范

### Go 代码规范

本项目使用 `golangci-lint` 进行代码检查，配置文件为 `.golangci.yml`。

```bash
# 运行代码检查
make lint

# 格式化代码
make fmt

# 静态分析
make vet
```

### 启用的 Linter

- `errcheck` - 检查未处理的错误
- `gosimple` - 简化代码建议
- `govet` - Go 静态分析
- `staticcheck` - 高级静态分析
- `unused` - 未使用代码检测
- `gofmt` - 代码格式化检查
- `goimports` - import 排序检查
- `revive` - 代码风格检查
- `gocritic` - 代码质量检查

### 代码风格要求

1. **Import 分组和排序**
   ```go
   import (
       // 标准库
       "context"
       "fmt"

       // 第三方库
       "go.uber.org/zap"
       "google.golang.org/grpc"

       // 本地包（使用 hypervisor 前缀）
       "hypervisor/pkg/cluster/etcd"
       "hypervisor/internal/server"
   )
   ```

2. **Context 作为第一个参数**
   ```go
   func DoSomething(ctx context.Context, args ...interface{}) error {
       // ...
   }
   ```

3. **包注释是必需的**
   ```go
   // Package etcd provides a wrapper around the etcd client with connection pooling.
   package etcd
   ```

4. **错误字符串不要大写或以标点结尾**
   ```go
   // 正确
   return fmt.Errorf("failed to connect to server")

   // 错误
   return fmt.Errorf("Failed to connect to server.")
   ```

5. **使用结构化日志（zap）**
   ```go
   logger.Info("node registered",
       zap.String("node_id", nodeID),
       zap.String("address", addr),
   )
   ```

### Git 提交规范

```
<type>(<scope>): <subject>

<body>

<footer>
```

**类型 (type):**
- `feat`: 新功能
- `fix`: Bug 修复
- `docs`: 文档更新
- `style`: 代码格式（不影响代码运行）
- `refactor`: 重构
- `perf`: 性能优化
- `test`: 测试相关
- `chore`: 构建/工具相关

**示例:**
```
feat(compute): add firecracker microvm driver support

- Implement Driver interface for Firecracker
- Add configuration options for microvm resources
- Support snapshot and restore operations

Closes #42
```

## 开发流程

### 1. 运行开发服务器

```bash
# 终端 1: 启动控制平面
make run-server

# 终端 2: 启动代理（可多实例）
make run-agent

# 或手动指定配置
./bin/hypervisor-server --config configs/server.yaml --log-level debug
./bin/hypervisor-agent --config configs/agent.yaml --log-level debug
```

### 2. 使用 CLI 工具

```bash
# 列出所有节点
./bin/hypervisor-ctl node list

# 查看集群信息
./bin/hypervisor-ctl cluster info

# JSON 格式输出
./bin/hypervisor-ctl node list -o json
```

### 3. 日志级别

支持的日志级别：`debug`, `info`, `warn`, `error`

```bash
# 通过命令行参数设置
./bin/hypervisor-server --log-level debug

# 通过环境变量设置
HYPERVISOR_LOG_LEVEL=debug ./bin/hypervisor-server
```

### 4. 配置覆盖

配置可以通过以下方式覆盖（优先级从高到低）：

1. 命令行参数
2. 环境变量（使用 `HYPERVISOR_` 前缀）
3. 配置文件
4. 默认值

```bash
# 环境变量示例
HYPERVISOR_SERVER_GRPC_ADDRESS=:9090 ./bin/hypervisor-server
```

## 前端开发

### 技术栈

- **Vue 3** - 渐进式 JavaScript 框架
- **TypeScript** - 类型安全
- **Pinia** - 状态管理
- **MDUI** - Material Design 3 组件库
- **Vite** - 构建工具
- **ConnectRPC** - gRPC-Web 客户端

### 开发环境

```bash
cd control_and_manage_plane/src

# 安装依赖
npm install

# 生成 TypeScript protobuf 代码
npm run proto:gen

# 启动开发服务器
npm run dev
```

### gRPC-Web 代理

前端需要 gRPC-Web 代理来与后端通信。使用 Envoy 代理：

```bash
cd control_and_manage_plane/nginx_web
docker-compose up -d
```

### 目录结构

```
src/
├── api/                # gRPC 客户端层
│   ├── transport.ts    # 传输配置
│   ├── clients.ts      # 服务客户端实例
│   └── converters.ts   # 类型转换器
├── gen/                # 生成的 protobuf 代码
├── stores/             # Pinia 状态管理
├── components/         # Vue 组件
│   ├── charts/         # 图表组件
│   └── TerminalConsole.vue  # 终端组件
├── views/              # 页面视图
└── styles/             # 样式文件
    └── theme.css       # Zixiao 主题
```

### 添加新的 API 调用

1. 修改 `.proto` 文件
2. 重新生成代码：`npm run proto:gen`
3. 在 `api/clients.ts` 中添加客户端方法
4. 在相应的 store 中使用

## C 库开发

### libvirt 封装

```bash
# 构建 C 库
make clib

# 清理
make clib-clean
```

### 目录结构

```
clib/
├── libvirt-wrapper/     # libvirt API 封装
├── guest-drivers/       # 客户机驱动
│   ├── balloon/         # 内存气球
│   ├── virtio/          # VirtIO 环实现
│   └── windows/         # Windows VirtIO 驱动
├── ebpf-network-accel/  # eBPF/XDP 网络加速
└── ovs-dpdk-network/    # OVS-DPDK 集成
```

### Windows 组件

Windows 组件需要 Visual Studio 2022/2026 和 WDK：

- `clib/windows-host/windows-hypervisor-agent/` - Windows 宿主机代理
- `clib/guest-drivers/windows/Windows-VirtIO-Driver/` - Windows VirtIO 驱动

## 常见问题

### Q: 构建时报 CGO 错误

确保安装了必要的 C 库：
```bash
# Ubuntu
sudo apt-get install build-essential libvirt-dev

# macOS
xcode-select --install
brew install libvirt
```

如果不需要 libvirt 支持，使用 `make all` 而不是 `make all-with-libvirt`。

### Q: etcd 连接失败

1. 确认 etcd 正在运行：
   ```bash
   docker ps | grep etcd
   ```

2. 检查端口是否正确：
   ```bash
   nc -zv localhost 2379
   ```

3. 验证配置文件中的 etcd 地址

### Q: protoc 找不到

安装 Protocol Buffers：
```bash
# macOS
brew install protobuf

# Ubuntu
sudo apt-get install protobuf-compiler

# 或使用 make proto-deps 安装 Go 插件
make proto-deps
```

### Q: 前端连接后端失败

1. 确认后端服务正在运行
2. 检查 gRPC-Web 代理（Envoy）配置
3. 验证 CORS 设置
4. 查看浏览器控制台错误信息

### Q: 如何调试 gRPC 调用

启用 gRPC 调试日志：
```bash
GRPC_GO_LOG_VERBOSITY_LEVEL=99 \
GRPC_GO_LOG_SEVERITY_LEVEL=info \
./bin/hypervisor-server
```

## 相关文档

- [测试指南](./TESTING.md) - 测试编写和执行指南
- [架构设计](./ARCHITECTURE.md) - 系统架构详细说明
- [API 参考](../api/proto/) - gRPC API 定义
- [README](../README.md) - 项目概述
