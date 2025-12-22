# Hypervisor gRPC API 文档

Zixiao Hypervisor 的 gRPC API 参考文档。

## 概述

Hypervisor 使用 gRPC 作为主要的通信协议，提供四个核心服务：

| 服务 | 说明 | 方法数 |
|------|------|--------|
| [ClusterService](services/cluster-service.md) | 集群和节点管理 | 8 |
| [ComputeService](services/compute-service.md) | 实例生命周期管理 | 12 |
| [AgentService](services/agent-service.md) | 计算节点内部通信 | 9 |
| [NetworkService](services/network-service.md) | SDN 网络管理 | 31 |

## 快速开始

### 连接端点

| 环境 | gRPC 端点 | HTTP 端点 |
|------|-----------|-----------|
| 开发 | `localhost:50051` | `localhost:8080` |
| 生产 | `api.hypervisor.example.com:50051` | `api.hypervisor.example.com:8080` |

### 使用 grpcurl 测试

```bash
# 安装 grpcurl
brew install grpcurl  # macOS
# 或
go install github.com/fullstorydev/grpcurl/cmd/grpcurl@latest

# 列出服务
grpcurl -plaintext localhost:50051 list

# 列出方法
grpcurl -plaintext localhost:50051 list hypervisor.v1.ClusterService

# 调用方法
grpcurl -plaintext -d '{}' localhost:50051 hypervisor.v1.ClusterService/GetClusterInfo
```

### 使用 Go 客户端

```go
package main

import (
    "context"
    "log"

    "google.golang.org/grpc"
    "google.golang.org/grpc/credentials/insecure"
    pb "hypervisor/api/gen/v1"
)

func main() {
    conn, err := grpc.Dial("localhost:50051", grpc.WithTransportCredentials(insecure.NewCredentials()))
    if err != nil {
        log.Fatal(err)
    }
    defer conn.Close()

    client := pb.NewClusterServiceClient(conn)
    info, err := client.GetClusterInfo(context.Background(), &emptypb.Empty{})
    if err != nil {
        log.Fatal(err)
    }

    log.Printf("Cluster: %s, Nodes: %d", info.ClusterName, info.TotalNodes)
}
```

## Proto 文件

Proto 文件位于 `api/proto/` 目录：

```
api/proto/
├── common.proto    # 公共类型定义
├── cluster.proto   # ClusterService
├── compute.proto   # ComputeService
├── agent.proto     # AgentService
└── network.proto   # NetworkService
```

### 生成代码

```bash
make proto
```

## 认证

当前版本暂不支持认证。生产环境建议：

1. 使用 mTLS 进行双向认证
2. 通过网络策略限制访问
3. 使用 API Gateway 添加认证层

## 错误处理

API 使用标准 gRPC 错误码：

| 错误码 | 说明 |
|--------|------|
| `OK` | 成功 |
| `INVALID_ARGUMENT` | 无效参数 |
| `NOT_FOUND` | 资源不存在 |
| `ALREADY_EXISTS` | 资源已存在 |
| `PERMISSION_DENIED` | 权限不足 |
| `RESOURCE_EXHAUSTED` | 资源耗尽 |
| `FAILED_PRECONDITION` | 前置条件失败 |
| `INTERNAL` | 内部错误 |
| `UNAVAILABLE` | 服务不可用 |

## 分页

列表接口支持分页：

```protobuf
message ListNodesRequest {
  int32 page_size = 1;    // 每页数量（默认 20，最大 100）
  string page_token = 2;  // 分页令牌
}

message ListNodesResponse {
  repeated Node nodes = 1;
  string next_page_token = 2;  // 下一页令牌
  int32 total_count = 3;       // 总数量
}
```

## 流式 RPC

部分接口使用流式 RPC：

| 方法 | 类型 | 说明 |
|------|------|------|
| `WatchNodes` | 服务端流 | 监听节点变化 |
| `WatchInstance` | 服务端流 | 监听实例状态 |
| `AttachConsole` | 双向流 | 连接实例控制台 |
| `PullImage` | 服务端流 | 拉取镜像进度 |

## 版本

- Proto 包名: `hypervisor.v1`
- Go 包路径: `hypervisor/api/gen/v1`
