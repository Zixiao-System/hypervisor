# =============================================================================
# Global Variables
# =============================================================================

variable "project_name" {
  description = "Project name used for resource naming and tagging"
  type        = string
  default     = "hypervisor"
}

variable "environment" {
  description = "Environment name (dev, staging, production)"
  type        = string
  validation {
    condition     = contains(["dev", "staging", "production"], var.environment)
    error_message = "Environment must be dev, staging, or production."
  }
}

variable "region" {
  description = "Primary region for deployment"
  type        = string
}

variable "zones" {
  description = "Availability zones for deployment"
  type        = list(string)
  default     = []
}

# =============================================================================
# Network Configuration
# =============================================================================

variable "vpc_cidr" {
  description = "CIDR block for VPC/VNet"
  type        = string
  default     = "10.0.0.0/16"
}

variable "subnet_cidrs" {
  description = "CIDR blocks for subnets"
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
  description = "Enable NAT Gateway for private subnets"
  type        = bool
  default     = true
}

# =============================================================================
# Cluster Configuration
# =============================================================================

variable "etcd_cluster_size" {
  description = "Number of etcd nodes (must be odd number)"
  type        = number
  default     = 3
  validation {
    condition     = var.etcd_cluster_size >= 1 && var.etcd_cluster_size % 2 == 1
    error_message = "etcd cluster size must be an odd number >= 1."
  }
}

variable "server_count" {
  description = "Number of hypervisor-server instances"
  type        = number
  default     = 2
}

variable "agent_count" {
  description = "Number of hypervisor-agent (compute) nodes"
  type        = number
  default     = 3
}

variable "agent_instance_types" {
  description = "Supported instance types on agents"
  type        = list(string)
  default     = ["vm", "container", "microvm"]
  validation {
    condition     = length(var.agent_instance_types) > 0
    error_message = "At least one instance type must be specified."
  }
}

# =============================================================================
# Instance Specifications
# =============================================================================

variable "instance_specs" {
  description = "Instance specifications for each component"
  type = object({
    etcd = object({
      instance_type = string
      disk_size_gb  = number
      disk_type     = optional(string, "gp3")
    })
    server = object({
      instance_type = string
      disk_size_gb  = number
      disk_type     = optional(string, "gp3")
    })
    agent = object({
      instance_type = string
      disk_size_gb  = number
      disk_type     = optional(string, "gp3")
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

# =============================================================================
# Version Configuration
# =============================================================================

variable "hypervisor_version" {
  description = "Hypervisor software version to deploy"
  type        = string
  default     = "latest"
}

variable "etcd_version" {
  description = "etcd version"
  type        = string
  default     = "v3.5.9"
}

# =============================================================================
# SSH Configuration
# =============================================================================

variable "ssh_public_key" {
  description = "SSH public key for instance access"
  type        = string
  default     = ""
}

variable "ssh_key_name" {
  description = "Name of existing SSH key pair (AWS)"
  type        = string
  default     = ""
}

# =============================================================================
# TLS Configuration
# =============================================================================

variable "tls_enabled" {
  description = "Enable TLS for all services"
  type        = bool
  default     = false
}

variable "tls_cert_path" {
  description = "Path to TLS certificate files (if using external certs)"
  type        = string
  default     = ""
}

# =============================================================================
# Logging and Monitoring
# =============================================================================

variable "log_level" {
  description = "Log level for all services"
  type        = string
  default     = "info"
  validation {
    condition     = contains(["debug", "info", "warn", "error"], var.log_level)
    error_message = "Log level must be debug, info, warn, or error."
  }
}

variable "enable_monitoring" {
  description = "Enable Prometheus/Grafana monitoring stack"
  type        = bool
  default     = false
}

# =============================================================================
# Tags
# =============================================================================

variable "common_tags" {
  description = "Common tags applied to all resources"
  type        = map(string)
  default     = {}
}
