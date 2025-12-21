// Package server provides the hypervisor server implementation.
package server

import (
	"context"
	"fmt"
	"net"
	"sync"

	v1 "hypervisor/api/gen"
	"hypervisor/pkg/cluster/etcd"
	"hypervisor/pkg/cluster/heartbeat"
	"hypervisor/pkg/cluster/registry"
	"hypervisor/pkg/compute/driver"

	"go.uber.org/zap"
	"google.golang.org/grpc"
	"google.golang.org/grpc/reflection"
)

// Config holds the server configuration.
type Config struct {
	// GRPCAddr is the address for the gRPC server.
	GRPCAddr string `mapstructure:"grpc_addr"`

	// HTTPAddr is the address for the HTTP/REST gateway.
	HTTPAddr string `mapstructure:"http_addr"`

	// Etcd configuration
	Etcd etcd.Config `mapstructure:"etcd"`

	// Heartbeat configuration
	Heartbeat heartbeat.Config `mapstructure:"heartbeat"`
}

// DefaultConfig returns the default server configuration.
func DefaultConfig() Config {
	return Config{
		GRPCAddr:  ":50051",
		HTTPAddr:  ":8080",
		Etcd:      etcd.DefaultConfig(),
		Heartbeat: heartbeat.DefaultConfig(),
	}
}

// Server is the hypervisor control plane server.
type Server struct {
	config Config
	logger *zap.Logger

	// gRPC server
	grpcServer *grpc.Server

	// Cluster components
	etcdClient       *etcd.Client
	registry         *registry.EtcdRegistry
	instanceRegistry *registry.EtcdInstanceRegistry
	monitor          *heartbeat.Monitor

	// Agent client pool
	agentClients *AgentClientPool

	// Network service
	networkService *NetworkService

	// Compute drivers (for managing instances across the cluster)
	drivers map[driver.InstanceType]driver.Driver

	mu      sync.RWMutex
	running bool
}

// New creates a new hypervisor server.
func New(config Config, logger *zap.Logger) (*Server, error) {
	if logger == nil {
		logger = zap.NewNop()
	}

	// Connect to etcd
	etcdClient, err := etcd.New(config.Etcd, logger.Named("etcd"))
	if err != nil {
		return nil, fmt.Errorf("failed to connect to etcd: %w", err)
	}

	// Create registry
	reg := registry.NewEtcdRegistry(etcdClient, logger.Named("registry"))

	// Create instance registry
	instanceReg := registry.NewEtcdInstanceRegistry(etcdClient, logger.Named("instance-registry"))

	// Create agent client pool
	agentClients := NewAgentClientPool(reg, logger.Named("agent-clients"))

	// Create heartbeat monitor
	monitor := heartbeat.NewMonitor(reg, config.Heartbeat, func(nodeID string, alive bool) {
		if !alive {
			logger.Warn("node is down", zap.String("node_id", nodeID))
			// TODO: Reschedule instances from the dead node
		}
	}, logger.Named("monitor"))

	// Create network service
	networkService, err := NewNetworkService(etcdClient, logger.Named("network"))
	if err != nil {
		logger.Warn("failed to create network service (networking features will be unavailable)", zap.Error(err))
	}

	s := &Server{
		config:           config,
		logger:           logger,
		etcdClient:       etcdClient,
		registry:         reg,
		instanceRegistry: instanceReg,
		agentClients:     agentClients,
		monitor:          monitor,
		networkService:   networkService,
		drivers:          make(map[driver.InstanceType]driver.Driver),
	}

	// Create gRPC server with interceptors
	s.grpcServer = grpc.NewServer(
		grpc.UnaryInterceptor(s.unaryInterceptor),
		grpc.StreamInterceptor(s.streamInterceptor),
	)

	// Register services
	s.registerServices()

	// Enable reflection for debugging
	reflection.Register(s.grpcServer)

	return s, nil
}

// registerServices registers gRPC services.
func (s *Server) registerServices() {
	// Register ClusterService
	clusterService := NewClusterService(s.registry, s.logger.Named("cluster"))
	clusterHandler := NewClusterGRPCHandler(clusterService)
	v1.RegisterClusterServiceServer(s.grpcServer, clusterHandler)

	// Register ComputeService
	computeService := NewComputeService(s.registry, s.instanceRegistry, s.agentClients, s.logger.Named("compute"))
	computeHandler := NewComputeGRPCHandler(computeService)
	v1.RegisterComputeServiceServer(s.grpcServer, computeHandler)

	// Register NetworkService
	if s.networkService != nil {
		networkHandler := NewNetworkGRPCHandler(s.networkService)
		v1.RegisterNetworkServiceServer(s.grpcServer, networkHandler)
	}
}

// Start starts the server.
func (s *Server) Start(ctx context.Context) error {
	s.mu.Lock()
	if s.running {
		s.mu.Unlock()
		return nil
	}
	s.running = true
	s.mu.Unlock()

	// Start heartbeat monitor
	if err := s.monitor.Start(ctx); err != nil {
		return fmt.Errorf("failed to start heartbeat monitor: %w", err)
	}

	// Start network service
	if s.networkService != nil {
		if err := s.networkService.Start(); err != nil {
			s.logger.Warn("failed to start network service", zap.Error(err))
		}
	}

	// Start gRPC server
	listener, err := net.Listen("tcp", s.config.GRPCAddr)
	if err != nil {
		return fmt.Errorf("failed to listen on %s: %w", s.config.GRPCAddr, err)
	}

	s.logger.Info("starting gRPC server", zap.String("addr", s.config.GRPCAddr))

	go func() {
		if err := s.grpcServer.Serve(listener); err != nil {
			s.logger.Error("gRPC server error", zap.Error(err))
		}
	}()

	return nil
}

// Stop stops the server.
func (s *Server) Stop() error {
	s.mu.Lock()
	defer s.mu.Unlock()

	if !s.running {
		return nil
	}

	s.running = false

	// Stop heartbeat monitor
	s.monitor.Stop()

	// Stop network service
	if s.networkService != nil {
		s.networkService.Stop()
	}

	// Close agent clients
	if s.agentClients != nil {
		s.agentClients.Close()
	}

	// Gracefully stop gRPC server
	s.grpcServer.GracefulStop()

	// Close instance registry
	if s.instanceRegistry != nil {
		s.instanceRegistry.Close()
	}

	// Close registry
	s.registry.Close()

	// Close etcd client
	s.etcdClient.Close()

	s.logger.Info("server stopped")
	return nil
}

// unaryInterceptor is a gRPC unary interceptor for logging and error handling.
func (s *Server) unaryInterceptor(
	ctx context.Context,
	req interface{},
	info *grpc.UnaryServerInfo,
	handler grpc.UnaryHandler,
) (interface{}, error) {
	s.logger.Debug("gRPC request",
		zap.String("method", info.FullMethod),
	)

	resp, err := handler(ctx, req)
	if err != nil {
		s.logger.Error("gRPC error",
			zap.String("method", info.FullMethod),
			zap.Error(err),
		)
	}

	return resp, err
}

// streamInterceptor is a gRPC stream interceptor for logging.
func (s *Server) streamInterceptor(
	srv interface{},
	ss grpc.ServerStream,
	info *grpc.StreamServerInfo,
	handler grpc.StreamHandler,
) error {
	s.logger.Debug("gRPC stream",
		zap.String("method", info.FullMethod),
	)

	return handler(srv, ss)
}
