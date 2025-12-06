package server

import (
	"context"
	"fmt"
	"time"

	"hypervisor/pkg/cluster/registry"
	"hypervisor/pkg/compute/driver"

	"github.com/google/uuid"
	"go.uber.org/zap"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
)

// ComputeService implements the ComputeService gRPC service.
type ComputeService struct {
	registry *registry.EtcdRegistry
	drivers  map[driver.InstanceType]driver.Driver
	logger   *zap.Logger
}

// NewComputeService creates a new ComputeService.
func NewComputeService(
	reg *registry.EtcdRegistry,
	drivers map[driver.InstanceType]driver.Driver,
	logger *zap.Logger,
) *ComputeService {
	return &ComputeService{
		registry: reg,
		drivers:  drivers,
		logger:   logger,
	}
}

// CreateInstanceRequest represents a create instance request.
type CreateInstanceRequest struct {
	Name            string
	Type            driver.InstanceType
	Spec            driver.InstanceSpec
	Metadata        map[string]string
	PreferredNodeID string
	Region          string
	Zone            string
}

// CreateInstance creates a new instance.
func (s *ComputeService) CreateInstance(ctx context.Context, req *CreateInstanceRequest) (*driver.Instance, error) {
	// Validate instance type
	if req.Type == "" {
		req.Type = driver.InstanceTypeVM
	}

	// Find suitable node for scheduling
	node, err := s.scheduleInstance(ctx, req)
	if err != nil {
		return nil, status.Errorf(codes.ResourceExhausted, "no suitable node found: %v", err)
	}

	s.logger.Info("instance scheduled",
		zap.String("name", req.Name),
		zap.String("type", string(req.Type)),
		zap.String("node_id", node.ID),
	)

	// In a real implementation, we would send a request to the agent on the target node
	// For now, we'll create a placeholder instance
	now := time.Now()
	instance := &driver.Instance{
		ID:        uuid.New().String(),
		Name:      req.Name,
		Type:      req.Type,
		State:     driver.StatePending,
		Spec:      req.Spec,
		Metadata:  req.Metadata,
		CreatedAt: now,
	}

	// TODO: Send CreateInstance request to the agent on node.ID

	return instance, nil
}

// scheduleInstance finds a suitable node for the instance.
func (s *ComputeService) scheduleInstance(ctx context.Context, req *CreateInstanceRequest) (*registry.Node, error) {
	var nodes []*registry.Node
	var err error

	// If preferred node is specified, try it first
	if req.PreferredNodeID != "" {
		node, err := s.registry.Get(ctx, req.PreferredNodeID)
		if err == nil && s.canScheduleOn(node, req) {
			return node, nil
		}
	}

	// List all worker nodes
	nodes, err = s.registry.ListByRole(ctx, registry.NodeRoleWorker)
	if err != nil {
		return nil, fmt.Errorf("failed to list nodes: %w", err)
	}

	// Filter by region and zone
	filtered := make([]*registry.Node, 0)
	for _, node := range nodes {
		if !node.IsReady() {
			continue
		}

		if req.Region != "" && node.Region != req.Region {
			continue
		}

		if req.Zone != "" && node.Zone != req.Zone {
			continue
		}

		if s.canScheduleOn(node, req) {
			filtered = append(filtered, node)
		}
	}

	if len(filtered) == 0 {
		return nil, fmt.Errorf("no suitable node found")
	}

	// Simple bin-packing: select node with least available resources
	// In production, you'd want more sophisticated scheduling algorithms
	selected := filtered[0]
	for _, node := range filtered[1:] {
		if s.scoreNode(node) > s.scoreNode(selected) {
			selected = node
		}
	}

	return selected, nil
}

// canScheduleOn checks if an instance can be scheduled on a node.
func (s *ComputeService) canScheduleOn(node *registry.Node, req *CreateInstanceRequest) bool {
	// Check if node supports the instance type
	if !node.SupportsInstanceType(registry.InstanceType(req.Type)) {
		return false
	}

	// Check resources
	required := registry.Resources{
		CPUCores:    req.Spec.CPUCores,
		MemoryBytes: req.Spec.MemoryMB * 1024 * 1024,
		DiskBytes:   req.Spec.DiskGB * 1024 * 1024 * 1024,
	}

	return node.CanSchedule(required)
}

// scoreNode calculates a scheduling score for a node (higher is better).
func (s *ComputeService) scoreNode(node *registry.Node) float64 {
	avail := node.AvailableResources()

	// Simple scoring based on available resources
	// In production, you'd want to consider more factors
	cpuScore := float64(avail.CPUCores) / float64(node.Capacity.CPUCores+1)
	memScore := float64(avail.MemoryBytes) / float64(node.Capacity.MemoryBytes+1)

	return (cpuScore + memScore) / 2
}

// DeleteInstanceRequest represents a delete instance request.
type DeleteInstanceRequest struct {
	InstanceID string
	Force      bool
}

// DeleteInstance deletes an instance.
func (s *ComputeService) DeleteInstance(ctx context.Context, req *DeleteInstanceRequest) error {
	// TODO: Implement instance deletion
	// 1. Find the node where the instance is running
	// 2. Send DeleteInstance request to the agent
	// 3. Remove from instance store

	s.logger.Info("instance deleted", zap.String("id", req.InstanceID))
	return nil
}

// GetInstanceRequest represents a get instance request.
type GetInstanceRequest struct {
	InstanceID string
}

// GetInstance retrieves an instance by ID.
func (s *ComputeService) GetInstance(ctx context.Context, req *GetInstanceRequest) (*driver.Instance, error) {
	// TODO: Implement instance lookup
	// This would query the instance store or the agent on the target node

	return nil, status.Errorf(codes.NotFound, "instance not found")
}

// ListInstancesRequest represents a list instances request.
type ListInstancesRequest struct {
	Type          driver.InstanceType
	State         driver.InstanceState
	NodeID        string
	LabelSelector map[string]string
	PageSize      int
	PageToken     string
}

// ListInstancesResponse represents a list instances response.
type ListInstancesResponse struct {
	Instances     []*driver.Instance
	NextPageToken string
	TotalCount    int
}

// ListInstances lists instances.
func (s *ComputeService) ListInstances(ctx context.Context, req *ListInstancesRequest) (*ListInstancesResponse, error) {
	// TODO: Implement instance listing
	// This would aggregate instances from all agents

	return &ListInstancesResponse{
		Instances:  []*driver.Instance{},
		TotalCount: 0,
	}, nil
}

// StartInstanceRequest represents a start instance request.
type StartInstanceRequest struct {
	InstanceID string
}

// StartInstance starts an instance.
func (s *ComputeService) StartInstance(ctx context.Context, req *StartInstanceRequest) (*driver.Instance, error) {
	// TODO: Implement instance start
	// 1. Find the node where the instance is defined
	// 2. Send StartInstance request to the agent

	return nil, status.Errorf(codes.NotFound, "instance not found")
}

// StopInstanceRequest represents a stop instance request.
type StopInstanceRequest struct {
	InstanceID     string
	Force          bool
	TimeoutSeconds int
}

// StopInstance stops an instance.
func (s *ComputeService) StopInstance(ctx context.Context, req *StopInstanceRequest) (*driver.Instance, error) {
	// TODO: Implement instance stop
	// 1. Find the node where the instance is running
	// 2. Send StopInstance request to the agent

	return nil, status.Errorf(codes.NotFound, "instance not found")
}

// RestartInstanceRequest represents a restart instance request.
type RestartInstanceRequest struct {
	InstanceID string
	Force      bool
}

// RestartInstance restarts an instance.
func (s *ComputeService) RestartInstance(ctx context.Context, req *RestartInstanceRequest) (*driver.Instance, error) {
	// TODO: Implement instance restart

	return nil, status.Errorf(codes.NotFound, "instance not found")
}

// GetInstanceStatsRequest represents a get instance stats request.
type GetInstanceStatsRequest struct {
	InstanceID string
}

// GetInstanceStats retrieves instance statistics.
func (s *ComputeService) GetInstanceStats(ctx context.Context, req *GetInstanceStatsRequest) (*driver.InstanceStats, error) {
	// TODO: Implement instance stats retrieval
	// This would query the agent on the target node

	return nil, status.Errorf(codes.NotFound, "instance not found")
}
