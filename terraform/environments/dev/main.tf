# =============================================================================
# Development Environment
# =============================================================================

terraform {
  # Uncomment for remote state storage
  # backend "s3" {
  #   bucket         = "hypervisor-terraform-state"
  #   key            = "dev/terraform.tfstate"
  #   region         = "us-west-2"
  #   encrypt        = true
  #   dynamodb_table = "hypervisor-terraform-locks"
  # }
}

provider "aws" {
  region = var.region

  default_tags {
    tags = local.tags
  }
}

# =============================================================================
# Local Values
# =============================================================================

locals {
  tags = merge(var.common_tags, {
    Project     = var.project_name
    Environment = var.environment
    ManagedBy   = "terraform"
  })
}

# =============================================================================
# Network
# =============================================================================

module "network" {
  source = "../../modules/network"

  project_name       = var.project_name
  environment        = var.environment
  vpc_cidr           = var.vpc_cidr
  subnet_cidrs       = var.subnet_cidrs
  zones              = var.zones
  enable_nat_gateway = var.enable_nat_gateway
  tags               = local.tags
}

# =============================================================================
# Security
# =============================================================================

module "security" {
  source = "../../modules/security"

  project_name      = var.project_name
  environment       = var.environment
  vpc_id            = module.network.vpc_id
  vpc_cidr          = module.network.vpc_cidr
  allowed_ssh_cidrs = [] # Add your IP for SSH access
  tags              = local.tags
}

# =============================================================================
# etcd Cluster
# =============================================================================

module "etcd" {
  source = "../../modules/etcd-cluster"

  cluster_name       = "${var.project_name}-${var.environment}"
  node_count         = var.etcd_cluster_size
  instance_type      = var.instance_specs.etcd.instance_type
  disk_size_gb       = var.instance_specs.etcd.disk_size_gb
  subnet_ids         = module.network.control_plane_subnet_ids
  security_group_ids = [module.security.etcd_security_group_id]
  etcd_version       = var.etcd_version
  ssh_key_name       = var.ssh_key_name
  tags               = local.tags
}

# =============================================================================
# Hypervisor Server (Control Plane)
# =============================================================================

module "server" {
  source = "../../modules/hypervisor-server"

  cluster_name       = "${var.project_name}-${var.environment}"
  server_count       = var.server_count
  instance_type      = var.instance_specs.server.instance_type
  disk_size_gb       = var.instance_specs.server.disk_size_gb
  subnet_ids         = module.network.control_plane_subnet_ids
  security_group_ids = [module.security.server_security_group_id]
  etcd_endpoints     = module.etcd.endpoints
  hypervisor_version = var.hypervisor_version
  log_level          = var.log_level
  tls_enabled        = var.tls_enabled
  ssh_key_name       = var.ssh_key_name
  tags               = local.tags

  depends_on = [module.etcd]
}

# =============================================================================
# Hypervisor Agents (Compute Nodes)
# =============================================================================

module "agent" {
  source = "../../modules/hypervisor-agent"

  cluster_name             = "${var.project_name}-${var.environment}"
  agent_count              = var.agent_count
  instance_type            = var.instance_specs.agent.instance_type
  disk_size_gb             = var.instance_specs.agent.disk_size_gb
  subnet_ids               = module.network.compute_subnet_ids
  security_group_ids       = [module.security.agent_security_group_id]
  server_endpoint          = module.server.grpc_endpoint
  etcd_endpoints           = module.etcd.endpoints
  supported_instance_types = var.agent_instance_types
  hypervisor_version       = var.hypervisor_version
  region                   = var.region
  log_level                = var.log_level
  ssh_key_name             = var.ssh_key_name
  tags                     = local.tags

  depends_on = [module.server]
}

# =============================================================================
# Outputs
# =============================================================================

output "etcd_endpoints" {
  description = "etcd cluster endpoints"
  value       = module.etcd.endpoints
}

output "server_grpc_endpoint" {
  description = "Hypervisor server gRPC endpoint"
  value       = module.server.grpc_endpoint
}

output "server_http_endpoint" {
  description = "Hypervisor server HTTP endpoint"
  value       = module.server.http_endpoint
}

output "agent_ips" {
  description = "Hypervisor agent private IPs"
  value       = module.agent.private_ips
}

output "vpc_id" {
  description = "VPC ID"
  value       = module.network.vpc_id
}
