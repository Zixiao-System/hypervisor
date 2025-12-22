# AgentService API

计算节点内部服务，由 hypervisor-agent 实现，供 hypervisor-server 调用。

## 服务概述

| 属性 | 值 |
|------|-----|
| 服务名称 | `AgentService` |
| Proto 文件 | `api/proto/agent.proto` |
| 包名 | `hypervisor.v1` |
| 调用方 | hypervisor-server |
| 实现方 | hypervisor-agent |

> **注意**: 此服务是内部服务，不对外暴露。Server 在调度决策后通过此服务操作 Agent 上的实例。

## 方法列表

| 方法 | 描述 | 请求类型 | 响应类型 |
|------|------|----------|----------|
| [CreateInstance](#createinstance) | 创建实例 | AgentCreateInstanceRequest | Instance |
| [DeleteInstance](#deleteinstance) | 删除实例 | AgentDeleteInstanceRequest | Empty |
| [StartInstance](#startinstance) | 启动实例 | AgentInstanceRequest | Instance |
| [StopInstance](#stopinstance) | 停止实例 | AgentStopInstanceRequest | Instance |
| [RestartInstance](#restartinstance) | 重启实例 | AgentRestartInstanceRequest | Instance |
| [GetInstance](#getinstance) | 获取实例 | AgentInstanceRequest | Instance |
| [ListInstances](#listinstances) | 列出实例 | Empty | AgentListInstancesResponse |
| [GetInstanceStats](#getinstancestats) | 获取统计 | AgentInstanceRequest | InstanceStats |
| [AttachConsole](#attachconsole) | 连接控制台 | stream AgentConsoleInput | stream AgentConsoleOutput |

---

## CreateInstance

在 Agent 节点上创建实例。Server 完成调度后调用此方法。

### 请求

**AgentCreateInstanceRequest**

| 字段 | 类型 | 必填 | 描述 |
|------|------|------|------|
| instance_id | string | 是 | 预分配的实例 ID |
| name | string | 是 | 实例名称 |
| type | InstanceType | 是 | 实例类型 |
| spec | InstanceSpec | 是 | 实例规格 |
| labels | map<string, string> | 否 | 标签 |
| annotations | map<string, string> | 否 | 注解 |

### 响应

返回创建的 **Instance** 对象。

### 示例

```bash
grpcurl -plaintext -d '{
  "instance_id": "inst-abc123",
  "name": "my-vm",
  "type": "INSTANCE_TYPE_VM",
  "spec": {
    "image": "ubuntu-22.04",
    "cpu_cores": 4,
    "memory_bytes": 8589934592,
    "disks": [
      {"name": "root", "size_bytes": 53687091200, "type": "ssd", "boot": true}
    ]
  },
  "labels": {"app": "web"}
}' 192.168.1.100:50052 hypervisor.v1.AgentService/CreateInstance
```

---

## DeleteInstance

删除 Agent 节点上的实例。

### 请求

**AgentDeleteInstanceRequest**

| 字段 | 类型 | 必填 | 描述 |
|------|------|------|------|
| instance_id | string | 是 | 实例 ID |
| force | bool | 否 | 强制删除（跳过优雅关闭） |

### 示例

```bash
grpcurl -plaintext -d '{
  "instance_id": "inst-abc123",
  "force": false
}' 192.168.1.100:50052 hypervisor.v1.AgentService/DeleteInstance
```

---

## StartInstance / StopInstance / RestartInstance

实例生命周期操作。

### StartInstance 请求

**AgentInstanceRequest**

| 字段 | 类型 | 描述 |
|------|------|------|
| instance_id | string | 实例 ID |

### StopInstance 请求

**AgentStopInstanceRequest**

| 字段 | 类型 | 描述 |
|------|------|------|
| instance_id | string | 实例 ID |
| force | bool | 强制停止（SIGKILL） |
| timeout_seconds | int32 | 优雅关闭超时时间 |

### RestartInstance 请求

**AgentRestartInstanceRequest**

| 字段 | 类型 | 描述 |
|------|------|------|
| instance_id | string | 实例 ID |
| force | bool | 强制重启 |

### 示例

```bash
# 启动
grpcurl -plaintext -d '{"instance_id": "inst-abc123"}' \
  192.168.1.100:50052 hypervisor.v1.AgentService/StartInstance

# 优雅停止（30秒超时）
grpcurl -plaintext -d '{
  "instance_id": "inst-abc123",
  "force": false,
  "timeout_seconds": 30
}' 192.168.1.100:50052 hypervisor.v1.AgentService/StopInstance

# 强制重启
grpcurl -plaintext -d '{
  "instance_id": "inst-abc123",
  "force": true
}' 192.168.1.100:50052 hypervisor.v1.AgentService/RestartInstance
```

---

## GetInstance

获取单个实例的详细信息。

### 请求

**AgentInstanceRequest**

| 字段 | 类型 | 描述 |
|------|------|------|
| instance_id | string | 实例 ID |

### 响应

返回 **Instance** 对象。

### 示例

```bash
grpcurl -plaintext -d '{"instance_id": "inst-abc123"}' \
  192.168.1.100:50052 hypervisor.v1.AgentService/GetInstance
```

---

## ListInstances

列出 Agent 节点上的所有实例。

### 请求

无参数（使用 `google.protobuf.Empty`）。

### 响应

**AgentListInstancesResponse**

| 字段 | 类型 | 描述 |
|------|------|------|
| instances | Instance[] | 实例列表 |

### 示例

```bash
grpcurl -plaintext -d '{}' \
  192.168.1.100:50052 hypervisor.v1.AgentService/ListInstances
```

---

## GetInstanceStats

获取实例资源使用统计。

### 请求

**AgentInstanceRequest**

| 字段 | 类型 | 描述 |
|------|------|------|
| instance_id | string | 实例 ID |

### 响应

**InstanceStats**

| 字段 | 类型 | 描述 |
|------|------|------|
| instance_id | string | 实例 ID |
| cpu_usage_percent | float | CPU 使用率 (0-100) |
| memory_usage_bytes | int64 | 内存使用量 |
| disk_read_bytes | int64 | 磁盘读取总量 |
| disk_write_bytes | int64 | 磁盘写入总量 |
| network_rx_bytes | int64 | 网络接收总量 |
| network_tx_bytes | int64 | 网络发送总量 |

### 示例

```bash
grpcurl -plaintext -d '{"instance_id": "inst-abc123"}' \
  192.168.1.100:50052 hypervisor.v1.AgentService/GetInstanceStats
```

---

## AttachConsole

连接到实例控制台（双向流式 RPC）。

### 请求

**stream AgentConsoleInput**

| 字段 | 类型 | 描述 |
|------|------|------|
| data | bytes | 终端输入数据 |
| resize | AgentConsoleResize | 终端大小调整 |

> 注意: `data` 和 `resize` 是 `oneof` 关系，每条消息只包含其中一个。

**AgentConsoleResize**

| 字段 | 类型 | 描述 |
|------|------|------|
| width | int32 | 终端宽度（列） |
| height | int32 | 终端高度（行） |

### 响应

**stream AgentConsoleOutput**

| 字段 | 类型 | 描述 |
|------|------|------|
| data | bytes | 终端输出数据 |

### 调用流程

1. 客户端首先发送初始消息（可包含 resize）
2. 客户端持续发送键盘输入数据
3. Agent 持续返回终端输出
4. 当终端大小变化时，客户端发送 resize 消息
5. 任一端关闭连接结束会话

---

## 与 ComputeService 的关系

```
用户请求 → ComputeService → 调度器 → AgentService
                ↑                           ↓
                └────── 状态同步 ←──────────┘
```

| 操作 | ComputeService | AgentService |
|------|----------------|--------------|
| 创建实例 | 接收请求、调度、记录状态 | 实际创建实例 |
| 启动实例 | 更新期望状态 | 执行启动命令 |
| 获取统计 | 聚合多节点数据 | 收集本地数据 |
| 控制台 | 代理连接 | 直接连接实例 |

---

## 类型定义

### InstanceType

```protobuf
enum InstanceType {
  INSTANCE_TYPE_UNSPECIFIED = 0;
  INSTANCE_TYPE_VM = 1;
  INSTANCE_TYPE_CONTAINER = 2;
  INSTANCE_TYPE_MICROVM = 3;
}
```

### Instance

```protobuf
message Instance {
  string id = 1;
  string name = 2;
  InstanceType type = 3;
  InstanceState state = 4;
  InstanceSpec spec = 5;
  string node_id = 6;
  string ip_address = 7;
  Metadata metadata = 8;
  string error_message = 9;
  google.protobuf.Timestamp created_at = 10;
  google.protobuf.Timestamp updated_at = 11;
}
```

### InstanceStats

```protobuf
message InstanceStats {
  string instance_id = 1;
  float cpu_usage_percent = 2;
  int64 memory_usage_bytes = 3;
  int64 disk_read_bytes = 4;
  int64 disk_write_bytes = 5;
  int64 network_rx_bytes = 6;
  int64 network_tx_bytes = 7;
}
```
