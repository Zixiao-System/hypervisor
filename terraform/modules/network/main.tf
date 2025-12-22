# =============================================================================
# Network Module
# =============================================================================

variable "project_name" {
  description = "Project name"
  type        = string
}

variable "environment" {
  description = "Environment name"
  type        = string
}

variable "vpc_cidr" {
  description = "VPC CIDR block"
  type        = string
}

variable "subnet_cidrs" {
  description = "Subnet CIDR blocks"
  type = object({
    control_plane = string
    compute       = string
    storage       = string
  })
}

variable "zones" {
  description = "Availability zones"
  type        = list(string)
}

variable "enable_nat_gateway" {
  description = "Enable NAT Gateway"
  type        = bool
  default     = true
}

variable "tags" {
  description = "Tags"
  type        = map(string)
  default     = {}
}

# =============================================================================
# VPC
# =============================================================================

resource "aws_vpc" "main" {
  cidr_block           = var.vpc_cidr
  enable_dns_hostnames = true
  enable_dns_support   = true

  tags = merge(var.tags, {
    Name = "${var.project_name}-${var.environment}-vpc"
  })
}

# =============================================================================
# Internet Gateway
# =============================================================================

resource "aws_internet_gateway" "main" {
  vpc_id = aws_vpc.main.id

  tags = merge(var.tags, {
    Name = "${var.project_name}-${var.environment}-igw"
  })
}

# =============================================================================
# Subnets
# =============================================================================

# Public subnets (for NAT Gateway and bastion)
resource "aws_subnet" "public" {
  count = length(var.zones)

  vpc_id                  = aws_vpc.main.id
  cidr_block              = cidrsubnet(var.vpc_cidr, 8, count.index)
  availability_zone       = var.zones[count.index]
  map_public_ip_on_launch = true

  tags = merge(var.tags, {
    Name = "${var.project_name}-${var.environment}-public-${var.zones[count.index]}"
    Type = "public"
  })
}

# Control plane subnets
resource "aws_subnet" "control_plane" {
  count = length(var.zones)

  vpc_id            = aws_vpc.main.id
  cidr_block        = cidrsubnet(var.subnet_cidrs.control_plane, 2, count.index)
  availability_zone = var.zones[count.index]

  tags = merge(var.tags, {
    Name = "${var.project_name}-${var.environment}-control-plane-${var.zones[count.index]}"
    Type = "private"
    Role = "control-plane"
  })
}

# Compute subnets
resource "aws_subnet" "compute" {
  count = length(var.zones)

  vpc_id            = aws_vpc.main.id
  cidr_block        = cidrsubnet(var.subnet_cidrs.compute, 2, count.index)
  availability_zone = var.zones[count.index]

  tags = merge(var.tags, {
    Name = "${var.project_name}-${var.environment}-compute-${var.zones[count.index]}"
    Type = "private"
    Role = "compute"
  })
}

# =============================================================================
# NAT Gateway
# =============================================================================

resource "aws_eip" "nat" {
  count  = var.enable_nat_gateway ? 1 : 0
  domain = "vpc"

  tags = merge(var.tags, {
    Name = "${var.project_name}-${var.environment}-nat-eip"
  })
}

resource "aws_nat_gateway" "main" {
  count = var.enable_nat_gateway ? 1 : 0

  allocation_id = aws_eip.nat[0].id
  subnet_id     = aws_subnet.public[0].id

  tags = merge(var.tags, {
    Name = "${var.project_name}-${var.environment}-nat"
  })

  depends_on = [aws_internet_gateway.main]
}

# =============================================================================
# Route Tables
# =============================================================================

# Public route table
resource "aws_route_table" "public" {
  vpc_id = aws_vpc.main.id

  route {
    cidr_block = "0.0.0.0/0"
    gateway_id = aws_internet_gateway.main.id
  }

  tags = merge(var.tags, {
    Name = "${var.project_name}-${var.environment}-public-rt"
  })
}

resource "aws_route_table_association" "public" {
  count = length(aws_subnet.public)

  subnet_id      = aws_subnet.public[count.index].id
  route_table_id = aws_route_table.public.id
}

# Private route table
resource "aws_route_table" "private" {
  vpc_id = aws_vpc.main.id

  dynamic "route" {
    for_each = var.enable_nat_gateway ? [1] : []
    content {
      cidr_block     = "0.0.0.0/0"
      nat_gateway_id = aws_nat_gateway.main[0].id
    }
  }

  tags = merge(var.tags, {
    Name = "${var.project_name}-${var.environment}-private-rt"
  })
}

resource "aws_route_table_association" "control_plane" {
  count = length(aws_subnet.control_plane)

  subnet_id      = aws_subnet.control_plane[count.index].id
  route_table_id = aws_route_table.private.id
}

resource "aws_route_table_association" "compute" {
  count = length(aws_subnet.compute)

  subnet_id      = aws_subnet.compute[count.index].id
  route_table_id = aws_route_table.private.id
}

# =============================================================================
# Outputs
# =============================================================================

output "vpc_id" {
  description = "VPC ID"
  value       = aws_vpc.main.id
}

output "vpc_cidr" {
  description = "VPC CIDR block"
  value       = aws_vpc.main.cidr_block
}

output "public_subnet_ids" {
  description = "Public subnet IDs"
  value       = aws_subnet.public[*].id
}

output "control_plane_subnet_ids" {
  description = "Control plane subnet IDs"
  value       = aws_subnet.control_plane[*].id
}

output "compute_subnet_ids" {
  description = "Compute subnet IDs"
  value       = aws_subnet.compute[*].id
}

output "private_subnet_ids" {
  description = "All private subnet IDs"
  value       = concat(aws_subnet.control_plane[*].id, aws_subnet.compute[*].id)
}

output "nat_gateway_ip" {
  description = "NAT Gateway public IP"
  value       = var.enable_nat_gateway ? aws_eip.nat[0].public_ip : null
}
