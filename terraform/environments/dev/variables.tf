# =============================================================================
# Development Environment Variables
# =============================================================================

variable "project_name" {
  description = "Project name"
  type        = string
  default     = "hypervisor"
}

variable "environment" {
  description = "Environment name"
  type        = string
  default     = "dev"
}

variable "region" {
  description = "AWS region"
  type        = string
  default     = "us-west-2"
}

variable "zones" {
  description = "Availability zones"
  type        = list(string)
  default     = ["us-west-2a", "us-west-2b", "us-west-2c"]
}

variable "vpc_cidr" {
  description = "VPC CIDR"
  type        = string
  default     = "10.0.0.0/16"
}

variable "subnet_cidrs" {
  description = "Subnet CIDRs"
  type = object({
    control_plane = string
    compute       = string
    storage       = string
  })
  default = {
    control_plane = "10.0.1.0/24"
    compute       = "10.0.2.0/24"
    storage       = "10.0.3.0/24"
  }
}

variable "enable_nat_gateway" {
  description = "Enable NAT Gateway"
  type        = bool
  default     = true
}

variable "etcd_cluster_size" {
  description = "etcd cluster size"
  type        = number
  default     = 3
}

variable "server_count" {
  description = "Server count"
  type        = number
  default     = 2
}

variable "agent_count" {
  description = "Agent count"
  type        = number
  default     = 3
}

variable "agent_instance_types" {
  description = "Supported instance types"
  type        = list(string)
  default     = ["vm", "container", "microvm"]
}

variable "instance_specs" {
  description = "Instance specifications"
  type = object({
    etcd = object({
      instance_type = string
      disk_size_gb  = number
    })
    server = object({
      instance_type = string
      disk_size_gb  = number
    })
    agent = object({
      instance_type = string
      disk_size_gb  = number
    })
  })
  default = {
    etcd = {
      instance_type = "t3.medium"
      disk_size_gb  = 50
    }
    server = {
      instance_type = "t3.large"
      disk_size_gb  = 100
    }
    agent = {
      instance_type = "m5.2xlarge"
      disk_size_gb  = 500
    }
  }
}

variable "hypervisor_version" {
  description = "Hypervisor version"
  type        = string
  default     = "latest"
}

variable "etcd_version" {
  description = "etcd version"
  type        = string
  default     = "v3.5.9"
}

variable "ssh_key_name" {
  description = "SSH key name"
  type        = string
  default     = ""
}

variable "tls_enabled" {
  description = "Enable TLS"
  type        = bool
  default     = false
}

variable "log_level" {
  description = "Log level"
  type        = string
  default     = "info"
}

variable "common_tags" {
  description = "Common tags"
  type        = map(string)
  default     = {}
}
