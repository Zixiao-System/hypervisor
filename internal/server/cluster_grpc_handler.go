package server

import (
	"context"

	v1 "hypervisor/api/gen"
	"hypervisor/pkg/cluster/registry"

	"google.golang.org/protobuf/types/known/emptypb"
	"google.golang.org/protobuf/types/known/timestamppb"
)

// ClusterGRPCHandler adapts ClusterService to the proto-generated interface.
type ClusterGRPCHandler struct {
	v1.UnimplementedClusterServiceServer
	service *ClusterService
}

// NewClusterGRPCHandler creates a new ClusterGRPCHandler.
func NewClusterGRPCHandler(service *ClusterService) *ClusterGRPCHandler {
	return &ClusterGRPCHandler{service: service}
}

// RegisterNode implements v1.ClusterServiceServer.
func (h *ClusterGRPCHandler) RegisterNode(ctx context.Context, req *v1.RegisterNodeRequest) (*v1.RegisterNodeResponse, error) {
	// Convert proto request to service request
	serviceReq := &RegisterNodeRequest{
		Hostname: req.Hostname,
		IP:       req.Ip,
		Port:     int(req.Port),
		Role:     protoRoleToRegistryRole(req.Role),
		Region:   req.Region,
		Zone:     req.Zone,
	}

	if req.Capacity != nil {
		serviceReq.Capacity = protoResourcesToRegistry(req.Capacity)
	}

	if req.Metadata != nil {
		serviceReq.Labels = req.Metadata.Labels
	}

	// Convert supported instance types
	for _, t := range req.SupportedInstanceTypes {
		serviceReq.SupportedInstanceTypes = append(serviceReq.SupportedInstanceTypes, registry.InstanceType(t))
	}

	resp, err := h.service.RegisterNode(ctx, serviceReq)
	if err != nil {
		return nil, err
	}

	return &v1.RegisterNodeResponse{
		NodeId:                   resp.NodeID,
		HeartbeatIntervalSeconds: resp.HeartbeatIntervalSeconds,
		LeaseTtlSeconds:          resp.LeaseTTLSeconds,
	}, nil
}

// DeregisterNode implements v1.ClusterServiceServer.
func (h *ClusterGRPCHandler) DeregisterNode(ctx context.Context, req *v1.DeregisterNodeRequest) (*emptypb.Empty, error) {
	err := h.service.DeregisterNode(ctx, &DeregisterNodeRequest{
		NodeID: req.NodeId,
	})
	if err != nil {
		return nil, err
	}
	return &emptypb.Empty{}, nil
}

// GetNode implements v1.ClusterServiceServer.
func (h *ClusterGRPCHandler) GetNode(ctx context.Context, req *v1.GetNodeRequest) (*v1.Node, error) {
	node, err := h.service.GetNode(ctx, &GetNodeRequest{
		NodeID: req.NodeId,
	})
	if err != nil {
		return nil, err
	}
	return registryNodeToProto(node), nil
}

// ListNodes implements v1.ClusterServiceServer.
func (h *ClusterGRPCHandler) ListNodes(ctx context.Context, req *v1.ListNodesRequest) (*v1.ListNodesResponse, error) {
	resp, err := h.service.ListNodes(ctx, &ListNodesRequest{
		Role:          protoRoleToRegistryRole(req.Role),
		Status:        protoStatusToRegistryStatus(req.Status),
		Region:        req.Region,
		Zone:          req.Zone,
		LabelSelector: req.LabelSelector,
		PageSize:      int(req.PageSize),
		PageToken:     req.PageToken,
	})
	if err != nil {
		return nil, err
	}

	nodes := make([]*v1.Node, len(resp.Nodes))
	for i, node := range resp.Nodes {
		nodes[i] = registryNodeToProto(node)
	}

	return &v1.ListNodesResponse{
		Nodes:         nodes,
		NextPageToken: resp.NextPageToken,
		TotalCount:    int32(resp.TotalCount),
	}, nil
}

// UpdateNodeStatus implements v1.ClusterServiceServer.
func (h *ClusterGRPCHandler) UpdateNodeStatus(ctx context.Context, req *v1.UpdateNodeStatusRequest) (*v1.Node, error) {
	node, err := h.service.UpdateNodeStatus(ctx, &UpdateNodeStatusRequest{
		NodeID:     req.NodeId,
		Status:     protoStatusToRegistryStatus(req.Status),
		Conditions: protoConditionsToRegistry(req.Conditions),
		Allocated:  protoResourcesToRegistry(req.Allocated),
	})
	if err != nil {
		return nil, err
	}
	return registryNodeToProto(node), nil
}

// Heartbeat implements v1.ClusterServiceServer.
func (h *ClusterGRPCHandler) Heartbeat(ctx context.Context, req *v1.HeartbeatRequest) (*v1.HeartbeatResponse, error) {
	resp, err := h.service.Heartbeat(ctx, &HeartbeatRequest{
		NodeID:     req.NodeId,
		Status:     protoStatusToRegistryStatus(req.Status),
		Conditions: protoConditionsToRegistry(req.Conditions),
		Allocated:  protoResourcesToRegistry(req.Allocated),
	})
	if err != nil {
		return nil, err
	}

	commands := make([]*v1.NodeCommand, len(resp.Commands))
	for i, cmd := range resp.Commands {
		commands[i] = &v1.NodeCommand{
			Id:         cmd.ID,
			Type:       cmd.Type,
			Parameters: cmd.Parameters,
		}
	}

	return &v1.HeartbeatResponse{
		Accepted:             resp.Accepted,
		NextHeartbeatSeconds: resp.NextHeartbeatSeconds,
		Commands:             commands,
	}, nil
}

// WatchNodes implements v1.ClusterServiceServer.
func (h *ClusterGRPCHandler) WatchNodes(req *v1.WatchNodesRequest, stream v1.ClusterService_WatchNodesServer) error {
	return h.service.WatchNodes(stream.Context(), &WatchNodesRequest{
		Role:   protoRoleToRegistryRole(req.Role),
		Region: req.Region,
		Zone:   req.Zone,
	}, func(event *registry.NodeEvent) error {
		return stream.Send(&v1.NodeEvent{
			Type: registryEventTypeToProto(event.Type),
			Node: registryNodeToProto(event.Node),
		})
	})
}

// GetClusterInfo implements v1.ClusterServiceServer.
func (h *ClusterGRPCHandler) GetClusterInfo(ctx context.Context, _ *emptypb.Empty) (*v1.ClusterInfo, error) {
	info, err := h.service.GetClusterInfo(ctx)
	if err != nil {
		return nil, err
	}

	return &v1.ClusterInfo{
		ClusterId:      info.ClusterID,
		ClusterName:    info.ClusterName,
		Version:        info.Version,
		TotalNodes:     int32(info.TotalNodes),
		ReadyNodes:     int32(info.ReadyNodes),
		TotalCapacity:  registryResourcesToProto(info.TotalCapacity),
		TotalAllocated: registryResourcesToProto(info.TotalAllocated),
	}, nil
}

// ============================================================================
// Conversion helpers
// ============================================================================

func protoRoleToRegistryRole(r v1.NodeRole) registry.NodeRole {
	switch r {
	case v1.NodeRole_NODE_ROLE_MASTER:
		return registry.NodeRoleMaster
	case v1.NodeRole_NODE_ROLE_WORKER:
		return registry.NodeRoleWorker
	default:
		return ""
	}
}

func registryRoleToProto(r registry.NodeRole) v1.NodeRole {
	switch r {
	case registry.NodeRoleMaster:
		return v1.NodeRole_NODE_ROLE_MASTER
	case registry.NodeRoleWorker:
		return v1.NodeRole_NODE_ROLE_WORKER
	default:
		return v1.NodeRole_NODE_ROLE_UNSPECIFIED
	}
}

func protoStatusToRegistryStatus(s v1.NodeStatus) registry.NodeStatus {
	switch s {
	case v1.NodeStatus_NODE_STATUS_READY:
		return registry.NodeStatusReady
	case v1.NodeStatus_NODE_STATUS_NOT_READY:
		return registry.NodeStatusNotReady
	case v1.NodeStatus_NODE_STATUS_MAINTENANCE:
		return registry.NodeStatusMaintenance
	case v1.NodeStatus_NODE_STATUS_DRAINING:
		return registry.NodeStatusDraining
	default:
		return ""
	}
}

func registryStatusToProto(s registry.NodeStatus) v1.NodeStatus {
	switch s {
	case registry.NodeStatusReady:
		return v1.NodeStatus_NODE_STATUS_READY
	case registry.NodeStatusNotReady:
		return v1.NodeStatus_NODE_STATUS_NOT_READY
	case registry.NodeStatusMaintenance:
		return v1.NodeStatus_NODE_STATUS_MAINTENANCE
	case registry.NodeStatusDraining:
		return v1.NodeStatus_NODE_STATUS_DRAINING
	default:
		return v1.NodeStatus_NODE_STATUS_UNSPECIFIED
	}
}

func protoResourcesToRegistry(r *v1.Resources) registry.Resources {
	if r == nil {
		return registry.Resources{}
	}
	return registry.Resources{
		CPUCores:    int(r.CpuCores),
		MemoryBytes: r.MemoryBytes,
		DiskBytes:   r.DiskBytes,
		GPUCount:    int(r.GpuCount),
	}
}

func registryResourcesToProto(r registry.Resources) *v1.Resources {
	return &v1.Resources{
		CpuCores:    int32(r.CPUCores),
		MemoryBytes: r.MemoryBytes,
		DiskBytes:   r.DiskBytes,
		GpuCount:    int32(r.GPUCount),
	}
}

func protoConditionsToRegistry(conditions []*v1.NodeCondition) []registry.NodeCondition {
	if conditions == nil {
		return nil
	}

	result := make([]registry.NodeCondition, len(conditions))
	for i, c := range conditions {
		result[i] = registry.NodeCondition{
			Type:    registry.ConditionType(c.Type),
			Status:  registry.ConditionStatus(c.Status),
			Reason:  c.Reason,
			Message: c.Message,
		}
		if c.LastTransitionTime != nil {
			result[i].LastTransitionTime = c.LastTransitionTime.AsTime()
		}
	}
	return result
}

func registryConditionsToProto(conditions []registry.NodeCondition) []*v1.NodeCondition {
	if conditions == nil {
		return nil
	}

	result := make([]*v1.NodeCondition, len(conditions))
	for i, c := range conditions {
		result[i] = &v1.NodeCondition{
			Type:               string(c.Type),
			Status:             string(c.Status),
			Reason:             c.Reason,
			Message:            c.Message,
			LastTransitionTime: timestamppb.New(c.LastTransitionTime),
		}
	}
	return result
}

func registryNodeToProto(node *registry.Node) *v1.Node {
	if node == nil {
		return nil
	}

	proto := &v1.Node{
		Id:          node.ID,
		Hostname:    node.Hostname,
		Ip:          node.IP,
		Port:        int32(node.Port),
		Role:        registryRoleToProto(node.Role),
		Status:      registryStatusToProto(node.Status),
		Region:      node.Region,
		Zone:        node.Zone,
		Capacity:    registryResourcesToProto(node.Capacity),
		Allocatable: registryResourcesToProto(node.Allocatable),
		Allocated:   registryResourcesToProto(node.Allocated),
		Conditions:  registryConditionsToProto(node.Conditions),
		CreatedAt:   timestamppb.New(node.CreatedAt),
		LastSeen:    timestamppb.New(node.LastSeen),
	}

	// Convert metadata
	if len(node.Labels) > 0 || len(node.Annotations) > 0 {
		proto.Metadata = &v1.Metadata{
			Labels:      node.Labels,
			Annotations: node.Annotations,
		}
	}

	// Convert supported instance types
	for _, t := range node.SupportedInstanceTypes {
		proto.SupportedInstanceTypes = append(proto.SupportedInstanceTypes, string(t))
	}

	return proto
}

func registryEventTypeToProto(t registry.EventType) v1.EventType {
	switch t {
	case registry.EventAdded:
		return v1.EventType_EVENT_TYPE_ADDED
	case registry.EventModified:
		return v1.EventType_EVENT_TYPE_MODIFIED
	case registry.EventDeleted:
		return v1.EventType_EVENT_TYPE_DELETED
	default:
		return v1.EventType_EVENT_TYPE_UNSPECIFIED
	}
}
