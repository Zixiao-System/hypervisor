package agent

import (
	"context"
	"fmt"
	"io"

	v1 "hypervisor/api/gen"
	"hypervisor/pkg/compute/driver"

	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
	"google.golang.org/protobuf/types/known/emptypb"
	"google.golang.org/protobuf/types/known/timestamppb"
)

// AgentGRPCService implements the AgentService gRPC interface.
type AgentGRPCService struct {
	v1.UnimplementedAgentServiceServer
	agent *Agent
}

// NewAgentGRPCService creates a new Agent gRPC service.
func NewAgentGRPCService(agent *Agent) *AgentGRPCService {
	return &AgentGRPCService{
		agent: agent,
	}
}

// CreateInstance creates an instance on this agent.
func (s *AgentGRPCService) CreateInstance(ctx context.Context, req *v1.AgentCreateInstanceRequest) (*v1.Instance, error) {
	// Convert proto spec to driver spec
	spec := protoSpecToDriverSpec(req.Spec)

	// Get instance type
	instanceType := protoTypeToDriverType(req.Type)

	// Create instance using agent
	instance, err := s.agent.CreateInstance(ctx, spec, instanceType)
	if err != nil {
		return nil, status.Errorf(codes.Internal, "failed to create instance: %v", err)
	}

	// Override ID if provided by server
	if req.InstanceId != "" {
		instance.ID = req.InstanceId
	}
	instance.Name = req.Name
	instance.Metadata = req.Labels

	// Update local cache with correct ID
	s.agent.instancesMu.Lock()
	delete(s.agent.instances, instance.ID)
	s.agent.instances[instance.ID] = instance
	s.agent.instancesMu.Unlock()

	return driverInstanceToProto(instance, s.agent.nodeID), nil
}

// DeleteInstance deletes an instance on this agent.
func (s *AgentGRPCService) DeleteInstance(ctx context.Context, req *v1.AgentDeleteInstanceRequest) (*emptypb.Empty, error) {
	if err := s.agent.DeleteInstance(ctx, req.InstanceId); err != nil {
		if err == driver.ErrInstanceNotFound {
			return nil, status.Errorf(codes.NotFound, "instance not found: %s", req.InstanceId)
		}
		return nil, status.Errorf(codes.Internal, "failed to delete instance: %v", err)
	}

	return &emptypb.Empty{}, nil
}

// StartInstance starts an instance on this agent.
func (s *AgentGRPCService) StartInstance(ctx context.Context, req *v1.AgentInstanceRequest) (*v1.Instance, error) {
	if err := s.agent.StartInstance(ctx, req.InstanceId); err != nil {
		if err == driver.ErrInstanceNotFound {
			return nil, status.Errorf(codes.NotFound, "instance not found: %s", req.InstanceId)
		}
		return nil, status.Errorf(codes.Internal, "failed to start instance: %v", err)
	}

	// Get updated instance
	instance, err := s.agent.GetInstance(ctx, req.InstanceId)
	if err != nil {
		return nil, status.Errorf(codes.Internal, "failed to get instance after start: %v", err)
	}

	return driverInstanceToProto(instance, s.agent.nodeID), nil
}

// StopInstance stops an instance on this agent.
func (s *AgentGRPCService) StopInstance(ctx context.Context, req *v1.AgentStopInstanceRequest) (*v1.Instance, error) {
	if err := s.agent.StopInstance(ctx, req.InstanceId, req.Force); err != nil {
		if err == driver.ErrInstanceNotFound {
			return nil, status.Errorf(codes.NotFound, "instance not found: %s", req.InstanceId)
		}
		return nil, status.Errorf(codes.Internal, "failed to stop instance: %v", err)
	}

	// Get updated instance
	instance, err := s.agent.GetInstance(ctx, req.InstanceId)
	if err != nil {
		return nil, status.Errorf(codes.Internal, "failed to get instance after stop: %v", err)
	}

	return driverInstanceToProto(instance, s.agent.nodeID), nil
}

// RestartInstance restarts an instance on this agent.
func (s *AgentGRPCService) RestartInstance(ctx context.Context, req *v1.AgentRestartInstanceRequest) (*v1.Instance, error) {
	// Get instance to find driver
	instance, err := s.agent.getInstance(req.InstanceId)
	if err != nil {
		return nil, status.Errorf(codes.NotFound, "instance not found: %s", req.InstanceId)
	}

	d, ok := s.agent.drivers[instance.Type]
	if !ok {
		return nil, status.Errorf(codes.Internal, "unsupported instance type: %s", instance.Type)
	}

	if err := d.Restart(ctx, req.InstanceId, req.Force); err != nil {
		return nil, status.Errorf(codes.Internal, "failed to restart instance: %v", err)
	}

	// Get updated instance
	updated, err := s.agent.GetInstance(ctx, req.InstanceId)
	if err != nil {
		return nil, status.Errorf(codes.Internal, "failed to get instance after restart: %v", err)
	}

	return driverInstanceToProto(updated, s.agent.nodeID), nil
}

// GetInstance retrieves an instance from this agent.
func (s *AgentGRPCService) GetInstance(ctx context.Context, req *v1.AgentInstanceRequest) (*v1.Instance, error) {
	instance, err := s.agent.GetInstance(ctx, req.InstanceId)
	if err != nil {
		if err == driver.ErrInstanceNotFound {
			return nil, status.Errorf(codes.NotFound, "instance not found: %s", req.InstanceId)
		}
		return nil, status.Errorf(codes.Internal, "failed to get instance: %v", err)
	}

	return driverInstanceToProto(instance, s.agent.nodeID), nil
}

// ListInstances lists all instances on this agent.
func (s *AgentGRPCService) ListInstances(ctx context.Context, _ *emptypb.Empty) (*v1.AgentListInstancesResponse, error) {
	instances, err := s.agent.ListInstances(ctx)
	if err != nil {
		return nil, status.Errorf(codes.Internal, "failed to list instances: %v", err)
	}

	protoInstances := make([]*v1.Instance, len(instances))
	for i, instance := range instances {
		protoInstances[i] = driverInstanceToProto(instance, s.agent.nodeID)
	}

	return &v1.AgentListInstancesResponse{
		Instances: protoInstances,
	}, nil
}

// GetInstanceStats retrieves statistics for an instance.
func (s *AgentGRPCService) GetInstanceStats(ctx context.Context, req *v1.AgentInstanceRequest) (*v1.InstanceStats, error) {
	instance, err := s.agent.getInstance(req.InstanceId)
	if err != nil {
		return nil, status.Errorf(codes.NotFound, "instance not found: %s", req.InstanceId)
	}

	d, ok := s.agent.drivers[instance.Type]
	if !ok {
		return nil, status.Errorf(codes.Internal, "unsupported instance type: %s", instance.Type)
	}

	stats, err := d.Stats(ctx, req.InstanceId)
	if err != nil {
		return nil, status.Errorf(codes.Internal, "failed to get instance stats: %v", err)
	}

	return driverStatsToProto(stats), nil
}

// AttachConsole attaches to an instance console (bidirectional streaming).
func (s *AgentGRPCService) AttachConsole(stream v1.AgentService_AttachConsoleServer) error {
	// Read first message to get instance ID
	firstMsg, err := stream.Recv()
	if err != nil {
		return status.Errorf(codes.InvalidArgument, "expected initial message with instance_id")
	}

	// The first message should contain data which is the instance ID
	dataMsg := firstMsg.GetData()
	if dataMsg == nil || len(dataMsg) == 0 {
		return status.Errorf(codes.InvalidArgument, "first message must contain instance_id as data")
	}

	instanceID := string(dataMsg)

	instance, err := s.agent.getInstance(instanceID)
	if err != nil {
		return status.Errorf(codes.NotFound, "instance not found: %s", instanceID)
	}

	d, ok := s.agent.drivers[instance.Type]
	if !ok {
		return status.Errorf(codes.Internal, "unsupported instance type: %s", instance.Type)
	}

	// Attach to console
	conn, err := d.Attach(stream.Context(), instanceID, driver.AttachOptions{
		TTY:    true,
		Stdin:  true,
		Stdout: true,
		Stderr: true,
	})
	if err != nil {
		return status.Errorf(codes.Internal, "failed to attach to console: %v", err)
	}
	defer conn.Close()

	// Handle bidirectional streaming
	errCh := make(chan error, 2)

	// Read from console and send to client
	go func() {
		buf := make([]byte, 4096)
		for {
			n, err := conn.Read(buf)
			if err != nil {
				if err != io.EOF {
					errCh <- fmt.Errorf("console read error: %w", err)
				} else {
					errCh <- nil
				}
				return
			}
			if n > 0 {
				if err := stream.Send(&v1.AgentConsoleOutput{Data: buf[:n]}); err != nil {
					errCh <- fmt.Errorf("stream send error: %w", err)
					return
				}
			}
		}
	}()

	// Read from client and write to console
	go func() {
		for {
			msg, err := stream.Recv()
			if err != nil {
				if err != io.EOF {
					errCh <- fmt.Errorf("stream recv error: %w", err)
				} else {
					errCh <- nil
				}
				return
			}

			switch input := msg.Input.(type) {
			case *v1.AgentConsoleInput_Data:
				if _, err := conn.Write(input.Data); err != nil {
					errCh <- fmt.Errorf("console write error: %w", err)
					return
				}
			case *v1.AgentConsoleInput_Resize:
				// Handle resize - this would need driver support
				// For now, we just ignore resize events
			}
		}
	}()

	// Wait for either goroutine to finish
	if err := <-errCh; err != nil {
		return status.Errorf(codes.Internal, "%v", err)
	}

	return nil
}

// ============================================================================
// Conversion helpers
// ============================================================================

func protoSpecToDriverSpec(spec *v1.InstanceSpec) *driver.InstanceSpec {
	if spec == nil {
		return &driver.InstanceSpec{}
	}

	ds := &driver.InstanceSpec{
		Image:      spec.Image,
		CPUCores:   int(spec.CpuCores),
		MemoryMB:   spec.MemoryBytes / (1024 * 1024),
		Kernel:     spec.Kernel,
		Initrd:     spec.Initrd,
		KernelArgs: spec.KernelArgs,
		Command:    spec.Command,
		Args:       spec.Args,
		Env:        spec.Env,
	}

	// Convert disks
	if len(spec.Disks) > 0 {
		ds.Disks = make([]driver.DiskSpec, len(spec.Disks))
		for i, d := range spec.Disks {
			ds.Disks[i] = driver.DiskSpec{
				Name:   d.Name,
				SizeGB: d.SizeBytes / (1024 * 1024 * 1024),
				Type:   d.Type,
				Boot:   d.Boot,
			}
		}
	}

	// Convert network
	if spec.Network != nil {
		ds.Network = driver.NetworkSpec{
			NetworkID:      spec.Network.NetworkId,
			SubnetID:       spec.Network.SubnetId,
			SecurityGroups: spec.Network.SecurityGroups,
			AssignPublicIP: spec.Network.AssignPublicIp,
		}
	}

	// Convert limits
	if spec.Limits != nil {
		ds.Limits = driver.ResourceLimits{
			CPUQuota:    spec.Limits.CpuQuota,
			CPUPeriod:   spec.Limits.CpuPeriod,
			MemoryLimit: spec.Limits.MemoryLimit,
			IOReadBPS:   spec.Limits.IoReadBps,
			IOWriteBPS:  spec.Limits.IoWriteBps,
		}
	}

	return ds
}

func protoTypeToDriverType(t v1.InstanceType) driver.InstanceType {
	switch t {
	case v1.InstanceType_INSTANCE_TYPE_VM:
		return driver.InstanceTypeVM
	case v1.InstanceType_INSTANCE_TYPE_CONTAINER:
		return driver.InstanceTypeContainer
	case v1.InstanceType_INSTANCE_TYPE_MICROVM:
		return driver.InstanceTypeMicroVM
	default:
		return driver.InstanceTypeVM
	}
}

func driverTypeToProto(t driver.InstanceType) v1.InstanceType {
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

func driverStateToProto(s driver.InstanceState) v1.InstanceState {
	switch s {
	case driver.StatePending:
		return v1.InstanceState_INSTANCE_STATE_PENDING
	case driver.StateCreating:
		return v1.InstanceState_INSTANCE_STATE_CREATING
	case driver.StateRunning:
		return v1.InstanceState_INSTANCE_STATE_RUNNING
	case driver.StateStopped:
		return v1.InstanceState_INSTANCE_STATE_STOPPED
	case driver.StatePaused:
		return v1.InstanceState_INSTANCE_STATE_STOPPED
	case driver.StateFailed:
		return v1.InstanceState_INSTANCE_STATE_FAILED
	default:
		return v1.InstanceState_INSTANCE_STATE_UNSPECIFIED
	}
}

func driverInstanceToProto(instance *driver.Instance, nodeID string) *v1.Instance {
	if instance == nil {
		return nil
	}

	proto := &v1.Instance{
		Id:          instance.ID,
		Name:        instance.Name,
		Type:        driverTypeToProto(instance.Type),
		State:       driverStateToProto(instance.State),
		StateReason: instance.StateReason,
		NodeId:      nodeID,
		IpAddress:   instance.IPAddress,
		CreatedAt:   timestamppb.New(instance.CreatedAt),
	}

	if instance.StartedAt != nil {
		proto.StartedAt = timestamppb.New(*instance.StartedAt)
	}

	// Convert spec
	proto.Spec = &v1.InstanceSpec{
		Image:       instance.Spec.Image,
		CpuCores:    int32(instance.Spec.CPUCores),
		MemoryBytes: instance.Spec.MemoryMB * 1024 * 1024,
		Kernel:      instance.Spec.Kernel,
		Initrd:      instance.Spec.Initrd,
		KernelArgs:  instance.Spec.KernelArgs,
		Command:     instance.Spec.Command,
		Args:        instance.Spec.Args,
		Env:         instance.Spec.Env,
	}

	// Convert metadata
	if len(instance.Metadata) > 0 {
		proto.Metadata = &v1.Metadata{
			Labels: instance.Metadata,
		}
	}

	return proto
}

func driverStatsToProto(stats *driver.InstanceStats) *v1.InstanceStats {
	if stats == nil {
		return nil
	}

	return &v1.InstanceStats{
		InstanceId:       stats.InstanceID,
		CpuUsagePercent:  stats.CPUUsagePercent,
		CpuTimeNs:        int64(stats.CPUTimeNs),
		MemoryUsedBytes:  int64(stats.MemoryUsedBytes),
		MemoryCacheBytes: int64(stats.MemoryCacheBytes),
		DiskReadBytes:    int64(stats.DiskReadBytes),
		DiskWriteBytes:   int64(stats.DiskWriteBytes),
		NetworkRxBytes:   int64(stats.NetworkRxBytes),
		NetworkTxBytes:   int64(stats.NetworkTxBytes),
		CollectedAt:      timestamppb.New(stats.CollectedAt),
	}
}
