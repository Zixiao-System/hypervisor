import type { Timestamp } from '@bufbuild/protobuf'
import {
  Node as ProtoNode,
  ClusterInfo as ProtoClusterInfo,
} from '../gen/cluster_pb'
import {
  Instance as ProtoInstance,
  InstanceStats as ProtoInstanceStats,
} from '../gen/compute_pb'
import {
  NodeRole,
  NodeStatus,
  InstanceType,
  InstanceState,
  Resources as ProtoResources,
  NodeCondition as ProtoNodeCondition,
} from '../gen/common_pb'
import type { Node, ClusterInfo, Resources, NodeCondition } from '../stores/cluster'
import type { Instance, InstanceStats, InstanceSpec, DiskSpec, NetworkSpec } from '../stores/compute'

// Helper to convert Timestamp to Date
function timestampToDate(ts: Timestamp | undefined): Date {
  if (!ts) return new Date()
  return ts.toDate()
}

// Helper to convert proto Resources to store Resources
function convertResources(proto: ProtoResources | undefined): Resources {
  return {
    cpuCores: proto?.cpuCores ?? 0,
    memoryBytes: Number(proto?.memoryBytes ?? 0n),
    diskBytes: Number(proto?.diskBytes ?? 0n),
    gpuCount: proto?.gpuCount ?? 0,
  }
}

// Convert proto NodeRole enum to store type
function convertNodeRole(role: NodeRole): 'master' | 'worker' {
  switch (role) {
    case NodeRole.MASTER:
      return 'master'
    case NodeRole.WORKER:
    default:
      return 'worker'
  }
}

// Convert store role to proto NodeRole
export function roleToProto(role: 'master' | 'worker'): NodeRole {
  return role === 'master' ? NodeRole.MASTER : NodeRole.WORKER
}

// Convert proto NodeStatus enum to store type
function convertNodeStatus(status: NodeStatus): 'unknown' | 'ready' | 'not_ready' | 'maintenance' {
  switch (status) {
    case NodeStatus.READY:
      return 'ready'
    case NodeStatus.NOT_READY:
      return 'not_ready'
    case NodeStatus.MAINTENANCE:
    case NodeStatus.DRAINING:
      return 'maintenance'
    default:
      return 'unknown'
  }
}

// Convert proto NodeCondition to store NodeCondition
function convertNodeCondition(proto: ProtoNodeCondition): NodeCondition {
  return {
    type: proto.type,
    status: proto.status === 'True',
    reason: proto.reason,
    message: proto.message,
    lastTransitionTime: timestampToDate(proto.lastTransitionTime),
  }
}

// Convert proto Node to store Node
export function protoNodeToNode(proto: ProtoNode): Node {
  return {
    id: proto.id,
    hostname: proto.hostname,
    ip: proto.ip,
    port: proto.port,
    role: convertNodeRole(proto.role),
    status: convertNodeStatus(proto.status),
    region: proto.region,
    zone: proto.zone,
    capacity: convertResources(proto.capacity),
    allocatable: convertResources(proto.allocatable),
    allocated: convertResources(proto.allocated),
    conditions: proto.conditions.map(convertNodeCondition),
    supportedInstanceTypes: proto.supportedInstanceTypes,
    createdAt: timestampToDate(proto.createdAt),
    lastSeen: timestampToDate(proto.lastSeen),
  }
}

// Convert proto ClusterInfo to store ClusterInfo
export function protoClusterInfoToClusterInfo(proto: ProtoClusterInfo): ClusterInfo {
  return {
    clusterId: proto.clusterId,
    clusterName: proto.clusterName,
    version: proto.version,
    totalNodes: proto.totalNodes,
    readyNodes: proto.readyNodes,
    totalCapacity: convertResources(proto.totalCapacity),
    totalAllocated: convertResources(proto.totalAllocated),
  }
}

// Convert proto InstanceType enum to store type
function convertInstanceType(type: InstanceType): 'vm' | 'container' | 'microvm' {
  switch (type) {
    case InstanceType.VM:
      return 'vm'
    case InstanceType.CONTAINER:
      return 'container'
    case InstanceType.MICROVM:
      return 'microvm'
    default:
      return 'vm'
  }
}

// Convert store instance type to proto InstanceType
export function instanceTypeToProto(type: 'vm' | 'container' | 'microvm'): InstanceType {
  switch (type) {
    case 'vm':
      return InstanceType.VM
    case 'container':
      return InstanceType.CONTAINER
    case 'microvm':
      return InstanceType.MICROVM
  }
}

// Convert proto InstanceState enum to store type
function convertInstanceState(
  state: InstanceState
): 'unknown' | 'pending' | 'creating' | 'running' | 'stopped' | 'paused' | 'failed' {
  switch (state) {
    case InstanceState.PENDING:
      return 'pending'
    case InstanceState.CREATING:
      return 'creating'
    case InstanceState.RUNNING:
      return 'running'
    case InstanceState.STOPPED:
      return 'stopped'
    case InstanceState.FAILED:
      return 'failed'
    case InstanceState.DELETING:
      return 'pending'
    default:
      return 'unknown'
  }
}

// Convert proto Instance to store Instance
export function protoInstanceToInstance(proto: ProtoInstance): Instance {
  const spec = proto.spec
  return {
    id: proto.id,
    name: proto.name,
    type: convertInstanceType(proto.type),
    state: convertInstanceState(proto.state),
    spec: {
      image: spec?.image ?? '',
      cpuCores: spec?.cpuCores ?? 1,
      memoryBytes: Number(spec?.memoryBytes ?? 0n),
      disks: spec?.disks.map(d => ({
        name: d.name,
        sizeBytes: Number(d.sizeBytes ?? 0n),
        type: d.type,
        boot: d.boot,
      })) ?? [],
      network: spec?.network ? {
        networkId: spec.network.networkId,
        subnetId: spec.network.subnetId,
        securityGroups: spec.network.securityGroups,
        assignPublicIp: spec.network.assignPublicIp,
      } : undefined,
    },
    nodeId: proto.nodeId,
    ipAddress: proto.ipAddress,
    metadata: proto.metadata ? {
      labels: Object.fromEntries(proto.metadata.labels),
      annotations: Object.fromEntries(proto.metadata.annotations),
    } : undefined,
    stateReason: proto.stateReason,
    createdAt: timestampToDate(proto.createdAt),
    startedAt: proto.startedAt ? timestampToDate(proto.startedAt) : undefined,
  }
}

// Convert proto InstanceStats to store InstanceStats
export function protoInstanceStatsToInstanceStats(proto: ProtoInstanceStats): InstanceStats {
  return {
    instanceId: proto.instanceId,
    cpuUsagePercent: proto.cpuUsagePercent,
    cpuTimeNs: Number(proto.cpuTimeNs ?? 0n),
    memoryUsedBytes: Number(proto.memoryUsedBytes ?? 0n),
    memoryCacheBytes: Number(proto.memoryCacheBytes ?? 0n),
    diskReadBytes: Number(proto.diskReadBytes ?? 0n),
    diskWriteBytes: Number(proto.diskWriteBytes ?? 0n),
    networkRxBytes: Number(proto.networkRxBytes ?? 0n),
    networkTxBytes: Number(proto.networkTxBytes ?? 0n),
    collectedAt: timestampToDate(proto.collectedAt),
  }
}
