# =============================================================================
# Security Module
# =============================================================================

variable "project_name" {
  description = "Project name"
  type        = string
}

variable "environment" {
  description = "Environment name"
  type        = string
}

variable "vpc_id" {
  description = "VPC ID"
  type        = string
}

variable "vpc_cidr" {
  description = "VPC CIDR block"
  type        = string
}

variable "allowed_ssh_cidrs" {
  description = "CIDR blocks allowed for SSH access"
  type        = list(string)
  default     = []
}

variable "tags" {
  description = "Tags"
  type        = map(string)
  default     = {}
}

# =============================================================================
# etcd Security Group
# =============================================================================

resource "aws_security_group" "etcd" {
  name        = "${var.project_name}-${var.environment}-etcd-sg"
  description = "Security group for etcd cluster"
  vpc_id      = var.vpc_id

  # etcd client port
  ingress {
    description = "etcd client"
    from_port   = 2379
    to_port     = 2379
    protocol    = "tcp"
    cidr_blocks = [var.vpc_cidr]
  }

  # etcd peer port
  ingress {
    description = "etcd peer"
    from_port   = 2380
    to_port     = 2380
    protocol    = "tcp"
    self        = true
  }

  # SSH (optional)
  dynamic "ingress" {
    for_each = length(var.allowed_ssh_cidrs) > 0 ? [1] : []
    content {
      description = "SSH"
      from_port   = 22
      to_port     = 22
      protocol    = "tcp"
      cidr_blocks = var.allowed_ssh_cidrs
    }
  }

  egress {
    from_port   = 0
    to_port     = 0
    protocol    = "-1"
    cidr_blocks = ["0.0.0.0/0"]
  }

  tags = merge(var.tags, {
    Name = "${var.project_name}-${var.environment}-etcd-sg"
  })
}

# =============================================================================
# Server Security Group
# =============================================================================

resource "aws_security_group" "server" {
  name        = "${var.project_name}-${var.environment}-server-sg"
  description = "Security group for hypervisor-server"
  vpc_id      = var.vpc_id

  # gRPC port
  ingress {
    description = "gRPC"
    from_port   = 50051
    to_port     = 50051
    protocol    = "tcp"
    cidr_blocks = [var.vpc_cidr]
  }

  # HTTP port
  ingress {
    description = "HTTP"
    from_port   = 8080
    to_port     = 8080
    protocol    = "tcp"
    cidr_blocks = [var.vpc_cidr]
  }

  # SSH (optional)
  dynamic "ingress" {
    for_each = length(var.allowed_ssh_cidrs) > 0 ? [1] : []
    content {
      description = "SSH"
      from_port   = 22
      to_port     = 22
      protocol    = "tcp"
      cidr_blocks = var.allowed_ssh_cidrs
    }
  }

  egress {
    from_port   = 0
    to_port     = 0
    protocol    = "-1"
    cidr_blocks = ["0.0.0.0/0"]
  }

  tags = merge(var.tags, {
    Name = "${var.project_name}-${var.environment}-server-sg"
  })
}

# =============================================================================
# Agent Security Group
# =============================================================================

resource "aws_security_group" "agent" {
  name        = "${var.project_name}-${var.environment}-agent-sg"
  description = "Security group for hypervisor-agent"
  vpc_id      = var.vpc_id

  # Agent gRPC port
  ingress {
    description     = "Agent gRPC"
    from_port       = 50052
    to_port         = 50052
    protocol        = "tcp"
    security_groups = [aws_security_group.server.id]
  }

  # VXLAN overlay network
  ingress {
    description = "VXLAN"
    from_port   = 4789
    to_port     = 4789
    protocol    = "udp"
    self        = true
  }

  # Inter-agent communication
  ingress {
    description = "Inter-agent"
    from_port   = 0
    to_port     = 0
    protocol    = "-1"
    self        = true
  }

  # SSH (optional)
  dynamic "ingress" {
    for_each = length(var.allowed_ssh_cidrs) > 0 ? [1] : []
    content {
      description = "SSH"
      from_port   = 22
      to_port     = 22
      protocol    = "tcp"
      cidr_blocks = var.allowed_ssh_cidrs
    }
  }

  egress {
    from_port   = 0
    to_port     = 0
    protocol    = "-1"
    cidr_blocks = ["0.0.0.0/0"]
  }

  tags = merge(var.tags, {
    Name = "${var.project_name}-${var.environment}-agent-sg"
  })
}

# =============================================================================
# Outputs
# =============================================================================

output "etcd_security_group_id" {
  description = "etcd security group ID"
  value       = aws_security_group.etcd.id
}

output "server_security_group_id" {
  description = "Server security group ID"
  value       = aws_security_group.server.id
}

output "agent_security_group_id" {
  description = "Agent security group ID"
  value       = aws_security_group.agent.id
}
