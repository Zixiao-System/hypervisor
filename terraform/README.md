# Hypervisor Terraform Infrastructure

使用 Terraform 自动化部署 Zixiao Hypervisor 集群。

## 目录结构

```
terraform/
├── versions.tf                # Terraform 和 Provider 版本约束
├── variables.tf               # 全局变量定义
├── outputs.tf                 # 输出定义
├── terraform.tfvars.example   # 变量示例
│
├── modules/                   # 可复用模块
│   ├── etcd-cluster/          # etcd 集群模块
│   ├── hypervisor-server/     # 控制平面模块
│   ├── hypervisor-agent/      # 计算节点模块
│   ├── network/               # 网络模块
│   └── security/              # 安全组模块
│
└── environments/              # 环境配置
    ├── dev/                   # 开发环境
    ├── staging/               # 预发布环境
    └── production/            # 生产环境
```

## 快速开始

### 1. 前置条件

- Terraform >= 1.6.0
- 云服务商凭证（AWS/Azure/GCP）或本地 libvirt

### 2. 配置环境

```bash
cd terraform/environments/dev

# 复制并编辑变量文件
cp ../../terraform.tfvars.example terraform.tfvars
vim terraform.tfvars

# 配置后端存储（可选）
vim backend.tf
```

### 3. 初始化和部署

```bash
# 初始化 Terraform
terraform init

# 预览变更
terraform plan

# 应用变更
terraform apply
```

### 4. 查看输出

```bash
terraform output
```

## 模块说明

### etcd-cluster

部署高可用 etcd 集群。

```hcl
module "etcd" {
  source = "../../modules/etcd-cluster"

  cluster_name       = "hypervisor-dev"
  node_count         = 3
  instance_type      = "t3.medium"
  disk_size_gb       = 50
  subnet_ids         = module.network.private_subnet_ids
  security_group_ids = [module.security.etcd_security_group_id]
  etcd_version       = "v3.5.9"
}
```

### hypervisor-server

部署控制平面（hypervisor-server）。

```hcl
module "server" {
  source = "../../modules/hypervisor-server"

  cluster_name       = "hypervisor-dev"
  server_count       = 2
  instance_type      = "t3.large"
  subnet_ids         = module.network.private_subnet_ids
  security_group_ids = [module.security.server_security_group_id]
  etcd_endpoints     = module.etcd.endpoints
  hypervisor_version = "v0.1.0"
}
```

### hypervisor-agent

部署计算节点（hypervisor-agent）。

```hcl
module "agent" {
  source = "../../modules/hypervisor-agent"

  cluster_name             = "hypervisor-dev"
  agent_count              = 3
  instance_type            = "m5.2xlarge"
  subnet_ids               = module.network.private_subnet_ids
  security_group_ids       = [module.security.agent_security_group_id]
  server_endpoint          = module.server.grpc_endpoint
  etcd_endpoints           = module.etcd.endpoints
  supported_instance_types = ["vm", "container", "microvm"]
}
```

## 多云支持

### AWS

```hcl
provider "aws" {
  region = var.region
}
```

### Azure

```hcl
provider "azurerm" {
  features {}
}
```

### GCP

```hcl
provider "google" {
  project = var.gcp_project
  region  = var.region
}
```

### 本地 libvirt

```hcl
provider "libvirt" {
  uri = "qemu:///system"
}
```

## 环境配置

### 开发环境 (dev)

- 最小资源配置
- 单可用区
- 禁用高可用特性

### 预发布环境 (staging)

- 生产级配置的缩小版
- 多可用区
- 启用监控

### 生产环境 (production)

- 完整高可用配置
- 多可用区
- 启用所有安全特性
- 配置备份策略

## 状态管理

### S3 后端 (AWS)

```hcl
terraform {
  backend "s3" {
    bucket         = "hypervisor-terraform-state"
    key            = "dev/terraform.tfstate"
    region         = "us-west-2"
    encrypt        = true
    dynamodb_table = "hypervisor-terraform-locks"
  }
}
```

### Azure Storage 后端

```hcl
terraform {
  backend "azurerm" {
    resource_group_name  = "terraform-state"
    storage_account_name = "hypervisorstate"
    container_name       = "tfstate"
    key                  = "dev/terraform.tfstate"
  }
}
```

## 安全最佳实践

1. **状态文件加密**：始终加密远程状态存储
2. **最小权限**：使用最小必要的 IAM 权限
3. **Secrets 管理**：使用 Vault 或云 Secrets Manager
4. **审计日志**：启用 CloudTrail/Azure Monitor

## 常用命令

```bash
# 格式化代码
terraform fmt -recursive

# 验证配置
terraform validate

# 查看资源
terraform state list

# 销毁资源
terraform destroy
```

## 故障排除

### 状态锁定

```bash
# 强制解锁（谨慎使用）
terraform force-unlock LOCK_ID
```

### 资源导入

```bash
# 导入现有资源
terraform import module.etcd.aws_instance.etcd[0] i-1234567890abcdef0
```
