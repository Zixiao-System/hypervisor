# Hypervisor Ansible Automation

使用 Ansible 自动化部署和管理 Zixiao Hypervisor 集群。

## 目录结构

```
ansible/
├── ansible.cfg              # Ansible 配置
├── requirements.yml         # Galaxy 依赖
├── inventories/             # 清单文件
│   ├── dev/
│   ├── staging/
│   └── production/
├── playbooks/               # Playbook
│   ├── site.yml             # 完整部署
│   └── windows-drivers.yml  # Windows 驱动安装
└── roles/                   # Ansible Roles
    ├── common/              # 通用配置
    ├── etcd/                # etcd 集群
    ├── hypervisor-server/   # 控制平面
    ├── hypervisor-agent/    # 计算节点
    ├── libvirt/             # libvirt 配置
    ├── containerd/          # containerd 配置
    └── windows-drivers/     # Windows 驱动
```

## 快速开始

### 1. 安装依赖

```bash
# 安装 Ansible
pip install ansible

# 安装 Galaxy 依赖
ansible-galaxy install -r requirements.yml
```

### 2. 配置清单

```bash
# 编辑清单文件
vim inventories/dev/hosts.yml

# 编辑变量
vim inventories/dev/group_vars/all.yml
```

### 3. 部署集群

```bash
# 完整部署
ansible-playbook -i inventories/dev/hosts.yml playbooks/site.yml

# 仅部署 etcd
ansible-playbook -i inventories/dev/hosts.yml playbooks/site.yml --tags etcd

# 仅部署控制平面
ansible-playbook -i inventories/dev/hosts.yml playbooks/site.yml --tags server
```

### 4. 安装 Windows 驱动

```bash
ansible-playbook -i inventories/dev/hosts.yml playbooks/windows-drivers.yml
```

## 常用命令

```bash
# 测试连接
ansible all -i inventories/dev/hosts.yml -m ping

# 查看变量
ansible-inventory -i inventories/dev/hosts.yml --list

# Dry run
ansible-playbook -i inventories/dev/hosts.yml playbooks/site.yml --check

# 限制主机
ansible-playbook -i inventories/dev/hosts.yml playbooks/site.yml --limit agents
```

## 自定义变量

在 `inventories/<env>/group_vars/all.yml` 中配置：

```yaml
# 版本
hypervisor_version: "v0.1.0"
etcd_version: "v3.5.9"

# 集群配置
server_grpc_port: 50051
agent_grpc_port: 50052

# 实例类型
supported_instance_types:
  - vm
  - container
  - microvm

# TLS
tls_enabled: true
```
