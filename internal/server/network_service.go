// Package server provides the hypervisor server implementation.
package server

import (
	"context"
	"fmt"
	"time"

	"go.uber.org/zap"
	"google.golang.org/protobuf/types/known/timestamppb"

	v1 "hypervisor/api/gen"
	"hypervisor/pkg/cluster/etcd"
	"hypervisor/pkg/network"
	"hypervisor/pkg/network/cgo"
	"hypervisor/pkg/network/ipam"
	"hypervisor/pkg/network/overlay"
	"hypervisor/pkg/network/router"
	"hypervisor/pkg/network/sdn"
)

// NetworkService handles network operations in the control plane.
type NetworkService struct {
	etcdClient *etcd.Client
	controller *sdn.Controller
	vxlanMgr   *overlay.VXLANManager
	vtepMgr    *overlay.VTEPManager
	ipam       *ipam.IPAM
	dvr        *router.DVR
	logger     *zap.Logger
}

// NewNetworkService creates a new network service.
func NewNetworkService(etcdClient *etcd.Client, logger *zap.Logger) (*NetworkService, error) {
	// Create IPAM
	ipamMgr := ipam.NewIPAM(etcdClient, logger.Named("ipam"))

	// Create default network config
	config := network.DefaultNetworkConfig()

	// Create OVS bridge wrapper for VXLANManager
	ovsBridge := cgo.NewOVSBridge(config.OVSBridge)

	// Create VXLAN manager with OVS client
	vxlanMgr, err := overlay.NewVXLANManager(config, logger.Named("vxlan"), ovsBridge)
	if err != nil {
		return nil, fmt.Errorf("failed to create VXLAN manager: %w", err)
	}

	// Create VTEP manager
	vtepMgr := overlay.NewVTEPManager(etcdClient, vxlanMgr, logger.Named("vtep"))

	// Create SDN controller
	controller, err := sdn.NewController(config, etcdClient, vxlanMgr, vtepMgr, ipamMgr, logger.Named("sdn"))
	if err != nil {
		return nil, fmt.Errorf("failed to create SDN controller: %w", err)
	}

	// Create DVR
	dvr := router.NewDVR(config, etcdClient, "server-node", logger.Named("dvr"))

	return &NetworkService{
		etcdClient: etcdClient,
		controller: controller,
		vxlanMgr:   vxlanMgr,
		vtepMgr:    vtepMgr,
		ipam:       ipamMgr,
		dvr:        dvr,
		logger:     logger,
	}, nil
}

// Start starts the network service.
func (s *NetworkService) Start() error {
	// Start SDN controller
	if err := s.controller.Start(); err != nil {
		return fmt.Errorf("failed to start SDN controller: %w", err)
	}

	// Start DVR
	if err := s.dvr.Start(); err != nil {
		s.logger.Warn("DVR start failed (may require root)", zap.Error(err))
	}

	s.logger.Info("network service started")
	return nil
}

// Stop stops the network service.
func (s *NetworkService) Stop() error {
	if err := s.controller.Stop(); err != nil {
		s.logger.Warn("failed to stop SDN controller", zap.Error(err))
	}

	if err := s.dvr.Stop(); err != nil {
		s.logger.Warn("failed to stop DVR", zap.Error(err))
	}

	s.logger.Info("network service stopped")
	return nil
}

// CreateNetwork creates a new virtual network.
func (s *NetworkService) CreateNetwork(ctx context.Context, req *v1.CreateNetworkRequest) (*network.Network, error) {
	net := &network.Network{
		ID:       generateID(),
		Name:     req.Name,
		TenantID: req.TenantId,
		Type:     network.NetworkType(req.Type.String()),
		VNI:      req.Vni,
		MTU:      uint16(req.Mtu),
		External: req.External,
		Shared:   req.Shared,
	}

	if err := s.controller.CreateNetwork(ctx, net); err != nil {
		return nil, fmt.Errorf("failed to create network: %w", err)
	}

	return net, nil
}

// GetNetwork retrieves a network by ID.
func (s *NetworkService) GetNetwork(ctx context.Context, networkID string) (*network.Network, error) {
	return s.controller.GetNetwork(ctx, networkID)
}

// ListNetworks lists all networks with optional filters.
func (s *NetworkService) ListNetworks(ctx context.Context, tenantID string) ([]*network.Network, error) {
	return s.controller.ListNetworks(ctx, tenantID)
}

// DeleteNetwork deletes a network.
func (s *NetworkService) DeleteNetwork(ctx context.Context, networkID string) error {
	return s.controller.DeleteNetwork(ctx, networkID)
}

// CreateSubnet creates a new subnet.
func (s *NetworkService) CreateSubnet(ctx context.Context, req *v1.CreateSubnetRequest) (*network.Subnet, error) {
	subnet := &network.Subnet{
		ID:         generateID(),
		Name:       req.Name,
		NetworkID:  req.NetworkId,
		CIDR:       req.Cidr,
		GatewayIP:  req.GatewayIp,
		DNSServers: req.DnsServers,
		EnableDHCP: req.EnableDhcp,
	}

	// Convert allocation pools
	for _, pool := range req.AllocationPools {
		subnet.AllocationPools = append(subnet.AllocationPools, network.IPPool{
			Start: pool.Start,
			End:   pool.End,
		})
	}

	if err := s.ipam.CreateSubnet(ctx, subnet); err != nil {
		return nil, fmt.Errorf("failed to create subnet: %w", err)
	}

	return subnet, nil
}

// GetSubnet retrieves a subnet by ID.
func (s *NetworkService) GetSubnet(ctx context.Context, subnetID string) (*network.Subnet, error) {
	return s.ipam.GetSubnet(ctx, subnetID)
}

// ListSubnets lists all subnets with optional network filter.
func (s *NetworkService) ListSubnets(ctx context.Context, networkID string) ([]*network.Subnet, error) {
	return s.ipam.ListSubnets(ctx, networkID)
}

// DeleteSubnet deletes a subnet.
func (s *NetworkService) DeleteSubnet(ctx context.Context, subnetID string) error {
	return s.ipam.DeleteSubnet(ctx, subnetID)
}

// CreatePort creates a new port.
func (s *NetworkService) CreatePort(ctx context.Context, req *v1.CreatePortRequest) (*network.Port, error) {
	port := &network.Port{
		ID:             generateID(),
		Name:           req.Name,
		NetworkID:      req.NetworkId,
		SubnetID:       req.SubnetId,
		MACAddress:     req.MacAddress,
		IPAddress:      req.IpAddress,
		SecurityGroups: req.SecurityGroups,
	}

	if err := s.controller.CreatePort(ctx, port); err != nil {
		return nil, fmt.Errorf("failed to create port: %w", err)
	}

	return port, nil
}

// GetPort retrieves a port by ID.
func (s *NetworkService) GetPort(ctx context.Context, portID string) (*network.Port, error) {
	return s.controller.GetPort(ctx, portID)
}

// ListPorts lists ports with optional filters.
func (s *NetworkService) ListPorts(ctx context.Context, networkID, instanceID, nodeID string) ([]*network.Port, error) {
	return s.controller.ListPorts(ctx, networkID, instanceID, nodeID)
}

// DeletePort deletes a port.
func (s *NetworkService) DeletePort(ctx context.Context, portID string) error {
	return s.controller.DeletePort(ctx, portID)
}

// BindPort binds a port to an instance.
func (s *NetworkService) BindPort(ctx context.Context, portID, instanceID, nodeID, deviceName string) error {
	return s.controller.BindPort(ctx, portID, instanceID, nodeID, deviceName)
}

// AllocateIP allocates an IP from a subnet.
func (s *NetworkService) AllocateIP(ctx context.Context, subnetID, ipAddress, instanceID, portID string) (*network.IPAllocation, error) {
	return s.ipam.AllocateIP(ctx, subnetID, ipam.AllocationOptions{
		IPAddress:  ipAddress,
		InstanceID: instanceID,
		PortID:     portID,
	})
}

// ReleaseIP releases an allocated IP.
func (s *NetworkService) ReleaseIP(ctx context.Context, subnetID, ipAddress string) error {
	return s.ipam.ReleaseIP(ctx, subnetID, ipAddress)
}

// NetworkGRPCHandler implements the gRPC NetworkService.
type NetworkGRPCHandler struct {
	v1.UnimplementedNetworkServiceServer
	service *NetworkService
}

// NewNetworkGRPCHandler creates a new network gRPC handler.
func NewNetworkGRPCHandler(service *NetworkService) *NetworkGRPCHandler {
	return &NetworkGRPCHandler{service: service}
}

// CreateNetwork implements the gRPC CreateNetwork method.
func (h *NetworkGRPCHandler) CreateNetwork(ctx context.Context, req *v1.CreateNetworkRequest) (*v1.CreateNetworkResponse, error) {
	net, err := h.service.CreateNetwork(ctx, req)
	if err != nil {
		return nil, err
	}

	return &v1.CreateNetworkResponse{
		Network: toProtoNetwork(net),
	}, nil
}

// GetNetwork implements the gRPC GetNetwork method.
func (h *NetworkGRPCHandler) GetNetwork(ctx context.Context, req *v1.GetNetworkRequest) (*v1.GetNetworkResponse, error) {
	net, err := h.service.GetNetwork(ctx, req.NetworkId)
	if err != nil {
		return nil, err
	}

	return &v1.GetNetworkResponse{
		Network: toProtoNetwork(net),
	}, nil
}

// ListNetworks implements the gRPC ListNetworks method.
func (h *NetworkGRPCHandler) ListNetworks(ctx context.Context, req *v1.ListNetworksRequest) (*v1.ListNetworksResponse, error) {
	networks, err := h.service.ListNetworks(ctx, req.TenantId)
	if err != nil {
		return nil, err
	}

	protoNetworks := make([]*v1.Network, len(networks))
	for i, net := range networks {
		protoNetworks[i] = toProtoNetwork(net)
	}

	return &v1.ListNetworksResponse{
		Networks: protoNetworks,
	}, nil
}

// DeleteNetwork implements the gRPC DeleteNetwork method.
func (h *NetworkGRPCHandler) DeleteNetwork(ctx context.Context, req *v1.DeleteNetworkRequest) (*v1.DeleteNetworkResponse, error) {
	if err := h.service.DeleteNetwork(ctx, req.NetworkId); err != nil {
		return nil, err
	}
	return &v1.DeleteNetworkResponse{}, nil
}

// CreateSubnet implements the gRPC CreateSubnet method.
func (h *NetworkGRPCHandler) CreateSubnet(ctx context.Context, req *v1.CreateSubnetRequest) (*v1.CreateSubnetResponse, error) {
	subnet, err := h.service.CreateSubnet(ctx, req)
	if err != nil {
		return nil, err
	}

	return &v1.CreateSubnetResponse{
		Subnet: toProtoSubnet(subnet),
	}, nil
}

// GetSubnet implements the gRPC GetSubnet method.
func (h *NetworkGRPCHandler) GetSubnet(ctx context.Context, req *v1.GetSubnetRequest) (*v1.GetSubnetResponse, error) {
	subnet, err := h.service.GetSubnet(ctx, req.SubnetId)
	if err != nil {
		return nil, err
	}

	return &v1.GetSubnetResponse{
		Subnet: toProtoSubnet(subnet),
	}, nil
}

// ListSubnets implements the gRPC ListSubnets method.
func (h *NetworkGRPCHandler) ListSubnets(ctx context.Context, req *v1.ListSubnetsRequest) (*v1.ListSubnetsResponse, error) {
	subnets, err := h.service.ListSubnets(ctx, req.NetworkId)
	if err != nil {
		return nil, err
	}

	protoSubnets := make([]*v1.Subnet, len(subnets))
	for i, subnet := range subnets {
		protoSubnets[i] = toProtoSubnet(subnet)
	}

	return &v1.ListSubnetsResponse{
		Subnets: protoSubnets,
	}, nil
}

// DeleteSubnet implements the gRPC DeleteSubnet method.
func (h *NetworkGRPCHandler) DeleteSubnet(ctx context.Context, req *v1.DeleteSubnetRequest) (*v1.DeleteSubnetResponse, error) {
	if err := h.service.DeleteSubnet(ctx, req.SubnetId); err != nil {
		return nil, err
	}
	return &v1.DeleteSubnetResponse{}, nil
}

// CreatePort implements the gRPC CreatePort method.
func (h *NetworkGRPCHandler) CreatePort(ctx context.Context, req *v1.CreatePortRequest) (*v1.CreatePortResponse, error) {
	port, err := h.service.CreatePort(ctx, req)
	if err != nil {
		return nil, err
	}

	return &v1.CreatePortResponse{
		Port: toProtoPort(port),
	}, nil
}

// GetPort implements the gRPC GetPort method.
func (h *NetworkGRPCHandler) GetPort(ctx context.Context, req *v1.GetPortRequest) (*v1.GetPortResponse, error) {
	port, err := h.service.GetPort(ctx, req.PortId)
	if err != nil {
		return nil, err
	}

	return &v1.GetPortResponse{
		Port: toProtoPort(port),
	}, nil
}

// ListPorts implements the gRPC ListPorts method.
func (h *NetworkGRPCHandler) ListPorts(ctx context.Context, req *v1.ListPortsRequest) (*v1.ListPortsResponse, error) {
	ports, err := h.service.ListPorts(ctx, req.NetworkId, req.InstanceId, req.NodeId)
	if err != nil {
		return nil, err
	}

	protoPorts := make([]*v1.Port, len(ports))
	for i, port := range ports {
		protoPorts[i] = toProtoPort(port)
	}

	return &v1.ListPortsResponse{
		Ports: protoPorts,
	}, nil
}

// DeletePort implements the gRPC DeletePort method.
func (h *NetworkGRPCHandler) DeletePort(ctx context.Context, req *v1.DeletePortRequest) (*v1.DeletePortResponse, error) {
	if err := h.service.DeletePort(ctx, req.PortId); err != nil {
		return nil, err
	}
	return &v1.DeletePortResponse{}, nil
}

// AllocateIP implements the gRPC AllocateIP method.
func (h *NetworkGRPCHandler) AllocateIP(ctx context.Context, req *v1.AllocateIPRequest) (*v1.AllocateIPResponse, error) {
	alloc, err := h.service.AllocateIP(ctx, req.SubnetId, req.IpAddress, req.InstanceId, req.PortId)
	if err != nil {
		return nil, err
	}

	return &v1.AllocateIPResponse{
		Allocation: &v1.IPAllocation{
			Id:         alloc.ID,
			SubnetId:   alloc.SubnetID,
			IpAddress:  alloc.IPAddress,
			MacAddress: alloc.MACAddress,
			InstanceId: alloc.InstanceID,
			PortId:     alloc.PortID,
			Hostname:   alloc.Hostname,
			Status:     alloc.Status,
			CreatedAt:  timestamppb.New(alloc.CreatedAt),
		},
	}, nil
}

// ReleaseIP implements the gRPC ReleaseIP method.
func (h *NetworkGRPCHandler) ReleaseIP(ctx context.Context, req *v1.ReleaseIPRequest) (*v1.ReleaseIPResponse, error) {
	if err := h.service.ReleaseIP(ctx, req.SubnetId, req.IpAddress); err != nil {
		return nil, err
	}
	return &v1.ReleaseIPResponse{}, nil
}

// Helper functions to convert between internal and proto types

func toProtoNetwork(n *network.Network) *v1.Network {
	return &v1.Network{
		Id:         n.ID,
		Name:       n.Name,
		TenantId:   n.TenantID,
		Type:       v1.NetworkType(v1.NetworkType_value[string(n.Type)]),
		Vni:        n.VNI,
		Mtu:        uint32(n.MTU),
		External:   n.External,
		Shared:     n.Shared,
		AdminState: n.AdminState,
		CreatedAt:  timestamppb.New(n.CreatedAt),
		UpdatedAt:  timestamppb.New(n.UpdatedAt),
	}
}

func toProtoSubnet(s *network.Subnet) *v1.Subnet {
	pools := make([]*v1.IPPool, len(s.AllocationPools))
	for i, pool := range s.AllocationPools {
		pools[i] = &v1.IPPool{
			Start: pool.Start,
			End:   pool.End,
		}
	}

	return &v1.Subnet{
		Id:              s.ID,
		Name:            s.Name,
		NetworkId:       s.NetworkID,
		Cidr:            s.CIDR,
		GatewayIp:       s.GatewayIP,
		DnsServers:      s.DNSServers,
		AllocationPools: pools,
		EnableDhcp:      s.EnableDHCP,
		Ipv6:            s.IPv6,
		CreatedAt:       timestamppb.New(s.CreatedAt),
		UpdatedAt:       timestamppb.New(s.UpdatedAt),
	}
}

func toProtoPort(p *network.Port) *v1.Port {
	return &v1.Port{
		Id:             p.ID,
		Name:           p.Name,
		NetworkId:      p.NetworkID,
		SubnetId:       p.SubnetID,
		MacAddress:     p.MACAddress,
		IpAddress:      p.IPAddress,
		InstanceId:     p.InstanceID,
		NodeId:         p.NodeID,
		DeviceName:     p.DeviceName,
		SecurityGroups: p.SecurityGroups,
		Status:         p.Status,
		AdminState:     p.AdminState,
		CreatedAt:      timestamppb.New(p.CreatedAt),
		UpdatedAt:      timestamppb.New(p.UpdatedAt),
	}
}

// generateID generates a unique ID for network resources.
func generateID() string {
	return fmt.Sprintf("%d", time.Now().UnixNano())
}
