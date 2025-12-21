# Hypervisor 测试指南

本文档提供了 Hypervisor 项目的完整测试指南，包括测试策略、测试编写规范、测试执行方法和持续集成配置。

## 目录

- [测试概述](#测试概述)
- [测试执行](#测试执行)
- [测试结构](#测试结构)
- [单元测试](#单元测试)
- [集成测试](#集成测试)
- [端到端测试](#端到端测试)
- [Mock 和测试工具](#mock-和测试工具)
- [测试覆盖率](#测试覆盖率)
- [持续集成](#持续集成)
- [前端测试](#前端测试)
- [性能测试](#性能测试)
- [最佳实践](#最佳实践)

## 测试概述

### 测试金字塔

```
        /\
       /  \        端到端测试 (E2E)
      /----\       - 完整系统测试
     /      \      - 真实环境验证
    /--------\
   /          \    集成测试
  /  --------  \   - 组件间交互
 /              \  - 数据库/etcd 集成
/----------------\
|    单元测试    |  - 函数/方法级别
|                |  - 隔离测试
------------------
```

### 测试框架

| 类型 | 工具 | 说明 |
|------|------|------|
| Go 单元测试 | `testing` | Go 标准库 |
| Go 断言 | `testify` | 增强断言和 mock |
| Go Mock | `gomock` | 接口 mock 生成 |
| 前端测试 | `vitest` | Vue 3 测试框架 |
| E2E 测试 | `testcontainers` | 容器化集成测试 |

## 测试执行

### 常用命令

```bash
# 运行所有测试（启用竞态检测）
make test

# 快速测试（跳过长时间运行的测试）
make test-short

# 生成覆盖率报告
make test-coverage

# 运行特定包的测试
go test -v ./pkg/cluster/etcd/...

# 运行特定测试函数
go test -v -run TestNodeRegistration ./pkg/cluster/registry/

# 运行匹配模式的测试
go test -v -run "Test.*Create.*" ./internal/server/

# 带超时运行测试
go test -v -timeout 5m ./...
```

### 测试标签

使用构建标签来分类测试：

```go
//go:build integration

package etcd_test
```

运行特定标签的测试：

```bash
# 只运行集成测试
go test -v -tags=integration ./...

# 排除集成测试
go test -v ./... -short
```

### 竞态检测

所有测试默认启用竞态检测：

```bash
go test -race ./...
```

## 测试结构

### 目录组织

测试文件应该与被测代码放在同一目录：

```
pkg/
└── cluster/
    └── etcd/
        ├── client.go           # 源代码
        ├── client_test.go      # 单元测试
        ├── client_integration_test.go  # 集成测试（带标签）
        ├── errors.go
        └── testdata/           # 测试数据文件
            └── fixtures.json
```

### 测试文件命名

| 后缀 | 用途 |
|------|------|
| `_test.go` | 单元测试 |
| `_integration_test.go` | 集成测试 |
| `_e2e_test.go` | 端到端测试 |
| `_bench_test.go` | 性能基准测试 |

## 单元测试

### 基本结构

```go
package etcd_test

import (
    "context"
    "testing"
    "time"

    "github.com/stretchr/testify/assert"
    "github.com/stretchr/testify/require"

    "hypervisor/pkg/cluster/etcd"
)

func TestClient_Put(t *testing.T) {
    // Arrange
    ctx := context.Background()
    client := setupTestClient(t)
    defer client.Close()

    // Act
    err := client.Put(ctx, "test-key", "test-value")

    // Assert
    require.NoError(t, err)
}
```

### 表格驱动测试

推荐使用表格驱动测试来覆盖多种情况：

```go
func TestNodeStatus_String(t *testing.T) {
    tests := []struct {
        name     string
        status   NodeStatus
        expected string
    }{
        {
            name:     "ready status",
            status:   NodeStatusReady,
            expected: "Ready",
        },
        {
            name:     "offline status",
            status:   NodeStatusOffline,
            expected: "Offline",
        },
        {
            name:     "unknown status",
            status:   NodeStatus(999),
            expected: "Unknown",
        },
    }

    for _, tt := range tests {
        t.Run(tt.name, func(t *testing.T) {
            result := tt.status.String()
            assert.Equal(t, tt.expected, result)
        })
    }
}
```

### 子测试

使用 `t.Run()` 组织相关测试：

```go
func TestRegistry(t *testing.T) {
    t.Run("Node", func(t *testing.T) {
        t.Run("Register", func(t *testing.T) {
            // 测试节点注册
        })

        t.Run("Deregister", func(t *testing.T) {
            // 测试节点注销
        })

        t.Run("List", func(t *testing.T) {
            // 测试节点列表
        })
    })

    t.Run("Instance", func(t *testing.T) {
        // 测试实例相关功能
    })
}
```

### 测试辅助函数

创建可重用的测试辅助函数：

```go
// testutil/helpers.go
package testutil

import (
    "testing"

    "go.uber.org/zap"
)

// NewTestLogger creates a logger for testing
func NewTestLogger(t *testing.T) *zap.Logger {
    t.Helper()
    logger, _ := zap.NewDevelopment()
    return logger
}

// MustParseTime parses a time string or fails the test
func MustParseTime(t *testing.T, layout, value string) time.Time {
    t.Helper()
    parsed, err := time.Parse(layout, value)
    if err != nil {
        t.Fatalf("failed to parse time: %v", err)
    }
    return parsed
}
```

### 错误测试

测试错误情况同样重要：

```go
func TestClient_Get_NotFound(t *testing.T) {
    ctx := context.Background()
    client := setupTestClient(t)
    defer client.Close()

    _, err := client.Get(ctx, "non-existent-key")

    assert.Error(t, err)
    assert.True(t, errors.Is(err, etcd.ErrKeyNotFound))
}

func TestClient_Connect_InvalidEndpoint(t *testing.T) {
    ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
    defer cancel()

    _, err := etcd.NewClient(ctx, etcd.Config{
        Endpoints: []string{"invalid:9999"},
    })

    assert.Error(t, err)
}
```

## 集成测试

### etcd 集成测试

使用 testcontainers 启动真实的 etcd：

```go
//go:build integration

package etcd_test

import (
    "context"
    "testing"

    "github.com/stretchr/testify/require"
    "github.com/testcontainers/testcontainers-go"
    "github.com/testcontainers/testcontainers-go/wait"

    "hypervisor/pkg/cluster/etcd"
)

func setupEtcdContainer(t *testing.T) (string, func()) {
    t.Helper()

    ctx := context.Background()

    req := testcontainers.ContainerRequest{
        Image:        "quay.io/coreos/etcd:v3.5.9",
        ExposedPorts: []string{"2379/tcp"},
        Cmd: []string{
            "etcd",
            "--advertise-client-urls=http://0.0.0.0:2379",
            "--listen-client-urls=http://0.0.0.0:2379",
        },
        WaitingFor: wait.ForHTTP("/health").WithPort("2379/tcp"),
    }

    container, err := testcontainers.GenericContainer(ctx, testcontainers.GenericContainerRequest{
        ContainerRequest: req,
        Started:          true,
    })
    require.NoError(t, err)

    endpoint, err := container.Endpoint(ctx, "")
    require.NoError(t, err)

    cleanup := func() {
        container.Terminate(ctx)
    }

    return "http://" + endpoint, cleanup
}

func TestClient_Integration(t *testing.T) {
    if testing.Short() {
        t.Skip("skipping integration test in short mode")
    }

    endpoint, cleanup := setupEtcdContainer(t)
    defer cleanup()

    ctx := context.Background()
    client, err := etcd.NewClient(ctx, etcd.Config{
        Endpoints: []string{endpoint},
    })
    require.NoError(t, err)
    defer client.Close()

    // 测试 Put/Get
    err = client.Put(ctx, "test-key", "test-value")
    require.NoError(t, err)

    value, err := client.Get(ctx, "test-key")
    require.NoError(t, err)
    assert.Equal(t, "test-value", value)
}
```

### gRPC 服务测试

测试 gRPC 服务端点：

```go
package server_test

import (
    "context"
    "net"
    "testing"

    "github.com/stretchr/testify/require"
    "google.golang.org/grpc"
    "google.golang.org/grpc/credentials/insecure"
    "google.golang.org/grpc/test/bufconn"

    pb "hypervisor/api/gen"
    "hypervisor/internal/server"
)

const bufSize = 1024 * 1024

func setupTestServer(t *testing.T) (pb.ClusterServiceClient, func()) {
    t.Helper()

    lis := bufconn.Listen(bufSize)

    s := grpc.NewServer()
    srv := server.NewClusterService(/* dependencies */)
    pb.RegisterClusterServiceServer(s, srv)

    go func() {
        if err := s.Serve(lis); err != nil {
            t.Logf("server exited: %v", err)
        }
    }()

    dialer := func(context.Context, string) (net.Conn, error) {
        return lis.Dial()
    }

    conn, err := grpc.DialContext(
        context.Background(),
        "bufnet",
        grpc.WithContextDialer(dialer),
        grpc.WithTransportCredentials(insecure.NewCredentials()),
    )
    require.NoError(t, err)

    client := pb.NewClusterServiceClient(conn)

    cleanup := func() {
        conn.Close()
        s.GracefulStop()
    }

    return client, cleanup
}

func TestClusterService_ListNodes(t *testing.T) {
    client, cleanup := setupTestServer(t)
    defer cleanup()

    ctx := context.Background()
    resp, err := client.ListNodes(ctx, &pb.ListNodesRequest{})

    require.NoError(t, err)
    require.NotNil(t, resp)
}
```

## 端到端测试

### 完整系统测试

```go
//go:build e2e

package e2e_test

import (
    "context"
    "os/exec"
    "testing"
    "time"

    "github.com/stretchr/testify/require"
    "google.golang.org/grpc"
    "google.golang.org/grpc/credentials/insecure"

    pb "hypervisor/api/gen"
)

func TestE2E_NodeLifecycle(t *testing.T) {
    if testing.Short() {
        t.Skip("skipping e2e test in short mode")
    }

    // 启动服务器
    serverCmd := exec.Command("./bin/hypervisor-server", "--config", "configs/test-server.yaml")
    require.NoError(t, serverCmd.Start())
    defer serverCmd.Process.Kill()

    // 等待服务器启动
    time.Sleep(2 * time.Second)

    // 启动代理
    agentCmd := exec.Command("./bin/hypervisor-agent", "--config", "configs/test-agent.yaml")
    require.NoError(t, agentCmd.Start())
    defer agentCmd.Process.Kill()

    // 等待代理注册
    time.Sleep(3 * time.Second)

    // 连接并验证
    conn, err := grpc.Dial(
        "localhost:50051",
        grpc.WithTransportCredentials(insecure.NewCredentials()),
    )
    require.NoError(t, err)
    defer conn.Close()

    client := pb.NewClusterServiceClient(conn)

    ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
    defer cancel()

    resp, err := client.ListNodes(ctx, &pb.ListNodesRequest{})
    require.NoError(t, err)
    require.GreaterOrEqual(t, len(resp.Nodes), 1)
}
```

## Mock 和测试工具

### 使用 gomock

1. 安装 mockgen：

```bash
go install github.com/golang/mock/mockgen@latest
```

2. 生成 mock：

```bash
# 从接口生成
mockgen -source=pkg/compute/driver/driver.go -destination=pkg/compute/driver/mock_driver.go -package=driver

# 或添加 go:generate 指令
//go:generate mockgen -source=driver.go -destination=mock_driver.go -package=driver
```

3. 使用 mock：

```go
func TestInstanceManager_Create(t *testing.T) {
    ctrl := gomock.NewController(t)
    defer ctrl.Finish()

    mockDriver := driver.NewMockDriver(ctrl)
    mockDriver.EXPECT().
        Create(gomock.Any(), gomock.Any()).
        Return(&driver.Instance{ID: "test-id"}, nil)

    manager := NewInstanceManager(mockDriver)

    instance, err := manager.Create(context.Background(), &InstanceSpec{})

    require.NoError(t, err)
    assert.Equal(t, "test-id", instance.ID)
}
```

### 使用 testify mock

```go
type MockRegistry struct {
    mock.Mock
}

func (m *MockRegistry) RegisterNode(ctx context.Context, node *Node) error {
    args := m.Called(ctx, node)
    return args.Error(0)
}

func TestHeartbeat_Start(t *testing.T) {
    mockRegistry := new(MockRegistry)
    mockRegistry.On("RegisterNode", mock.Anything, mock.Anything).Return(nil)

    hb := NewHeartbeat(mockRegistry)
    err := hb.Start(context.Background())

    require.NoError(t, err)
    mockRegistry.AssertExpectations(t)
}
```

### Fake 实现

对于复杂依赖，创建 fake 实现：

```go
// pkg/cluster/etcd/fake_client.go
package etcd

import (
    "context"
    "sync"
)

// FakeClient is an in-memory etcd client for testing
type FakeClient struct {
    mu   sync.RWMutex
    data map[string]string
}

func NewFakeClient() *FakeClient {
    return &FakeClient{
        data: make(map[string]string),
    }
}

func (c *FakeClient) Put(ctx context.Context, key, value string) error {
    c.mu.Lock()
    defer c.mu.Unlock()
    c.data[key] = value
    return nil
}

func (c *FakeClient) Get(ctx context.Context, key string) (string, error) {
    c.mu.RLock()
    defer c.mu.RUnlock()
    value, ok := c.data[key]
    if !ok {
        return "", ErrKeyNotFound
    }
    return value, nil
}

func (c *FakeClient) Close() error {
    return nil
}
```

## 测试覆盖率

### 生成覆盖率报告

```bash
# 生成覆盖率报告
make test-coverage

# 手动生成
go test -coverprofile=coverage.out ./...

# 查看 HTML 报告
go tool cover -html=coverage.out -o coverage.html

# 查看函数覆盖率
go tool cover -func=coverage.out
```

### 覆盖率目标

| 包类型 | 目标覆盖率 |
|--------|------------|
| 核心业务逻辑 | >= 80% |
| 工具/辅助函数 | >= 70% |
| 驱动实现 | >= 60% |
| 整体项目 | >= 70% |

### CI 中的覆盖率

覆盖率报告会自动上传到 codecov：

```yaml
# .github/workflows/ci.yml
- name: Upload coverage
  uses: codecov/codecov-action@v3
  with:
    file: ./coverage.out
    fail_ci_if_error: true
```

## 持续集成

### GitHub Actions 工作流

项目使用 GitHub Actions 进行持续集成，配置文件为 `.github/workflows/ci.yml`。

### 工作流阶段

```
┌─────────┐    ┌──────────┐    ┌─────────┐
│  Lint   │───▶│   Test   │───▶│  Build  │
└─────────┘    └──────────┘    └─────────┘
     │              │               │
     ▼              ▼               ▼
  golangci-    go test -race   Linux amd64/arm64
    lint       + etcd service    binaries
```

### 本地 CI 模拟

在提交前本地运行 CI 检查：

```bash
# 运行所有 CI 步骤
make lint && make test && make build

# 或使用 act（需要 Docker）
act push
```

### CI 环境变量

| 变量 | 说明 |
|------|------|
| `ETCD_ENDPOINTS` | etcd 端点地址（CI 自动设置） |
| `CGO_ENABLED` | CGO 开关（构建时设为 0） |
| `GOOS` | 目标操作系统 |
| `GOARCH` | 目标架构 |

## 前端测试

### 技术栈

- **Vitest** - 测试运行器（与 Vite 集成）
- **Vue Test Utils** - Vue 组件测试
- **@testing-library/vue** - 用户行为测试

### 设置

```bash
cd control_and_manage_plane/src

# 安装测试依赖
npm install -D vitest @vue/test-utils @testing-library/vue
```

### 配置

```typescript
// vitest.config.ts
import { defineConfig } from 'vitest/config'
import vue from '@vitejs/plugin-vue'

export default defineConfig({
  plugins: [vue()],
  test: {
    globals: true,
    environment: 'jsdom',
    coverage: {
      reporter: ['text', 'html'],
    },
  },
})
```

### 组件测试示例

```typescript
// components/__tests__/ResourceGaugeChart.spec.ts
import { describe, it, expect } from 'vitest'
import { mount } from '@vue/test-utils'
import ResourceGaugeChart from '../charts/ResourceGaugeChart.vue'

describe('ResourceGaugeChart', () => {
  it('renders with default props', () => {
    const wrapper = mount(ResourceGaugeChart, {
      props: {
        title: 'CPU Usage',
        value: 75,
        max: 100,
      },
    })

    expect(wrapper.text()).toContain('CPU Usage')
  })

  it('displays correct percentage', () => {
    const wrapper = mount(ResourceGaugeChart, {
      props: {
        title: 'Memory',
        value: 50,
        max: 100,
      },
    })

    expect(wrapper.text()).toContain('50%')
  })
})
```

### Store 测试示例

```typescript
// stores/__tests__/cluster.spec.ts
import { describe, it, expect, beforeEach } from 'vitest'
import { setActivePinia, createPinia } from 'pinia'
import { useClusterStore } from '../cluster'

describe('Cluster Store', () => {
  beforeEach(() => {
    setActivePinia(createPinia())
  })

  it('initializes with empty nodes', () => {
    const store = useClusterStore()
    expect(store.nodes).toHaveLength(0)
  })

  it('adds node correctly', () => {
    const store = useClusterStore()
    store.addNode({
      id: 'node-1',
      hostname: 'test-host',
      status: 'ready',
    })

    expect(store.nodes).toHaveLength(1)
    expect(store.nodes[0].hostname).toBe('test-host')
  })
})
```

### 运行前端测试

```bash
cd control_and_manage_plane/src

# 运行测试
npm run test

# 监视模式
npm run test:watch

# 生成覆盖率
npm run test:coverage
```

## 性能测试

### 基准测试

```go
func BenchmarkClient_Put(b *testing.B) {
    ctx := context.Background()
    client := setupBenchClient(b)
    defer client.Close()

    b.ResetTimer()
    for i := 0; i < b.N; i++ {
        key := fmt.Sprintf("key-%d", i)
        client.Put(ctx, key, "value")
    }
}

func BenchmarkClient_Get(b *testing.B) {
    ctx := context.Background()
    client := setupBenchClient(b)
    defer client.Close()

    // 预填充数据
    client.Put(ctx, "benchmark-key", "benchmark-value")

    b.ResetTimer()
    for i := 0; i < b.N; i++ {
        client.Get(ctx, "benchmark-key")
    }
}
```

### 运行基准测试

```bash
# 运行所有基准测试
go test -bench=. ./...

# 运行特定基准测试
go test -bench=BenchmarkClient_Put ./pkg/cluster/etcd/

# 包含内存分配统计
go test -bench=. -benchmem ./...

# 多次运行取平均
go test -bench=. -count=5 ./...
```

### 性能分析

```bash
# CPU 分析
go test -bench=. -cpuprofile=cpu.out ./pkg/cluster/etcd/
go tool pprof cpu.out

# 内存分析
go test -bench=. -memprofile=mem.out ./pkg/cluster/etcd/
go tool pprof mem.out
```

## 最佳实践

### 测试编写原则

1. **测试行为，而非实现**
   - 关注公共 API 和外部可观察行为
   - 避免测试私有方法

2. **每个测试只验证一件事**
   ```go
   // 好
   func TestUser_Create(t *testing.T) { ... }
   func TestUser_Create_DuplicateEmail(t *testing.T) { ... }

   // 不好
   func TestUser_CreateAndUpdateAndDelete(t *testing.T) { ... }
   ```

3. **测试名称要有描述性**
   ```go
   // 好
   func TestClient_Get_ReturnsErrorWhenKeyNotFound(t *testing.T)

   // 不好
   func TestGet1(t *testing.T)
   ```

4. **使用 t.Helper() 标记辅助函数**
   ```go
   func setupTestClient(t *testing.T) *Client {
       t.Helper()  // 错误堆栈将指向调用者
       // ...
   }
   ```

5. **使用 t.Cleanup() 清理资源**
   ```go
   func TestWithDatabase(t *testing.T) {
       db := setupDatabase(t)
       t.Cleanup(func() {
           db.Close()
       })
       // ...
   }
   ```

### 测试独立性

1. **测试之间不应有依赖**
2. **每个测试创建自己的测试数据**
3. **使用随机数据避免冲突**
   ```go
   func randomNodeID() string {
       return fmt.Sprintf("node-%d", rand.Int())
   }
   ```

### 避免常见错误

1. **不要使用 time.Sleep 等待**
   ```go
   // 不好
   time.Sleep(1 * time.Second)

   // 好
   require.Eventually(t, func() bool {
       return condition()
   }, 5*time.Second, 100*time.Millisecond)
   ```

2. **不要忽略错误**
   ```go
   // 不好
   result, _ := client.Get(ctx, key)

   // 好
   result, err := client.Get(ctx, key)
   require.NoError(t, err)
   ```

3. **使用 context 超时**
   ```go
   ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
   defer cancel()
   ```

### 测试数据管理

1. **使用 testdata 目录存放测试数据**
   ```
   pkg/
   └── parser/
       ├── parser.go
       ├── parser_test.go
       └── testdata/
           ├── valid_input.json
           └── invalid_input.json
   ```

2. **使用 golden files 进行输出验证**
   ```go
   func TestParser_Output(t *testing.T) {
       output := parser.Parse(input)

       golden := filepath.Join("testdata", t.Name()+".golden")
       if *update {
           os.WriteFile(golden, output, 0644)
       }

       expected, _ := os.ReadFile(golden)
       assert.Equal(t, expected, output)
   }
   ```

## 相关文档

- [开发指南](./DEVELOPMENT.md) - 开发环境搭建和日常开发流程
- [架构设计](./ARCHITECTURE.md) - 系统架构详细说明
- [CI 配置](../.github/workflows/ci.yml) - GitHub Actions 工作流配置
