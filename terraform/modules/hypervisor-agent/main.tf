# =============================================================================
# Hypervisor Agent (Compute Node) Module
# =============================================================================

variable "cluster_name" {
  description = "Name of the cluster"
  type        = string
}

variable "agent_count" {
  description = "Number of agent instances"
  type        = number
  default     = 3
}

variable "instance_type" {
  description = "Instance type for agent nodes"
  type        = string
}

variable "disk_size_gb" {
  description = "Disk size in GB"
  type        = number
  default     = 500
}

variable "disk_type" {
  description = "Disk type"
  type        = string
  default     = "gp3"
}

variable "subnet_ids" {
  description = "Subnet IDs for agent nodes"
  type        = list(string)
}

variable "security_group_ids" {
  description = "Security group IDs"
  type        = list(string)
}

variable "server_endpoint" {
  description = "Hypervisor server gRPC endpoint"
  type        = string
}

variable "etcd_endpoints" {
  description = "etcd cluster endpoints"
  type        = list(string)
}

variable "supported_instance_types" {
  description = "Supported instance types (vm, container, microvm)"
  type        = list(string)
  default     = ["vm", "container", "microvm"]
}

variable "hypervisor_version" {
  description = "Hypervisor software version"
  type        = string
  default     = "latest"
}

variable "agent_port" {
  description = "Agent gRPC port"
  type        = number
  default     = 50052
}

variable "region" {
  description = "Region name"
  type        = string
  default     = "default"
}

variable "log_level" {
  description = "Log level"
  type        = string
  default     = "info"
}

variable "ssh_key_name" {
  description = "SSH key pair name"
  type        = string
  default     = ""
}

variable "tags" {
  description = "Tags for resources"
  type        = map(string)
  default     = {}
}

# =============================================================================
# Data Sources
# =============================================================================

data "aws_ami" "ubuntu" {
  most_recent = true
  owners      = ["099720109477"]

  filter {
    name   = "name"
    values = ["ubuntu/images/hvm-ssd/ubuntu-jammy-22.04-amd64-server-*"]
  }

  filter {
    name   = "virtualization-type"
    values = ["hvm"]
  }
}

data "aws_subnet" "selected" {
  count = length(var.subnet_ids)
  id    = var.subnet_ids[count.index]
}

# =============================================================================
# Local Values
# =============================================================================

locals {
  zones = [for s in data.aws_subnet.selected : s.availability_zone]
}

# =============================================================================
# EC2 Instances
# =============================================================================

resource "aws_instance" "agent" {
  count = var.agent_count

  ami           = data.aws_ami.ubuntu.id
  instance_type = var.instance_type
  subnet_id     = element(var.subnet_ids, count.index % length(var.subnet_ids))
  key_name      = var.ssh_key_name != "" ? var.ssh_key_name : null

  vpc_security_group_ids = var.security_group_ids

  root_block_device {
    volume_size = var.disk_size_gb
    volume_type = var.disk_type
    encrypted   = true
  }

  user_data = templatefile("${path.module}/templates/agent-userdata.sh.tftpl", {
    hypervisor_version       = var.hypervisor_version
    server_endpoint          = var.server_endpoint
    etcd_endpoints           = join(",", var.etcd_endpoints)
    agent_port               = var.agent_port
    region                   = var.region
    zone                     = element(local.zones, count.index % length(local.zones))
    supported_instance_types = join(",", var.supported_instance_types)
    log_level                = var.log_level
    install_libvirt          = contains(var.supported_instance_types, "vm")
    install_containerd       = contains(var.supported_instance_types, "container")
    install_firecracker      = contains(var.supported_instance_types, "microvm")
  })

  tags = merge(var.tags, {
    Name = "${var.cluster_name}-agent-${count.index}"
    Role = "hypervisor-agent"
    Zone = element(local.zones, count.index % length(local.zones))
  })

  lifecycle {
    ignore_changes = [ami, user_data]
  }
}

# =============================================================================
# Outputs
# =============================================================================

output "instance_ids" {
  description = "Instance IDs"
  value       = aws_instance.agent[*].id
}

output "private_ips" {
  description = "Private IP addresses"
  value       = aws_instance.agent[*].private_ip
}

output "zones" {
  description = "Availability zones of agents"
  value       = [for i, inst in aws_instance.agent : element(local.zones, i % length(local.zones))]
}
