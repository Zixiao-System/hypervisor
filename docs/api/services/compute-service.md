# ComputeService API

计算资源管理服务，用于实例（VM/Container/MicroVM）的生命周期管理。

## 服务概述

| 属性 | 值 |
|------|-----|
| 服务名称 | `ComputeService` |
| Proto 文件 | `api/proto/compute.proto` |
| 包名 | `hypervisor.v1` |

## 方法列表

| 方法 | 描述 | 请求类型 | 响应类型 |
|------|------|----------|----------|
| [CreateInstance](#createinstance) | 创建实例 | CreateInstanceRequest | Instance |
| [DeleteInstance](#deleteinstance) | 删除实例 | DeleteInstanceRequest | Empty |
| [GetInstance](#getinstance) | 获取实例详情 | GetInstanceRequest | Instance |
| [ListInstances](#listinstances) | 列出实例 | ListInstancesRequest | ListInstancesResponse |
| [StartInstance](#startinstance) | 启动实例 | StartInstanceRequest | Instance |
| [StopInstance](#stopinstance) | 停止实例 | StopInstanceRequest | Instance |
| [RestartInstance](#restartinstance) | 重启实例 | RestartInstanceRequest | Instance |
| [GetInstanceStats](#getinstancestats) | 获取实例统计 | GetInstanceStatsRequest | InstanceStats |
| [WatchInstance](#watchinstance) | 监听实例变化 | WatchInstanceRequest | stream InstanceEvent |
| [AttachConsole](#attachconsole) | 连接控制台 | stream ConsoleInput | stream ConsoleOutput |
| [ListImages](#listimages) | 列出镜像 | ListImagesRequest | ListImagesResponse |
| [PullImage](#pullimage) | 拉取镜像 | PullImageRequest | stream PullImageProgress |

---

## CreateInstance

创建新的计算实例。

### 请求

**CreateInstanceRequest**

| 字段 | 类型 | 必填 | 描述 |
|------|------|------|------|
| name | string | 是 | 实例名称 |
| type | InstanceType | 是 | 实例类型 |
| spec | InstanceSpec | 是 | 实例规格 |
| metadata | Metadata | 否 | 自定义元数据 |
| preferred_node_id | string | 否 | 首选节点 |
| region | string | 否 | 目标区域 |
| zone | string | 否 | 目标可用区 |

**InstanceSpec**

| 字段 | 类型 | 描述 |
|------|------|------|
| image | string | 镜像名称或 URL |
| cpu_cores | int32 | CPU 核心数 |
| memory_bytes | int64 | 内存大小 |
| disks | DiskSpec[] | 磁盘配置 |
| network | NetworkSpec | 网络配置 |
| kernel | string | 内核路径（VM/MicroVM） |
| command | string[] | 容器命令 |
| env | map<string, string> | 环境变量 |

### 响应

返回创建的 **Instance** 对象。

### 示例

**创建虚拟机**

```bash
grpcurl -plaintext -d '{
  "name": "my-ubuntu-vm",
  "type": "INSTANCE_TYPE_VM",
  "spec": {
    "image": "ubuntu-22.04",
    "cpu_cores": 4,
    "memory_bytes": 8589934592,
    "disks": [
      {
        "name": "root",
        "size_bytes": 53687091200,
        "type": "ssd",
        "boot": true
      }
    ],
    "network": {
      "network_id": "default"
    }
  }
}' localhost:50051 hypervisor.v1.ComputeService/CreateInstance
```

**创建容器**

```bash
grpcurl -plaintext -d '{
  "name": "nginx-container",
  "type": "INSTANCE_TYPE_CONTAINER",
  "spec": {
    "image": "nginx:latest",
    "cpu_cores": 1,
    "memory_bytes": 536870912,
    "command": ["nginx", "-g", "daemon off;"]
  }
}' localhost:50051 hypervisor.v1.ComputeService/CreateInstance
```

---

## StartInstance / StopInstance / RestartInstance

实例生命周期操作。

### 请求

| 字段 | 类型 | 描述 |
|------|------|------|
| instance_id | string | 实例 ID |
| force | bool | 强制操作（仅 Stop） |

### 示例

```bash
# 启动
grpcurl -plaintext -d '{"instance_id": "inst-xyz789"}' \
  localhost:50051 hypervisor.v1.ComputeService/StartInstance

# 停止
grpcurl -plaintext -d '{"instance_id": "inst-xyz789", "force": false}' \
  localhost:50051 hypervisor.v1.ComputeService/StopInstance

# 重启
grpcurl -plaintext -d '{"instance_id": "inst-xyz789"}' \
  localhost:50051 hypervisor.v1.ComputeService/RestartInstance
```

---

## ListInstances

列出实例。

### 请求

**ListInstancesRequest**

| 字段 | 类型 | 描述 |
|------|------|------|
| type | InstanceType | 按类型过滤 |
| state | InstanceState | 按状态过滤 |
| node_id | string | 按节点过滤 |
| page_size | int32 | 每页数量 |
| page_token | string | 分页令牌 |

### 示例

```bash
grpcurl -plaintext -d '{
  "type": "INSTANCE_TYPE_VM",
  "state": "INSTANCE_STATE_RUNNING",
  "page_size": 20
}' localhost:50051 hypervisor.v1.ComputeService/ListInstances
```

---

## GetInstanceStats

获取实例资源使用统计。

### 响应

**InstanceStats**

| 字段 | 类型 | 描述 |
|------|------|------|
| instance_id | string | 实例 ID |
| cpu_usage_percent | float | CPU 使用率 |
| memory_usage_bytes | int64 | 内存使用量 |
| disk_read_bytes | int64 | 磁盘读取量 |
| disk_write_bytes | int64 | 磁盘写入量 |
| network_rx_bytes | int64 | 网络接收量 |
| network_tx_bytes | int64 | 网络发送量 |

### 示例

```bash
grpcurl -plaintext -d '{"instance_id": "inst-xyz789"}' \
  localhost:50051 hypervisor.v1.ComputeService/GetInstanceStats
```

---

## AttachConsole

连接到实例控制台（双向流式 RPC）。

### 请求

**stream ConsoleInput**

| 字段 | 类型 | 描述 |
|------|------|------|
| instance_id | string | 实例 ID（首次消息） |
| data | bytes | 输入数据 |
| resize | TerminalSize | 终端大小调整 |

### 响应

**stream ConsoleOutput**

| 字段 | 类型 | 描述 |
|------|------|------|
| data | bytes | 输出数据 |

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

### InstanceState

```protobuf
enum InstanceState {
  INSTANCE_STATE_UNSPECIFIED = 0;
  INSTANCE_STATE_CREATING = 1;
  INSTANCE_STATE_RUNNING = 2;
  INSTANCE_STATE_STOPPED = 3;
  INSTANCE_STATE_STOPPING = 4;
  INSTANCE_STATE_STARTING = 5;
  INSTANCE_STATE_RESTARTING = 6;
  INSTANCE_STATE_ERROR = 7;
  INSTANCE_STATE_DELETED = 8;
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
