# =============================================================================
# etcd Cluster Module
# =============================================================================

variable "cluster_name" {
  description = "Name of the etcd cluster"
  type        = string
}

variable "node_count" {
  description = "Number of etcd nodes"
  type        = number
  default     = 3
}

variable "instance_type" {
  description = "Instance type for etcd nodes"
  type        = string
}

variable "disk_size_gb" {
  description = "Disk size in GB"
  type        = number
  default     = 50
}

variable "disk_type" {
  description = "Disk type (gp3, gp2, io1, etc.)"
  type        = string
  default     = "gp3"
}

variable "subnet_ids" {
  description = "Subnet IDs for etcd nodes"
  type        = list(string)
}

variable "security_group_ids" {
  description = "Security group IDs"
  type        = list(string)
}

variable "etcd_version" {
  description = "etcd version"
  type        = string
  default     = "v3.5.9"
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
  owners      = ["099720109477"] # Canonical

  filter {
    name   = "name"
    values = ["ubuntu/images/hvm-ssd/ubuntu-jammy-22.04-amd64-server-*"]
  }

  filter {
    name   = "virtualization-type"
    values = ["hvm"]
  }
}

# =============================================================================
# Local Values
# =============================================================================

locals {
  # Build initial cluster configuration
  initial_cluster = join(",", [
    for i in range(var.node_count) :
    "etcd-${i}=http://${cidrhost(data.aws_subnet.selected[i % length(var.subnet_ids)].cidr_block, 10 + i)}:2380"
  ])
}

data "aws_subnet" "selected" {
  count = length(var.subnet_ids)
  id    = var.subnet_ids[count.index]
}

# =============================================================================
# EC2 Instances
# =============================================================================

resource "aws_instance" "etcd" {
  count = var.node_count

  ami           = data.aws_ami.ubuntu.id
  instance_type = var.instance_type
  subnet_id     = element(var.subnet_ids, count.index % length(var.subnet_ids))
  key_name      = var.ssh_key_name != "" ? var.ssh_key_name : null

  vpc_security_group_ids = var.security_group_ids

  root_block_device {
    volume_size = var.disk_size_gb
    volume_type = var.disk_type
    encrypted   = true

    tags = merge(var.tags, {
      Name = "${var.cluster_name}-etcd-${count.index}-root"
    })
  }

  user_data = templatefile("${path.module}/templates/etcd-userdata.sh.tftpl", {
    etcd_version    = var.etcd_version
    cluster_name    = var.cluster_name
    node_name       = "etcd-${count.index}"
    node_index      = count.index
    initial_cluster = local.initial_cluster
  })

  tags = merge(var.tags, {
    Name = "${var.cluster_name}-etcd-${count.index}"
    Role = "etcd"
  })

  lifecycle {
    ignore_changes = [ami, user_data]
  }
}

# =============================================================================
# Outputs
# =============================================================================

output "endpoints" {
  description = "etcd cluster endpoints"
  value       = [for instance in aws_instance.etcd : "${instance.private_ip}:2379"]
}

output "endpoints_string" {
  description = "etcd cluster endpoints as comma-separated string"
  value       = join(",", [for instance in aws_instance.etcd : "${instance.private_ip}:2379"])
}

output "instance_ids" {
  description = "Instance IDs of etcd nodes"
  value       = aws_instance.etcd[*].id
}

output "private_ips" {
  description = "Private IP addresses of etcd nodes"
  value       = aws_instance.etcd[*].private_ip
}
