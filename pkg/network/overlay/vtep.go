package overlay

import (
	"context"
	"encoding/json"
	"fmt"
	"net"
	"sync"
	"time"

	"go.uber.org/zap"

	"hypervisor/pkg/cluster/etcd"
	"hypervisor/pkg/network"
)

const (
	vtepKeyPrefix  = "/hypervisor/network/vteps/"
	vtepTTL        = 60 // seconds
	vtepRefreshInterval = 30 * time.Second
)

// VTEPManager manages VTEP registration and discovery across the cluster.
type VTEPManager struct {
	etcdClient   *etcd.Client
	logger       *zap.Logger
	vxlanMgr     *VXLANManager

	localVTEP    *network.VTEP
	remoteVTEPs  map[string]*network.VTEP // indexed by node ID
	vtepsMu      sync.RWMutex

	ctx          context.Context
	cancel       context.CancelFunc
	wg           sync.WaitGroup
}

// NewVTEPManager creates a new VTEP manager.
func NewVTEPManager(
	etcdClient *etcd.Client,
	vxlanMgr *VXLANManager,
	logger *zap.Logger,
) *VTEPManager {
	ctx, cancel := context.WithCancel(context.Background())
	return &VTEPManager{
		etcdClient:  etcdClient,
		logger:      logger,
		vxlanMgr:    vxlanMgr,
		remoteVTEPs: make(map[string]*network.VTEP),
		ctx:         ctx,
		cancel:      cancel,
	}
}

// Start begins VTEP registration and discovery.
func (m *VTEPManager) Start(nodeID string, localIP net.IP, port uint16) error {
	m.localVTEP = &network.VTEP{
		NodeID:    nodeID,
		IP:        localIP,
		Port:      port,
		Interface: "br-tun",
		Status:    "active",
		UpdatedAt: time.Now(),
	}

	// Register local VTEP
	if err := m.registerVTEP(); err != nil {
		return fmt.Errorf("failed to register local VTEP: %w", err)
	}

	// Start background workers
	m.wg.Add(2)
	go m.refreshLoop()
	go m.watchVTEPs()

	// Initial discovery
	if err := m.discoverVTEPs(); err != nil {
		m.logger.Warn("initial VTEP discovery failed", zap.Error(err))
	}

	m.logger.Info("VTEP manager started",
		zap.String("node_id", nodeID),
		zap.String("ip", localIP.String()),
		zap.Uint16("port", port),
	)

	return nil
}

// registerVTEP registers the local VTEP in etcd.
func (m *VTEPManager) registerVTEP() error {
	key := vtepKeyPrefix + m.localVTEP.NodeID
	data, err := json.Marshal(m.localVTEP)
	if err != nil {
		return fmt.Errorf("failed to marshal VTEP: %w", err)
	}

	ctx, cancel := context.WithTimeout(m.ctx, 5*time.Second)
	defer cancel()

	if err := m.etcdClient.PutWithTTL(ctx, key, string(data), vtepTTL); err != nil {
		return fmt.Errorf("failed to put VTEP in etcd: %w", err)
	}

	return nil
}

// refreshLoop periodically refreshes the local VTEP registration.
func (m *VTEPManager) refreshLoop() {
	defer m.wg.Done()

	ticker := time.NewTicker(vtepRefreshInterval)
	defer ticker.Stop()

	for {
		select {
		case <-m.ctx.Done():
			return
		case <-ticker.C:
			m.localVTEP.UpdatedAt = time.Now()
			if err := m.registerVTEP(); err != nil {
				m.logger.Warn("failed to refresh VTEP registration", zap.Error(err))
			}
		}
	}
}

// discoverVTEPs discovers all VTEPs in the cluster.
func (m *VTEPManager) discoverVTEPs() error {
	ctx, cancel := context.WithTimeout(m.ctx, 10*time.Second)
	defer cancel()

	kvs, err := m.etcdClient.GetWithPrefixKV(ctx, vtepKeyPrefix)
	if err != nil {
		return fmt.Errorf("failed to list VTEPs: %w", err)
	}

	m.vtepsMu.Lock()
	defer m.vtepsMu.Unlock()

	for _, kv := range kvs {
		var vtep network.VTEP
		if err := json.Unmarshal([]byte(kv.Value), &vtep); err != nil {
			m.logger.Warn("failed to unmarshal VTEP", zap.String("key", kv.Key), zap.Error(err))
			continue
		}

		// Skip local VTEP
		if vtep.NodeID == m.localVTEP.NodeID {
			continue
		}

		m.remoteVTEPs[vtep.NodeID] = &vtep
		m.logger.Debug("discovered remote VTEP",
			zap.String("node_id", vtep.NodeID),
			zap.String("ip", vtep.IP.String()),
		)
	}

	return nil
}

// watchVTEPs watches for VTEP changes in etcd.
func (m *VTEPManager) watchVTEPs() {
	defer m.wg.Done()

	watchCh := m.etcdClient.WatchPrefixEvents(m.ctx, vtepKeyPrefix)

	for {
		select {
		case <-m.ctx.Done():
			return
		case event, ok := <-watchCh:
			if !ok {
				m.logger.Warn("VTEP watch channel closed, reconnecting...")
				time.Sleep(time.Second)
				watchCh = m.etcdClient.WatchPrefixEvents(m.ctx, vtepKeyPrefix)
				continue
			}

			m.handleVTEPEvent(event)
		}
	}
}

// handleVTEPEvent processes a VTEP change event.
func (m *VTEPManager) handleVTEPEvent(event etcd.WatchEvent) {
	nodeID := event.Key[len(vtepKeyPrefix):]

	// Skip local VTEP
	if nodeID == m.localVTEP.NodeID {
		return
	}

	switch event.Type {
	case etcd.EventTypePut:
		var vtep network.VTEP
		if err := json.Unmarshal([]byte(event.Value), &vtep); err != nil {
			m.logger.Warn("failed to unmarshal VTEP event", zap.Error(err))
			return
		}

		m.vtepsMu.Lock()
		oldVTEP, existed := m.remoteVTEPs[vtep.NodeID]
		m.remoteVTEPs[vtep.NodeID] = &vtep
		m.vtepsMu.Unlock()

		if !existed {
			m.logger.Info("new VTEP discovered",
				zap.String("node_id", vtep.NodeID),
				zap.String("ip", vtep.IP.String()),
			)
			// Establish tunnels to new VTEP for all networks
			m.establishTunnelsToVTEP(&vtep)
		} else if !oldVTEP.IP.Equal(vtep.IP) {
			m.logger.Info("VTEP IP changed",
				zap.String("node_id", vtep.NodeID),
				zap.String("old_ip", oldVTEP.IP.String()),
				zap.String("new_ip", vtep.IP.String()),
			)
			// Re-establish tunnels with new IP
			m.reestablishTunnelsToVTEP(oldVTEP, &vtep)
		}

	case etcd.EventTypeDelete:
		m.vtepsMu.Lock()
		vtep, existed := m.remoteVTEPs[nodeID]
		delete(m.remoteVTEPs, nodeID)
		m.vtepsMu.Unlock()

		if existed {
			m.logger.Info("VTEP removed",
				zap.String("node_id", nodeID),
				zap.String("ip", vtep.IP.String()),
			)
			// Clean up tunnels to removed VTEP
			m.cleanupTunnelsToVTEP(vtep)
		}
	}
}

// establishTunnelsToVTEP creates tunnels to a new VTEP for all active networks.
func (m *VTEPManager) establishTunnelsToVTEP(vtep *network.VTEP) {
	// Get all registered networks
	m.vxlanMgr.vniMapMu.RLock()
	networks := make([]*network.Network, 0, len(m.vxlanMgr.vniMap))
	for _, net := range m.vxlanMgr.vniMap {
		networks = append(networks, net)
	}
	m.vxlanMgr.vniMapMu.RUnlock()

	// Create tunnel for each network
	for _, net := range networks {
		if _, err := m.vxlanMgr.CreateTunnel(m.ctx, vtep.NodeID, vtep.IP, net.VNI); err != nil {
			m.logger.Error("failed to create tunnel to new VTEP",
				zap.String("remote_node", vtep.NodeID),
				zap.Uint32("vni", net.VNI),
				zap.Error(err),
			)
		}
	}
}

// reestablishTunnelsToVTEP recreates tunnels when VTEP IP changes.
func (m *VTEPManager) reestablishTunnelsToVTEP(oldVTEP, newVTEP *network.VTEP) {
	// First clean up old tunnels
	m.cleanupTunnelsToVTEP(oldVTEP)

	// Then establish new tunnels
	m.establishTunnelsToVTEP(newVTEP)
}

// cleanupTunnelsToVTEP removes all tunnels to a VTEP.
func (m *VTEPManager) cleanupTunnelsToVTEP(vtep *network.VTEP) {
	tunnels := m.vxlanMgr.ListTunnels()
	for _, tunnel := range tunnels {
		if tunnel.RemoteVTEP == vtep.NodeID {
			if err := m.vxlanMgr.DeleteTunnel(m.ctx, vtep.NodeID, tunnel.VNI); err != nil {
				m.logger.Warn("failed to delete tunnel to removed VTEP",
					zap.String("remote_node", vtep.NodeID),
					zap.Uint32("vni", tunnel.VNI),
					zap.Error(err),
				)
			}
		}
	}
}

// GetRemoteVTEPs returns all known remote VTEPs.
func (m *VTEPManager) GetRemoteVTEPs() []*network.VTEP {
	m.vtepsMu.RLock()
	defer m.vtepsMu.RUnlock()

	vteps := make([]*network.VTEP, 0, len(m.remoteVTEPs))
	for _, vtep := range m.remoteVTEPs {
		vteps = append(vteps, vtep)
	}
	return vteps
}

// GetRemoteVTEP returns a specific remote VTEP by node ID.
func (m *VTEPManager) GetRemoteVTEP(nodeID string) (*network.VTEP, bool) {
	m.vtepsMu.RLock()
	defer m.vtepsMu.RUnlock()

	vtep, exists := m.remoteVTEPs[nodeID]
	return vtep, exists
}

// EstablishMesh creates tunnels to all remote VTEPs for a given VNI.
func (m *VTEPManager) EstablishMesh(vni uint32) error {
	m.vtepsMu.RLock()
	vteps := make([]*network.VTEP, 0, len(m.remoteVTEPs))
	for _, vtep := range m.remoteVTEPs {
		vteps = append(vteps, vtep)
	}
	m.vtepsMu.RUnlock()

	var lastErr error
	for _, vtep := range vteps {
		if _, err := m.vxlanMgr.CreateTunnel(m.ctx, vtep.NodeID, vtep.IP, vni); err != nil {
			m.logger.Error("failed to create tunnel in mesh",
				zap.String("remote_node", vtep.NodeID),
				zap.Uint32("vni", vni),
				zap.Error(err),
			)
			lastErr = err
		}
	}

	return lastErr
}

// TeardownMesh removes tunnels to all remote VTEPs for a given VNI.
func (m *VTEPManager) TeardownMesh(vni uint32) error {
	m.vtepsMu.RLock()
	vteps := make([]*network.VTEP, 0, len(m.remoteVTEPs))
	for _, vtep := range m.remoteVTEPs {
		vteps = append(vteps, vtep)
	}
	m.vtepsMu.RUnlock()

	var lastErr error
	for _, vtep := range vteps {
		if err := m.vxlanMgr.DeleteTunnel(m.ctx, vtep.NodeID, vni); err != nil {
			m.logger.Warn("failed to delete tunnel in mesh teardown",
				zap.String("remote_node", vtep.NodeID),
				zap.Uint32("vni", vni),
				zap.Error(err),
			)
			lastErr = err
		}
	}

	return lastErr
}

// Stop stops the VTEP manager.
func (m *VTEPManager) Stop() error {
	m.logger.Info("stopping VTEP manager")

	m.cancel()
	m.wg.Wait()

	// Deregister local VTEP
	key := vtepKeyPrefix + m.localVTEP.NodeID
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	if err := m.etcdClient.Delete(ctx, key); err != nil {
		m.logger.Warn("failed to deregister VTEP", zap.Error(err))
	}

	return nil
}
