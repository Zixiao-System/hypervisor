# ClusterService API

集群管理服务，用于节点注册、健康监控和集群协调。

## 服务概述

| 属性 | 值 |
|------|-----|
| 服务名称 | `ClusterService` |
| Proto 文件 | `api/proto/cluster.proto` |
| 包名 | `hypervisor.v1` |

## 方法列表

| 方法 | 描述 | 请求类型 | 响应类型 |
|------|------|----------|----------|
| [RegisterNode](#registernode) | 注册新节点 | RegisterNodeRequest | RegisterNodeResponse |
| [DeregisterNode](#deregisternode) | 注销节点 | DeregisterNodeRequest | Empty |
| [GetNode](#getnode) | 获取节点详情 | GetNodeRequest | Node |
| [ListNodes](#listnodes) | 列出所有节点 | ListNodesRequest | ListNodesResponse |
| [UpdateNodeStatus](#updatenodestatus) | 更新节点状态 | UpdateNodeStatusRequest | Node |
| [Heartbeat](#heartbeat) | 节点心跳 | HeartbeatRequest | HeartbeatResponse |
| [WatchNodes](#watchnodes) | 监听节点变化 | WatchNodesRequest | stream NodeEvent |
| [GetClusterInfo](#getclusterinfo) | 获取集群信息 | Empty | ClusterInfo |

---

## RegisterNode

注册新的计算节点到集群。

### 请求

**RegisterNodeRequest**

| 字段 | 类型 | 必填 | 描述 |
|------|------|------|------|
| hostname | string | 是 | 节点主机名 |
| ip | string | 是 | 节点 IP 地址 |
| port | int32 | 是 | Agent gRPC 端口 |
| role | NodeRole | 是 | 节点角色 |
| region | string | 否 | 区域标识 |
| zone | string | 否 | 可用区标识 |
| capacity | Resources | 是 | 节点资源容量 |
| supported_instance_types | string[] | 是 | 支持的实例类型 |

### 响应

**RegisterNodeResponse**

| 字段 | 类型 | 描述 |
|------|------|------|
| node_id | string | 分配的节点 ID |
| heartbeat_interval_seconds | int64 | 心跳间隔（秒） |
| lease_ttl_seconds | int64 | 租约 TTL（秒） |

### 示例

```bash
grpcurl -plaintext -d '{
  "hostname": "compute-node-1",
  "ip": "192.168.1.100",
  "port": 50052,
  "role": "NODE_ROLE_WORKER",
  "capacity": {
    "cpu_cores": 16,
    "memory_bytes": 68719476736,
    "disk_bytes": 536870912000
  },
  "supported_instance_types": ["vm", "container"]
}' localhost:50051 hypervisor.v1.ClusterService/RegisterNode
```

---

## Heartbeat

发送节点心跳，更新节点状态。

### 请求

**HeartbeatRequest**

| 字段 | 类型 | 必填 | 描述 |
|------|------|------|------|
| node_id | string | 是 | 节点 ID |
| status | NodeStatus | 是 | 当前状态 |
| conditions | NodeCondition[] | 否 | 健康条件 |
| allocated | Resources | 否 | 已分配资源 |

### 响应

**HeartbeatResponse**

| 字段 | 类型 | 描述 |
|------|------|------|
| accepted | bool | 心跳是否被接受 |
| next_heartbeat_seconds | int64 | 下次心跳时间 |

### 示例

```bash
grpcurl -plaintext -d '{
  "node_id": "node-abc123",
  "status": "NODE_STATUS_READY",
  "allocated": {
    "cpu_cores": 8,
    "memory_bytes": 34359738368
  }
}' localhost:50051 hypervisor.v1.ClusterService/Heartbeat
```

---

## ListNodes

列出集群中的所有节点。

### 请求

**ListNodesRequest**

| 字段 | 类型 | 必填 | 描述 |
|------|------|------|------|
| role | NodeRole | 否 | 按角色过滤 |
| status | NodeStatus | 否 | 按状态过滤 |
| region | string | 否 | 按区域过滤 |
| page_size | int32 | 否 | 每页数量 |
| page_token | string | 否 | 分页令牌 |

### 响应

**ListNodesResponse**

| 字段 | 类型 | 描述 |
|------|------|------|
| nodes | Node[] | 节点列表 |
| next_page_token | string | 下一页令牌 |
| total_count | int32 | 总数量 |

### 示例

```bash
grpcurl -plaintext -d '{
  "status": "NODE_STATUS_READY",
  "page_size": 10
}' localhost:50051 hypervisor.v1.ClusterService/ListNodes
```

---

## GetClusterInfo

获取集群整体信息。

### 响应

**ClusterInfo**

| 字段 | 类型 | 描述 |
|------|------|------|
| cluster_name | string | 集群名称 |
| total_nodes | int32 | 节点总数 |
| ready_nodes | int32 | 就绪节点数 |
| total_capacity | Resources | 总资源容量 |
| total_allocated | Resources | 已分配资源 |

### 示例

```bash
grpcurl -plaintext -d '{}' localhost:50051 hypervisor.v1.ClusterService/GetClusterInfo
```

---

## WatchNodes

监听节点变化事件（服务端流）。

### 请求

**WatchNodesRequest**

| 字段 | 类型 | 描述 |
|------|------|------|
| node_ids | string[] | 监听的节点 ID（空则监听所有） |

### 响应

**stream NodeEvent**

| 字段 | 类型 | 描述 |
|------|------|------|
| event_type | EventType | 事件类型 (ADDED/MODIFIED/DELETED) |
| node | Node | 节点信息 |
| timestamp | Timestamp | 事件时间 |

### 示例

```bash
grpcurl -plaintext -d '{}' localhost:50051 hypervisor.v1.ClusterService/WatchNodes
```

---

## 类型定义

### NodeRole

```protobuf
enum NodeRole {
  NODE_ROLE_UNSPECIFIED = 0;
  NODE_ROLE_MASTER = 1;
  NODE_ROLE_WORKER = 2;
}
```

### NodeStatus

```protobuf
enum NodeStatus {
  NODE_STATUS_UNSPECIFIED = 0;
  NODE_STATUS_PENDING = 1;
  NODE_STATUS_READY = 2;
  NODE_STATUS_NOT_READY = 3;
  NODE_STATUS_UNKNOWN = 4;
}
```

### Resources

```protobuf
message Resources {
  int32 cpu_cores = 1;
  int64 memory_bytes = 2;
  int64 disk_bytes = 3;
  int32 gpu_count = 4;
}
```

### Node

```protobuf
message Node {
  string id = 1;
  string hostname = 2;
  string ip = 3;
  int32 port = 4;
  NodeRole role = 5;
  NodeStatus status = 6;
  string region = 7;
  string zone = 8;
  Resources capacity = 9;
  Resources allocatable = 10;
  Resources allocated = 11;
  repeated string supported_instance_types = 12;
  repeated NodeCondition conditions = 13;
  Metadata metadata = 14;
  google.protobuf.Timestamp created_at = 15;
  google.protobuf.Timestamp last_seen = 16;
}
```
