# NetworkService API

SDN 网络管理服务，提供完整的虚拟网络管理功能。

## 服务概述

| 属性 | 值 |
|------|-----|
| 服务名称 | `NetworkService` |
| Proto 文件 | `api/proto/network.proto` |
| 包名 | `hypervisor.v1` |

## 方法分组

### 网络管理

| 方法 | 描述 |
|------|------|
| CreateNetwork | 创建虚拟网络 |
| GetNetwork | 获取网络详情 |
| ListNetworks | 列出网络 |
| DeleteNetwork | 删除网络 |

### 子网管理

| 方法 | 描述 |
|------|------|
| CreateSubnet | 创建子网 |
| GetSubnet | 获取子网详情 |
| ListSubnets | 列出子网 |
| DeleteSubnet | 删除子网 |

### IP 地址管理

| 方法 | 描述 |
|------|------|
| AllocateIP | 分配 IP 地址 |
| ReleaseIP | 释放 IP 地址 |
| ListAllocations | 列出 IP 分配 |

### 端口管理

| 方法 | 描述 |
|------|------|
| CreatePort | 创建端口 |
| GetPort | 获取端口详情 |
| ListPorts | 列出端口 |
| DeletePort | 删除端口 |
| BindPort | 绑定端口到实例 |
| UnbindPort | 解绑端口 |

### 安全组管理

| 方法 | 描述 |
|------|------|
| CreateSecurityGroup | 创建安全组 |
| GetSecurityGroup | 获取安全组详情 |
| ListSecurityGroups | 列出安全组 |
| DeleteSecurityGroup | 删除安全组 |
| AddSecurityRule | 添加安全规则 |
| RemoveSecurityRule | 删除安全规则 |

### 路由器管理

| 方法 | 描述 |
|------|------|
| CreateRouter | 创建路由器 |
| GetRouter | 获取路由器详情 |
| ListRouters | 列出路由器 |
| DeleteRouter | 删除路由器 |
| AddRouterInterface | 添加路由器接口 |
| RemoveRouterInterface | 删除路由器接口 |
| AddRoute | 添加路由 |
| RemoveRoute | 删除路由 |

### 浮动 IP 管理

| 方法 | 描述 |
|------|------|
| CreateFloatingIP | 创建浮动 IP |
| AssociateFloatingIP | 关联浮动 IP |
| DisassociateFloatingIP | 解除关联 |
| DeleteFloatingIP | 删除浮动 IP |
| ListFloatingIPs | 列出浮动 IP |

### VTEP 管理

| 方法 | 描述 |
|------|------|
| ListVTEPs | 列出 VTEP 节点 |

---

## CreateNetwork

创建虚拟网络。

### 请求

**CreateNetworkRequest**

| 字段 | 类型 | 必填 | 描述 |
|------|------|------|------|
| name | string | 是 | 网络名称 |
| type | NetworkType | 是 | 网络类型 |
| vni | int32 | 否 | VXLAN VNI |
| vlan_id | int32 | 否 | VLAN ID |
| mtu | int32 | 否 | MTU 大小 |
| metadata | Metadata | 否 | 元数据 |

### NetworkType

```protobuf
enum NetworkType {
  NETWORK_TYPE_UNSPECIFIED = 0;
  NETWORK_TYPE_VXLAN = 1;
  NETWORK_TYPE_VLAN = 2;
  NETWORK_TYPE_FLAT = 3;
  NETWORK_TYPE_BRIDGE = 4;
}
```

### 示例

```bash
grpcurl -plaintext -d '{
  "name": "tenant-network",
  "type": "NETWORK_TYPE_VXLAN",
  "vni": 1001,
  "mtu": 1450
}' localhost:50051 hypervisor.v1.NetworkService/CreateNetwork
```

---

## CreateSubnet

创建子网。

### 请求

**CreateSubnetRequest**

| 字段 | 类型 | 必填 | 描述 |
|------|------|------|------|
| network_id | string | 是 | 所属网络 ID |
| name | string | 是 | 子网名称 |
| cidr | string | 是 | CIDR 块 |
| gateway_ip | string | 否 | 网关 IP |
| enable_dhcp | bool | 否 | 启用 DHCP |
| dns_nameservers | string[] | 否 | DNS 服务器 |
| allocation_pools | AllocationPool[] | 否 | IP 分配池 |

### 示例

```bash
grpcurl -plaintext -d '{
  "network_id": "net-abc123",
  "name": "web-subnet",
  "cidr": "10.0.1.0/24",
  "gateway_ip": "10.0.1.1",
  "enable_dhcp": true,
  "dns_nameservers": ["8.8.8.8", "8.8.4.4"],
  "allocation_pools": [
    {"start": "10.0.1.10", "end": "10.0.1.254"}
  ]
}' localhost:50051 hypervisor.v1.NetworkService/CreateSubnet
```

---

## CreateSecurityGroup

创建安全组。

### 请求

**CreateSecurityGroupRequest**

| 字段 | 类型 | 必填 | 描述 |
|------|------|------|------|
| name | string | 是 | 安全组名称 |
| description | string | 否 | 描述 |
| rules | SecurityRule[] | 否 | 初始规则 |

**SecurityRule**

| 字段 | 类型 | 描述 |
|------|------|------|
| direction | Direction | 方向 (INGRESS/EGRESS) |
| protocol | Protocol | 协议 (TCP/UDP/ICMP/ANY) |
| port_range_min | int32 | 端口范围起始 |
| port_range_max | int32 | 端口范围结束 |
| remote_ip_prefix | string | 远程 IP CIDR |
| action | Action | 动作 (ALLOW/DENY) |

### 示例

```bash
grpcurl -plaintext -d '{
  "name": "web-security-group",
  "description": "Allow HTTP/HTTPS traffic",
  "rules": [
    {
      "direction": "INGRESS",
      "protocol": "TCP",
      "port_range_min": 80,
      "port_range_max": 80,
      "remote_ip_prefix": "0.0.0.0/0",
      "action": "ALLOW"
    },
    {
      "direction": "INGRESS",
      "protocol": "TCP",
      "port_range_min": 443,
      "port_range_max": 443,
      "remote_ip_prefix": "0.0.0.0/0",
      "action": "ALLOW"
    }
  ]
}' localhost:50051 hypervisor.v1.NetworkService/CreateSecurityGroup
```

---

## CreateFloatingIP

创建浮动 IP。

### 请求

**CreateFloatingIPRequest**

| 字段 | 类型 | 必填 | 描述 |
|------|------|------|------|
| floating_network_id | string | 是 | 外部网络 ID |
| floating_ip_address | string | 否 | 指定 IP（可选） |
| description | string | 否 | 描述 |

### 响应

**FloatingIP**

| 字段 | 类型 | 描述 |
|------|------|------|
| id | string | 浮动 IP ID |
| floating_ip_address | string | 浮动 IP 地址 |
| fixed_ip_address | string | 关联的固定 IP |
| port_id | string | 关联的端口 ID |
| status | string | 状态 |

### 示例

```bash
# 创建浮动 IP
grpcurl -plaintext -d '{
  "floating_network_id": "external-net"
}' localhost:50051 hypervisor.v1.NetworkService/CreateFloatingIP

# 关联到端口
grpcurl -plaintext -d '{
  "floating_ip_id": "fip-xyz789",
  "port_id": "port-abc123"
}' localhost:50051 hypervisor.v1.NetworkService/AssociateFloatingIP
```

---

## 类型定义

### Network

```protobuf
message Network {
  string id = 1;
  string name = 2;
  NetworkType type = 3;
  int32 vni = 4;
  int32 vlan_id = 5;
  int32 mtu = 6;
  NetworkStatus status = 7;
  repeated string subnet_ids = 8;
  Metadata metadata = 9;
  google.protobuf.Timestamp created_at = 10;
}
```

### Subnet

```protobuf
message Subnet {
  string id = 1;
  string network_id = 2;
  string name = 3;
  string cidr = 4;
  string gateway_ip = 5;
  bool enable_dhcp = 6;
  repeated string dns_nameservers = 7;
  repeated AllocationPool allocation_pools = 8;
  Metadata metadata = 9;
}
```

### Port

```protobuf
message Port {
  string id = 1;
  string network_id = 2;
  string subnet_id = 3;
  string mac_address = 4;
  repeated string fixed_ips = 5;
  string device_id = 6;
  string device_owner = 7;
  repeated string security_group_ids = 8;
  PortStatus status = 9;
  BindingType binding_type = 10;
}
```

### SecurityGroup

```protobuf
message SecurityGroup {
  string id = 1;
  string name = 2;
  string description = 3;
  repeated SecurityRule rules = 4;
  Metadata metadata = 5;
}
```

### Router

```protobuf
message Router {
  string id = 1;
  string name = 2;
  bool dvr_enabled = 3;
  string external_gateway_network_id = 4;
  repeated RouterInterface interfaces = 5;
  repeated Route routes = 6;
  RouterStatus status = 7;
}
```
