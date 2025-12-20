package server

import (
	"context"

	v1 "hypervisor/api/gen"
	"hypervisor/pkg/cluster/registry"
	"hypervisor/pkg/compute/driver"

	"google.golang.org/protobuf/types/known/emptypb"
	"google.golang.org/protobuf/types/known/timestamppb"
)

// ComputeGRPCHandler adapts ComputeService to the proto-generated interface.
type ComputeGRPCHandler struct {
	v1.UnimplementedComputeServiceServer
	service *ComputeService
}

// NewComputeGRPCHandler creates a new ComputeGRPCHandler.
func NewComputeGRPCHandler(service *ComputeService) *ComputeGRPCHandler {
	return &ComputeGRPCHandler{service: service}
}

// CreateInstance implements v1.ComputeServiceServer.
func (h *ComputeGRPCHandler) CreateInstance(ctx context.Context, req *v1.CreateInstanceRequest) (*v1.Instance, error) {
	// Convert proto request to service request
	serviceReq := &CreateInstanceRequest{
		Name:            req.Name,
		Type:            protoTypeToDriverType(req.Type),
		Spec:            protoSpecToDriverSpec(req.Spec),
		Metadata:        protoMetadataToLabels(req.Metadata),
		PreferredNodeID: req.PreferredNodeId,
		Region:          req.Region,
		Zone:            req.Zone,
	}

	instance, err := h.service.CreateInstance(ctx, serviceReq)
	if err != nil {
		return nil, err
	}

	return registryInstanceToProto(instance), nil
}

// DeleteInstance implements v1.ComputeServiceServer.
func (h *ComputeGRPCHandler) DeleteInstance(ctx context.Context, req *v1.DeleteInstanceRequest) (*emptypb.Empty, error) {
	err := h.service.DeleteInstance(ctx, &DeleteInstanceRequest{
		InstanceID: req.InstanceId,
		Force:      req.Force,
	})
	if err != nil {
		return nil, err
	}
	return &emptypb.Empty{}, nil
}

// GetInstance implements v1.ComputeServiceServer.
func (h *ComputeGRPCHandler) GetInstance(ctx context.Context, req *v1.GetInstanceRequest) (*v1.Instance, error) {
	instance, err := h.service.GetInstance(ctx, &GetInstanceRequest{
		InstanceID: req.InstanceId,
	})
	if err != nil {
		return nil, err
	}
	return registryInstanceToProto(instance), nil
}

// ListInstances implements v1.ComputeServiceServer.
func (h *ComputeGRPCHandler) ListInstances(ctx context.Context, req *v1.ListInstancesRequest) (*v1.ListInstancesResponse, error) {
	resp, err := h.service.ListInstances(ctx, &ListInstancesRequest{
		Type:          protoTypeToDriverType(req.Type),
		State:         protoStateToDriverState(req.State),
		NodeID:        req.NodeId,
		LabelSelector: req.LabelSelector,
		PageSize:      int(req.PageSize),
		PageToken:     req.PageToken,
	})
	if err != nil {
		return nil, err
	}

	instances := make([]*v1.Instance, len(resp.Instances))
	for i, inst := range resp.Instances {
		instances[i] = registryInstanceToProto(inst)
	}

	return &v1.ListInstancesResponse{
		Instances:     instances,
		NextPageToken: resp.NextPageToken,
		TotalCount:    int32(resp.TotalCount),
	}, nil
}

// StartInstance implements v1.ComputeServiceServer.
func (h *ComputeGRPCHandler) StartInstance(ctx context.Context, req *v1.StartInstanceRequest) (*v1.Instance, error) {
	instance, err := h.service.StartInstance(ctx, &StartInstanceRequest{
		InstanceID: req.InstanceId,
	})
	if err != nil {
		return nil, err
	}
	return registryInstanceToProto(instance), nil
}

// StopInstance implements v1.ComputeServiceServer.
func (h *ComputeGRPCHandler) StopInstance(ctx context.Context, req *v1.StopInstanceRequest) (*v1.Instance, error) {
	instance, err := h.service.StopInstance(ctx, &StopInstanceRequest{
		InstanceID:     req.InstanceId,
		Force:          req.Force,
		TimeoutSeconds: int(req.TimeoutSeconds),
	})
	if err != nil {
		return nil, err
	}
	return registryInstanceToProto(instance), nil
}

// RestartInstance implements v1.ComputeServiceServer.
func (h *ComputeGRPCHandler) RestartInstance(ctx context.Context, req *v1.RestartInstanceRequest) (*v1.Instance, error) {
	instance, err := h.service.RestartInstance(ctx, &RestartInstanceRequest{
		InstanceID: req.InstanceId,
		Force:      req.Force,
	})
	if err != nil {
		return nil, err
	}
	return registryInstanceToProto(instance), nil
}

// GetInstanceStats implements v1.ComputeServiceServer.
func (h *ComputeGRPCHandler) GetInstanceStats(ctx context.Context, req *v1.GetInstanceStatsRequest) (*v1.InstanceStats, error) {
	stats, err := h.service.GetInstanceStats(ctx, &GetInstanceStatsRequest{
		InstanceID: req.InstanceId,
	})
	if err != nil {
		return nil, err
	}
	return driverStatsToProtoStats(stats), nil
}

// ============================================================================
// Conversion helpers
// ============================================================================

func protoTypeToDriverType(t v1.InstanceType) driver.InstanceType {
	switch t {
	case v1.InstanceType_INSTANCE_TYPE_VM:
		return driver.InstanceTypeVM
	case v1.InstanceType_INSTANCE_TYPE_CONTAINER:
		return driver.InstanceTypeContainer
	case v1.InstanceType_INSTANCE_TYPE_MICROVM:
		return driver.InstanceTypeMicroVM
	default:
		return ""
	}
}

func protoSpecToDriverSpec(spec *v1.InstanceSpec) driver.InstanceSpec {
	if spec == nil {
		return driver.InstanceSpec{}
	}

	ds := driver.InstanceSpec{
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

func protoMetadataToLabels(m *v1.Metadata) map[string]string {
	if m == nil {
		return nil
	}
	return m.Labels
}

func registryInstanceToProto(inst *registry.Instance) *v1.Instance {
	if inst == nil {
		return nil
	}

	proto := &v1.Instance{
		Id:          inst.ID,
		Name:        inst.Name,
		Type:        driverTypeToProtoType(inst.Type),
		State:       driverStateToProtoState(inst.State),
		StateReason: inst.StateReason,
		NodeId:      inst.NodeID,
		IpAddress:   inst.IPAddress,
		CreatedAt:   timestamppb.New(inst.CreatedAt),
	}

	if inst.StartedAt != nil {
		proto.StartedAt = timestamppb.New(*inst.StartedAt)
	}

	// Convert spec
	proto.Spec = driverSpecToProtoSpec(&inst.Spec)

	// Convert metadata
	if len(inst.Labels) > 0 || len(inst.Annotations) > 0 {
		proto.Metadata = &v1.Metadata{
			Labels:      inst.Labels,
			Annotations: inst.Annotations,
		}
	}

	return proto
}

func driverStateToProtoState(s driver.InstanceState) v1.InstanceState {
	switch s {
	case driver.StatePending:
		return v1.InstanceState_INSTANCE_STATE_PENDING
	case driver.StateCreating:
		return v1.InstanceState_INSTANCE_STATE_CREATING
	case driver.StateRunning:
		return v1.InstanceState_INSTANCE_STATE_RUNNING
	case driver.StateStopped:
		return v1.InstanceState_INSTANCE_STATE_STOPPED
	case driver.StateFailed:
		return v1.InstanceState_INSTANCE_STATE_FAILED
	default:
		return v1.InstanceState_INSTANCE_STATE_UNSPECIFIED
	}
}

func driverStatsToProtoStats(stats *driver.InstanceStats) *v1.InstanceStats {
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
