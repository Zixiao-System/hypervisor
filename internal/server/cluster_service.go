package server

import (
	"context"
	"time"

	"hypervisor/pkg/cluster/registry"

	"go.uber.org/zap"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
)

// ClusterService implements the ClusterService gRPC service.
type ClusterService struct {
	registry *registry.EtcdRegistry
	logger   *zap.Logger
}

// NewClusterService creates a new ClusterService.
func NewClusterService(reg *registry.EtcdRegistry, logger *zap.Logger) *ClusterService {
	return &ClusterService{
		registry: reg,
		logger:   logger,
	}
}

// RegisterNodeRequest represents a node registration request.
type RegisterNodeRequest struct {
	Hostname               string
	IP                     string
	Port                   int
	Role                   registry.NodeRole
	Region                 string
	Zone                   string
	Capacity               registry.Resources
	Labels                 map[string]string
	SupportedInstanceTypes []registry.InstanceType
}

// RegisterNodeResponse represents a node registration response.
type RegisterNodeResponse struct {
	NodeID                   string
	HeartbeatIntervalSeconds int64
	LeaseTTLSeconds          int64
}

// RegisterNode registers a new node in the cluster.
func (s *ClusterService) RegisterNode(ctx context.Context, req *RegisterNodeRequest) (*RegisterNodeResponse, error) {
	node := &registry.Node{
		Hostname:               req.Hostname,
		IP:                     req.IP,
		Port:                   req.Port,
		Role:                   req.Role,
		Status:                 registry.NodeStatusReady,
		Region:                 req.Region,
		Zone:                   req.Zone,
		Capacity:               req.Capacity,
		Allocatable:            req.Capacity, // Initially all capacity is allocatable
		Labels:                 req.Labels,
		SupportedInstanceTypes: req.SupportedInstanceTypes,
		Conditions: []registry.NodeCondition{
			{
				Type:               registry.ConditionReady,
				Status:             registry.ConditionTrue,
				Reason:             "NodeRegistered",
				Message:            "Node has been registered",
				LastTransitionTime: time.Now(),
			},
		},
	}

	nodeID, err := s.registry.Register(ctx, node)
	if err != nil {
		s.logger.Error("failed to register node", zap.Error(err))
		return nil, status.Errorf(codes.Internal, "failed to register node: %v", err)
	}

	s.logger.Info("node registered",
		zap.String("node_id", nodeID),
		zap.String("hostname", req.Hostname),
		zap.String("role", string(req.Role)),
	)

	return &RegisterNodeResponse{
		NodeID:                   nodeID,
		HeartbeatIntervalSeconds: 10,
		LeaseTTLSeconds:          30,
	}, nil
}

// DeregisterNodeRequest represents a node deregistration request.
type DeregisterNodeRequest struct {
	NodeID string
}

// DeregisterNode removes a node from the cluster.
func (s *ClusterService) DeregisterNode(ctx context.Context, req *DeregisterNodeRequest) error {
	if err := s.registry.Deregister(ctx, req.NodeID); err != nil {
		if err == registry.ErrNodeNotFound {
			return status.Errorf(codes.NotFound, "node not found")
		}
		s.logger.Error("failed to deregister node", zap.Error(err))
		return status.Errorf(codes.Internal, "failed to deregister node: %v", err)
	}

	s.logger.Info("node deregistered", zap.String("node_id", req.NodeID))
	return nil
}

// GetNodeRequest represents a get node request.
type GetNodeRequest struct {
	NodeID string
}

// GetNode retrieves a node by ID.
func (s *ClusterService) GetNode(ctx context.Context, req *GetNodeRequest) (*registry.Node, error) {
	node, err := s.registry.Get(ctx, req.NodeID)
	if err != nil {
		if err == registry.ErrNodeNotFound {
			return nil, status.Errorf(codes.NotFound, "node not found")
		}
		return nil, status.Errorf(codes.Internal, "failed to get node: %v", err)
	}

	return node, nil
}

// ListNodesRequest represents a list nodes request.
type ListNodesRequest struct {
	Role          registry.NodeRole
	Status        registry.NodeStatus
	Region        string
	Zone          string
	LabelSelector map[string]string
	PageSize      int
	PageToken     string
}

// ListNodesResponse represents a list nodes response.
type ListNodesResponse struct {
	Nodes         []*registry.Node
	NextPageToken string
	TotalCount    int
}

// ListNodes lists nodes in the cluster.
func (s *ClusterService) ListNodes(ctx context.Context, req *ListNodesRequest) (*ListNodesResponse, error) {
	var nodes []*registry.Node
	var err error

	if req.Role != "" {
		nodes, err = s.registry.ListByRole(ctx, req.Role)
	} else if req.Region != "" {
		nodes, err = s.registry.ListByRegion(ctx, req.Region)
	} else {
		nodes, err = s.registry.List(ctx)
	}

	if err != nil {
		return nil, status.Errorf(codes.Internal, "failed to list nodes: %v", err)
	}

	// Apply additional filters
	filtered := make([]*registry.Node, 0, len(nodes))
	for _, node := range nodes {
		// Filter by status
		if req.Status != "" && node.Status != req.Status {
			continue
		}

		// Filter by zone
		if req.Zone != "" && node.Zone != req.Zone {
			continue
		}

		// Filter by labels
		if len(req.LabelSelector) > 0 {
			match := true
			for k, v := range req.LabelSelector {
				if node.Labels[k] != v {
					match = false
					break
				}
			}
			if !match {
				continue
			}
		}

		filtered = append(filtered, node)
	}

	return &ListNodesResponse{
		Nodes:      filtered,
		TotalCount: len(filtered),
	}, nil
}

// UpdateNodeStatusRequest represents an update node status request.
type UpdateNodeStatusRequest struct {
	NodeID     string
	Status     registry.NodeStatus
	Conditions []registry.NodeCondition
	Allocated  registry.Resources
}

// UpdateNodeStatus updates a node's status.
func (s *ClusterService) UpdateNodeStatus(ctx context.Context, req *UpdateNodeStatusRequest) (*registry.Node, error) {
	node, err := s.registry.Get(ctx, req.NodeID)
	if err != nil {
		if err == registry.ErrNodeNotFound {
			return nil, status.Errorf(codes.NotFound, "node not found")
		}
		return nil, status.Errorf(codes.Internal, "failed to get node: %v", err)
	}

	node.Status = req.Status
	node.Conditions = req.Conditions
	node.Allocated = req.Allocated
	node.LastSeen = time.Now()

	if err := s.registry.Update(ctx, node); err != nil {
		return nil, status.Errorf(codes.Internal, "failed to update node: %v", err)
	}

	return node, nil
}

// HeartbeatRequest represents a heartbeat request.
type HeartbeatRequest struct {
	NodeID     string
	Status     registry.NodeStatus
	Conditions []registry.NodeCondition
	Allocated  registry.Resources
}

// HeartbeatResponse represents a heartbeat response.
type HeartbeatResponse struct {
	Accepted             bool
	NextHeartbeatSeconds int64
	Commands             []NodeCommand
}

// NodeCommand represents a command to be executed by the agent.
type NodeCommand struct {
	ID         string
	Type       string
	Parameters map[string]string
}

// Heartbeat processes a heartbeat from an agent.
func (s *ClusterService) Heartbeat(ctx context.Context, req *HeartbeatRequest) (*HeartbeatResponse, error) {
	node, err := s.registry.Get(ctx, req.NodeID)
	if err != nil {
		if err == registry.ErrNodeNotFound {
			return &HeartbeatResponse{Accepted: false}, nil
		}
		return nil, status.Errorf(codes.Internal, "failed to get node: %v", err)
	}

	// Update node status
	node.Status = req.Status
	node.Conditions = req.Conditions
	node.Allocated = req.Allocated
	node.LastSeen = time.Now()

	if err := s.registry.Update(ctx, node); err != nil {
		return nil, status.Errorf(codes.Internal, "failed to update node: %v", err)
	}

	// TODO: Check for pending commands for this node
	commands := []NodeCommand{}

	return &HeartbeatResponse{
		Accepted:             true,
		NextHeartbeatSeconds: 10,
		Commands:             commands,
	}, nil
}

// WatchNodesRequest represents a watch nodes request.
type WatchNodesRequest struct {
	Role   registry.NodeRole
	Region string
	Zone   string
}

// WatchNodes watches for node changes.
func (s *ClusterService) WatchNodes(ctx context.Context, req *WatchNodesRequest, send func(*registry.NodeEvent) error) error {
	events, err := s.registry.Watch(ctx)
	if err != nil {
		return status.Errorf(codes.Internal, "failed to watch nodes: %v", err)
	}

	for event := range events {
		// Apply filters
		if req.Role != "" && event.Node.Role != req.Role {
			continue
		}
		if req.Region != "" && event.Node.Region != req.Region {
			continue
		}
		if req.Zone != "" && event.Node.Zone != req.Zone {
			continue
		}

		if err := send(&event); err != nil {
			return err
		}
	}

	return nil
}

// GetClusterInfoResponse represents cluster information.
type GetClusterInfoResponse struct {
	ClusterID      string
	ClusterName    string
	Version        string
	TotalNodes     int
	ReadyNodes     int
	TotalCapacity  registry.Resources
	TotalAllocated registry.Resources
}

// GetClusterInfo returns cluster information.
func (s *ClusterService) GetClusterInfo(ctx context.Context) (*GetClusterInfoResponse, error) {
	nodes, err := s.registry.List(ctx)
	if err != nil {
		return nil, status.Errorf(codes.Internal, "failed to list nodes: %v", err)
	}

	var totalCapacity, totalAllocated registry.Resources
	readyCount := 0

	for _, node := range nodes {
		if node.IsReady() {
			readyCount++
		}

		totalCapacity.CPUCores += node.Capacity.CPUCores
		totalCapacity.MemoryBytes += node.Capacity.MemoryBytes
		totalCapacity.DiskBytes += node.Capacity.DiskBytes
		totalCapacity.GPUCount += node.Capacity.GPUCount

		totalAllocated.CPUCores += node.Allocated.CPUCores
		totalAllocated.MemoryBytes += node.Allocated.MemoryBytes
		totalAllocated.DiskBytes += node.Allocated.DiskBytes
		totalAllocated.GPUCount += node.Allocated.GPUCount
	}

	return &GetClusterInfoResponse{
		ClusterID:      "default",
		ClusterName:    "hypervisor-cluster",
		Version:        "0.1.0",
		TotalNodes:     len(nodes),
		ReadyNodes:     readyCount,
		TotalCapacity:  totalCapacity,
		TotalAllocated: totalAllocated,
	}, nil
}
