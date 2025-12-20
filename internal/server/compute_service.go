package server

import (
	"context"
	"fmt"
	"time"

	v1 "hypervisor/api/gen"
	"hypervisor/pkg/cluster/registry"
	"hypervisor/pkg/compute/driver"

	"github.com/google/uuid"
	"go.uber.org/zap"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
)

// ComputeService implements the ComputeService gRPC service.
type ComputeService struct {
	nodeRegistry     *registry.EtcdRegistry
	instanceRegistry *registry.EtcdInstanceRegistry
	agentClients     *AgentClientPool
	logger           *zap.Logger
}

// NewComputeService creates a new ComputeService.
func NewComputeService(
	nodeReg *registry.EtcdRegistry,
	instanceReg *registry.EtcdInstanceRegistry,
	agentClients *AgentClientPool,
	logger *zap.Logger,
) *ComputeService {
	return &ComputeService{
		nodeRegistry:     nodeReg,
		instanceRegistry: instanceReg,
		agentClients:     agentClients,
		logger:           logger,
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
func (s *ComputeService) CreateInstance(ctx context.Context, req *CreateInstanceRequest) (*registry.Instance, error) {
	// Validate instance type
	if req.Type == "" {
		req.Type = driver.InstanceTypeVM
	}

	// Generate instance ID
	instanceID := uuid.New().String()

	// Find suitable node for scheduling
	node, err := s.scheduleInstance(ctx, req)
	if err != nil {
		return nil, status.Errorf(codes.ResourceExhausted, "no suitable node found: %v", err)
	}

	s.logger.Info("instance scheduled",
		zap.String("instance_id", instanceID),
		zap.String("name", req.Name),
		zap.String("type", string(req.Type)),
		zap.String("node_id", node.ID),
	)

	// Get agent client
	agentClient, err := s.agentClients.GetClient(ctx, node.ID)
	if err != nil {
		return nil, status.Errorf(codes.Unavailable, "failed to connect to agent: %v", err)
	}

	// Call agent to create instance
	agentReq := &v1.AgentCreateInstanceRequest{
		InstanceId: instanceID,
		Name:       req.Name,
		Type:       driverTypeToProtoType(req.Type),
		Spec:       driverSpecToProtoSpec(&req.Spec),
		Labels:     req.Metadata,
	}

	agentResp, err := agentClient.CreateInstance(ctx, agentReq)
	if err != nil {
		return nil, status.Errorf(codes.Internal, "agent failed to create instance: %v", err)
	}

	// Create instance record for registry
	now := time.Now()
	instance := &registry.Instance{
		ID:          instanceID,
		Name:        req.Name,
		Type:        req.Type,
		State:       protoStateToDriverState(agentResp.State),
		StateReason: agentResp.StateReason,
		Spec:        req.Spec,
		NodeID:      node.ID,
		IPAddress:   agentResp.IpAddress,
		Labels:      req.Metadata,
		CreatedAt:   now,
		UpdatedAt:   now,
	}

	// Store in etcd
	if err := s.instanceRegistry.Create(ctx, instance); err != nil {
		s.logger.Error("failed to store instance in registry",
			zap.String("instance_id", instanceID),
			zap.Error(err),
		)
		// Try to clean up on agent
		_, _ = agentClient.DeleteInstance(ctx, &v1.AgentDeleteInstanceRequest{InstanceId: instanceID})
		return nil, status.Errorf(codes.Internal, "failed to store instance: %v", err)
	}

	s.logger.Info("instance created",
		zap.String("instance_id", instanceID),
		zap.String("name", req.Name),
		zap.String("node_id", node.ID),
	)

	return instance, nil
}

// scheduleInstance finds a suitable node for the instance.
func (s *ComputeService) scheduleInstance(ctx context.Context, req *CreateInstanceRequest) (*registry.Node, error) {
	var nodes []*registry.Node
	var err error

	// If preferred node is specified, try it first
	if req.PreferredNodeID != "" {
		node, err := s.nodeRegistry.Get(ctx, req.PreferredNodeID)
		if err == nil && s.canScheduleOn(node, req) {
			return node, nil
		}
	}

	// List all worker nodes
	nodes, err = s.nodeRegistry.ListByRole(ctx, registry.NodeRoleWorker)
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
	// Get instance from registry
	instance, err := s.instanceRegistry.Get(ctx, req.InstanceID)
	if err != nil {
		if err == registry.ErrInstanceNotFound {
			return status.Errorf(codes.NotFound, "instance not found: %s", req.InstanceID)
		}
		return status.Errorf(codes.Internal, "failed to get instance: %v", err)
	}

	// Get agent client
	agentClient, err := s.agentClients.GetClient(ctx, instance.NodeID)
	if err != nil {
		s.logger.Warn("failed to connect to agent, will delete from registry anyway",
			zap.String("instance_id", req.InstanceID),
			zap.String("node_id", instance.NodeID),
			zap.Error(err),
		)
	} else {
		// Call agent to delete instance
		_, err = agentClient.DeleteInstance(ctx, &v1.AgentDeleteInstanceRequest{
			InstanceId: req.InstanceID,
			Force:      req.Force,
		})
		if err != nil {
			s.logger.Warn("agent failed to delete instance",
				zap.String("instance_id", req.InstanceID),
				zap.Error(err),
			)
		}
	}

	// Delete from registry
	if err := s.instanceRegistry.Delete(ctx, req.InstanceID); err != nil {
		return status.Errorf(codes.Internal, "failed to delete instance from registry: %v", err)
	}

	s.logger.Info("instance deleted", zap.String("instance_id", req.InstanceID))
	return nil
}

// GetInstanceRequest represents a get instance request.
type GetInstanceRequest struct {
	InstanceID string
}

// GetInstance retrieves an instance by ID.
func (s *ComputeService) GetInstance(ctx context.Context, req *GetInstanceRequest) (*registry.Instance, error) {
	instance, err := s.instanceRegistry.Get(ctx, req.InstanceID)
	if err != nil {
		if err == registry.ErrInstanceNotFound {
			return nil, status.Errorf(codes.NotFound, "instance not found: %s", req.InstanceID)
		}
		return nil, status.Errorf(codes.Internal, "failed to get instance: %v", err)
	}

	return instance, nil
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
	Instances     []*registry.Instance
	NextPageToken string
	TotalCount    int
}

// ListInstances lists instances.
func (s *ComputeService) ListInstances(ctx context.Context, req *ListInstancesRequest) (*ListInstancesResponse, error) {
	var instances []*registry.Instance
	var err error

	// Get instances based on filters
	if req.NodeID != "" {
		instances, err = s.instanceRegistry.ListByNode(ctx, req.NodeID)
	} else if req.Type != "" {
		instances, err = s.instanceRegistry.ListByType(ctx, req.Type)
	} else if req.State != "" {
		instances, err = s.instanceRegistry.ListByState(ctx, req.State)
	} else {
		instances, err = s.instanceRegistry.List(ctx)
	}

	if err != nil {
		return nil, status.Errorf(codes.Internal, "failed to list instances: %v", err)
	}

	// Apply additional filters
	filtered := make([]*registry.Instance, 0, len(instances))
	for _, instance := range instances {
		// Type filter (if not already filtered)
		if req.Type != "" && req.NodeID != "" && instance.Type != req.Type {
			continue
		}

		// State filter (if not already filtered)
		if req.State != "" && req.NodeID != "" && instance.State != req.State {
			continue
		}

		// Label selector
		if len(req.LabelSelector) > 0 && !instance.MatchesLabels(req.LabelSelector) {
			continue
		}

		filtered = append(filtered, instance)
	}

	// TODO: Implement pagination
	return &ListInstancesResponse{
		Instances:  filtered,
		TotalCount: len(filtered),
	}, nil
}

// StartInstanceRequest represents a start instance request.
type StartInstanceRequest struct {
	InstanceID string
}

// StartInstance starts an instance.
func (s *ComputeService) StartInstance(ctx context.Context, req *StartInstanceRequest) (*registry.Instance, error) {
	// Get instance from registry
	instance, err := s.instanceRegistry.Get(ctx, req.InstanceID)
	if err != nil {
		if err == registry.ErrInstanceNotFound {
			return nil, status.Errorf(codes.NotFound, "instance not found: %s", req.InstanceID)
		}
		return nil, status.Errorf(codes.Internal, "failed to get instance: %v", err)
	}

	// Get agent client
	agentClient, err := s.agentClients.GetClient(ctx, instance.NodeID)
	if err != nil {
		return nil, status.Errorf(codes.Unavailable, "failed to connect to agent: %v", err)
	}

	// Call agent to start instance
	agentResp, err := agentClient.StartInstance(ctx, &v1.AgentInstanceRequest{
		InstanceId: req.InstanceID,
	})
	if err != nil {
		return nil, status.Errorf(codes.Internal, "agent failed to start instance: %v", err)
	}

	// Update registry
	instance.State = protoStateToDriverState(agentResp.State)
	instance.StateReason = agentResp.StateReason
	if agentResp.StartedAt != nil {
		t := agentResp.StartedAt.AsTime()
		instance.StartedAt = &t
	}

	if err := s.instanceRegistry.Update(ctx, instance); err != nil {
		s.logger.Warn("failed to update instance in registry", zap.Error(err))
	}

	s.logger.Info("instance started", zap.String("instance_id", req.InstanceID))
	return instance, nil
}

// StopInstanceRequest represents a stop instance request.
type StopInstanceRequest struct {
	InstanceID     string
	Force          bool
	TimeoutSeconds int
}

// StopInstance stops an instance.
func (s *ComputeService) StopInstance(ctx context.Context, req *StopInstanceRequest) (*registry.Instance, error) {
	// Get instance from registry
	instance, err := s.instanceRegistry.Get(ctx, req.InstanceID)
	if err != nil {
		if err == registry.ErrInstanceNotFound {
			return nil, status.Errorf(codes.NotFound, "instance not found: %s", req.InstanceID)
		}
		return nil, status.Errorf(codes.Internal, "failed to get instance: %v", err)
	}

	// Get agent client
	agentClient, err := s.agentClients.GetClient(ctx, instance.NodeID)
	if err != nil {
		return nil, status.Errorf(codes.Unavailable, "failed to connect to agent: %v", err)
	}

	// Call agent to stop instance
	agentResp, err := agentClient.StopInstance(ctx, &v1.AgentStopInstanceRequest{
		InstanceId:     req.InstanceID,
		Force:          req.Force,
		TimeoutSeconds: int32(req.TimeoutSeconds),
	})
	if err != nil {
		return nil, status.Errorf(codes.Internal, "agent failed to stop instance: %v", err)
	}

	// Update registry
	instance.State = protoStateToDriverState(agentResp.State)
	instance.StateReason = agentResp.StateReason

	if err := s.instanceRegistry.Update(ctx, instance); err != nil {
		s.logger.Warn("failed to update instance in registry", zap.Error(err))
	}

	s.logger.Info("instance stopped", zap.String("instance_id", req.InstanceID))
	return instance, nil
}

// RestartInstanceRequest represents a restart instance request.
type RestartInstanceRequest struct {
	InstanceID string
	Force      bool
}

// RestartInstance restarts an instance.
func (s *ComputeService) RestartInstance(ctx context.Context, req *RestartInstanceRequest) (*registry.Instance, error) {
	// Get instance from registry
	instance, err := s.instanceRegistry.Get(ctx, req.InstanceID)
	if err != nil {
		if err == registry.ErrInstanceNotFound {
			return nil, status.Errorf(codes.NotFound, "instance not found: %s", req.InstanceID)
		}
		return nil, status.Errorf(codes.Internal, "failed to get instance: %v", err)
	}

	// Get agent client
	agentClient, err := s.agentClients.GetClient(ctx, instance.NodeID)
	if err != nil {
		return nil, status.Errorf(codes.Unavailable, "failed to connect to agent: %v", err)
	}

	// Call agent to restart instance
	agentResp, err := agentClient.RestartInstance(ctx, &v1.AgentRestartInstanceRequest{
		InstanceId: req.InstanceID,
		Force:      req.Force,
	})
	if err != nil {
		return nil, status.Errorf(codes.Internal, "agent failed to restart instance: %v", err)
	}

	// Update registry
	instance.State = protoStateToDriverState(agentResp.State)
	instance.StateReason = agentResp.StateReason
	if agentResp.StartedAt != nil {
		t := agentResp.StartedAt.AsTime()
		instance.StartedAt = &t
	}

	if err := s.instanceRegistry.Update(ctx, instance); err != nil {
		s.logger.Warn("failed to update instance in registry", zap.Error(err))
	}

	s.logger.Info("instance restarted", zap.String("instance_id", req.InstanceID))
	return instance, nil
}

// GetInstanceStatsRequest represents a get instance stats request.
type GetInstanceStatsRequest struct {
	InstanceID string
}

// GetInstanceStats retrieves instance statistics.
func (s *ComputeService) GetInstanceStats(ctx context.Context, req *GetInstanceStatsRequest) (*driver.InstanceStats, error) {
	// Get instance from registry
	instance, err := s.instanceRegistry.Get(ctx, req.InstanceID)
	if err != nil {
		if err == registry.ErrInstanceNotFound {
			return nil, status.Errorf(codes.NotFound, "instance not found: %s", req.InstanceID)
		}
		return nil, status.Errorf(codes.Internal, "failed to get instance: %v", err)
	}

	// Get agent client
	agentClient, err := s.agentClients.GetClient(ctx, instance.NodeID)
	if err != nil {
		return nil, status.Errorf(codes.Unavailable, "failed to connect to agent: %v", err)
	}

	// Call agent to get stats
	agentResp, err := agentClient.GetInstanceStats(ctx, &v1.AgentInstanceRequest{
		InstanceId: req.InstanceID,
	})
	if err != nil {
		return nil, status.Errorf(codes.Internal, "agent failed to get instance stats: %v", err)
	}

	return &driver.InstanceStats{
		InstanceID:       agentResp.InstanceId,
		CPUUsagePercent:  agentResp.CpuUsagePercent,
		CPUTimeNs:        uint64(agentResp.CpuTimeNs),
		MemoryUsedBytes:  uint64(agentResp.MemoryUsedBytes),
		MemoryCacheBytes: uint64(agentResp.MemoryCacheBytes),
		DiskReadBytes:    uint64(agentResp.DiskReadBytes),
		DiskWriteBytes:   uint64(agentResp.DiskWriteBytes),
		NetworkRxBytes:   uint64(agentResp.NetworkRxBytes),
		NetworkTxBytes:   uint64(agentResp.NetworkTxBytes),
		CollectedAt:      agentResp.CollectedAt.AsTime(),
	}, nil
}

// ============================================================================
// Conversion helpers
// ============================================================================

func driverTypeToProtoType(t driver.InstanceType) v1.InstanceType {
	switch t {
	case driver.InstanceTypeVM:
		return v1.InstanceType_INSTANCE_TYPE_VM
	case driver.InstanceTypeContainer:
		return v1.InstanceType_INSTANCE_TYPE_CONTAINER
	case driver.InstanceTypeMicroVM:
		return v1.InstanceType_INSTANCE_TYPE_MICROVM
	default:
		return v1.InstanceType_INSTANCE_TYPE_UNSPECIFIED
	}
}

func protoStateToDriverState(s v1.InstanceState) driver.InstanceState {
	switch s {
	case v1.InstanceState_INSTANCE_STATE_PENDING:
		return driver.StatePending
	case v1.InstanceState_INSTANCE_STATE_CREATING:
		return driver.StateCreating
	case v1.InstanceState_INSTANCE_STATE_RUNNING:
		return driver.StateRunning
	case v1.InstanceState_INSTANCE_STATE_STOPPED:
		return driver.StateStopped
	case v1.InstanceState_INSTANCE_STATE_FAILED:
		return driver.StateFailed
	default:
		return driver.StateUnknown
	}
}

func driverSpecToProtoSpec(spec *driver.InstanceSpec) *v1.InstanceSpec {
	if spec == nil {
		return nil
	}

	protoSpec := &v1.InstanceSpec{
		Image:       spec.Image,
		CpuCores:    int32(spec.CPUCores),
		MemoryBytes: spec.MemoryMB * 1024 * 1024,
		Kernel:      spec.Kernel,
		Initrd:      spec.Initrd,
		KernelArgs:  spec.KernelArgs,
		Command:     spec.Command,
		Args:        spec.Args,
		Env:         spec.Env,
	}

	// Convert disks
	if len(spec.Disks) > 0 {
		protoSpec.Disks = make([]*v1.DiskSpec, len(spec.Disks))
		for i, d := range spec.Disks {
			protoSpec.Disks[i] = &v1.DiskSpec{
				Name:      d.Name,
				SizeBytes: d.SizeGB * 1024 * 1024 * 1024,
				Type:      d.Type,
				Boot:      d.Boot,
			}
		}
	}

	// Convert network
	protoSpec.Network = &v1.NetworkSpec{
		NetworkId:      spec.Network.NetworkID,
		SubnetId:       spec.Network.SubnetID,
		SecurityGroups: spec.Network.SecurityGroups,
		AssignPublicIp: spec.Network.AssignPublicIP,
	}

	// Convert limits
	if spec.Limits.CPUQuota > 0 || spec.Limits.MemoryLimit > 0 {
		protoSpec.Limits = &v1.ResourceLimits{
			CpuQuota:    spec.Limits.CPUQuota,
			CpuPeriod:   spec.Limits.CPUPeriod,
			MemoryLimit: spec.Limits.MemoryLimit,
			IoReadBps:   spec.Limits.IOReadBPS,
			IoWriteBps:  spec.Limits.IOWriteBPS,
		}
	}

	return protoSpec
}
