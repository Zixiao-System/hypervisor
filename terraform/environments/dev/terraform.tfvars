# Development Environment Configuration

# Project settings
project_name = "hypervisor"
environment  = "dev"
region       = "us-west-2"
zones        = ["us-west-2a", "us-west-2b"]

# Minimal resource configuration for development
etcd_cluster_size = 1
server_count      = 1
agent_count       = 2

instance_specs = {
  etcd = {
    instance_type = "t3.small"
    disk_size_gb  = 20
  }
  server = {
    instance_type = "t3.medium"
    disk_size_gb  = 50
  }
  agent = {
    instance_type = "t3.large"
    disk_size_gb  = 100
  }
}

# Only support containers for dev (lower resource usage)
agent_instance_types = ["container"]

# Disable NAT Gateway to save costs
enable_nat_gateway = false

# Logging
log_level = "debug"

# Tags
common_tags = {
  Team        = "Platform"
  CostCenter  = "Development"
}
