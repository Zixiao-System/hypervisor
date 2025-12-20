import { defineStore } from 'pinia'
import { ref, computed } from 'vue'
import { clusterClient } from '../api/clients'
import { protoNodeToNode, protoClusterInfoToClusterInfo } from '../api/converters'

// Types matching proto definitions
export interface Node {
  id: string
  hostname: string
  ip: string
  port: number
  role: 'master' | 'worker'
  status: 'unknown' | 'ready' | 'not_ready' | 'maintenance'
  region: string
  zone: string
  capacity: Resources
  allocatable: Resources
  allocated: Resources
  conditions: NodeCondition[]
  supportedInstanceTypes: string[]
  createdAt: Date
  lastSeen: Date
}

export interface Resources {
  cpuCores: number
  memoryBytes: number
  diskBytes: number
  gpuCount: number
}

export interface NodeCondition {
  type: string
  status: boolean
  reason: string
  message: string
  lastTransitionTime: Date
}

export interface ClusterInfo {
  clusterId: string
  clusterName: string
  version: string
  totalNodes: number
  readyNodes: number
  totalCapacity: Resources
  totalAllocated: Resources
}

export const useClusterStore = defineStore('cluster', () => {
  const nodes = ref<Node[]>([])
  const clusterInfo = ref<ClusterInfo | null>(null)
  const loading = ref(false)
  const error = ref<string | null>(null)

  const readyNodes = computed(() => nodes.value.filter(n => n.status === 'ready'))
  const masterNodes = computed(() => nodes.value.filter(n => n.role === 'master'))
  const workerNodes = computed(() => nodes.value.filter(n => n.role === 'worker'))

  const totalCapacity = computed(() => {
    return nodes.value.reduce((acc, node) => ({
      cpuCores: acc.cpuCores + node.capacity.cpuCores,
      memoryBytes: acc.memoryBytes + node.capacity.memoryBytes,
      diskBytes: acc.diskBytes + node.capacity.diskBytes,
      gpuCount: acc.gpuCount + node.capacity.gpuCount
    }), { cpuCores: 0, memoryBytes: 0, diskBytes: 0, gpuCount: 0 })
  })

  const totalAllocated = computed(() => {
    return nodes.value.reduce((acc, node) => ({
      cpuCores: acc.cpuCores + node.allocated.cpuCores,
      memoryBytes: acc.memoryBytes + node.allocated.memoryBytes,
      diskBytes: acc.diskBytes + node.allocated.diskBytes,
      gpuCount: acc.gpuCount + node.allocated.gpuCount
    }), { cpuCores: 0, memoryBytes: 0, diskBytes: 0, gpuCount: 0 })
  })

  async function fetchClusterInfo() {
    loading.value = true
    error.value = null
    try {
      const response = await clusterClient.getClusterInfo({})
      clusterInfo.value = protoClusterInfoToClusterInfo(response)
    } catch (e) {
      error.value = e instanceof Error ? e.message : 'Failed to fetch cluster info'
      // Fallback to mock data in development
      if (import.meta.env.DEV) {
        clusterInfo.value = {
          clusterId: 'zixiao-cluster-001',
          clusterName: 'Zixiao Production',
          version: '0.1.0',
          totalNodes: 3,
          readyNodes: 3,
          totalCapacity: { cpuCores: 48, memoryBytes: 128 * 1024 * 1024 * 1024, diskBytes: 2000 * 1024 * 1024 * 1024, gpuCount: 2 },
          totalAllocated: { cpuCores: 16, memoryBytes: 32 * 1024 * 1024 * 1024, diskBytes: 500 * 1024 * 1024 * 1024, gpuCount: 1 }
        }
      }
    } finally {
      loading.value = false
    }
  }

  async function fetchNodes() {
    loading.value = true
    error.value = null
    try {
      const response = await clusterClient.listNodes({})
      nodes.value = response.nodes.map(protoNodeToNode)
    } catch (e) {
      error.value = e instanceof Error ? e.message : 'Failed to fetch nodes'
      // Fallback to mock data in development
      if (import.meta.env.DEV) {
        nodes.value = [
          {
            id: 'node-001',
            hostname: 'zixiao-master-01',
            ip: '192.168.1.10',
            port: 50051,
            role: 'master',
            status: 'ready',
            region: 'cn-east',
            zone: 'cn-east-1a',
            capacity: { cpuCores: 16, memoryBytes: 64 * 1024 * 1024 * 1024, diskBytes: 1000 * 1024 * 1024 * 1024, gpuCount: 1 },
            allocatable: { cpuCores: 14, memoryBytes: 60 * 1024 * 1024 * 1024, diskBytes: 900 * 1024 * 1024 * 1024, gpuCount: 1 },
            allocated: { cpuCores: 8, memoryBytes: 16 * 1024 * 1024 * 1024, diskBytes: 200 * 1024 * 1024 * 1024, gpuCount: 0 },
            conditions: [{ type: 'Ready', status: true, reason: 'KubeletReady', message: 'kubelet is posting ready status', lastTransitionTime: new Date() }],
            supportedInstanceTypes: ['vm', 'container', 'microvm'],
            createdAt: new Date('2024-01-01'),
            lastSeen: new Date()
          },
          {
            id: 'node-002',
            hostname: 'zixiao-worker-01',
            ip: '192.168.1.11',
            port: 50051,
            role: 'worker',
            status: 'ready',
            region: 'cn-east',
            zone: 'cn-east-1a',
            capacity: { cpuCores: 16, memoryBytes: 32 * 1024 * 1024 * 1024, diskBytes: 500 * 1024 * 1024 * 1024, gpuCount: 1 },
            allocatable: { cpuCores: 15, memoryBytes: 30 * 1024 * 1024 * 1024, diskBytes: 480 * 1024 * 1024 * 1024, gpuCount: 1 },
            allocated: { cpuCores: 4, memoryBytes: 8 * 1024 * 1024 * 1024, diskBytes: 150 * 1024 * 1024 * 1024, gpuCount: 1 },
            conditions: [{ type: 'Ready', status: true, reason: 'KubeletReady', message: 'kubelet is posting ready status', lastTransitionTime: new Date() }],
            supportedInstanceTypes: ['vm', 'container'],
            createdAt: new Date('2024-01-02'),
            lastSeen: new Date()
          },
          {
            id: 'node-003',
            hostname: 'zixiao-worker-02',
            ip: '192.168.1.12',
            port: 50051,
            role: 'worker',
            status: 'ready',
            region: 'cn-east',
            zone: 'cn-east-1b',
            capacity: { cpuCores: 16, memoryBytes: 32 * 1024 * 1024 * 1024, diskBytes: 500 * 1024 * 1024 * 1024, gpuCount: 0 },
            allocatable: { cpuCores: 15, memoryBytes: 30 * 1024 * 1024 * 1024, diskBytes: 480 * 1024 * 1024 * 1024, gpuCount: 0 },
            allocated: { cpuCores: 4, memoryBytes: 8 * 1024 * 1024 * 1024, diskBytes: 150 * 1024 * 1024 * 1024, gpuCount: 0 },
            conditions: [{ type: 'Ready', status: true, reason: 'KubeletReady', message: 'kubelet is posting ready status', lastTransitionTime: new Date() }],
            supportedInstanceTypes: ['vm', 'container', 'microvm'],
            createdAt: new Date('2024-01-03'),
            lastSeen: new Date()
          }
        ]
      }
    } finally {
      loading.value = false
    }
  }

  function getNodeById(id: string): Node | undefined {
    return nodes.value.find(n => n.id === id)
  }

  return {
    nodes,
    clusterInfo,
    loading,
    error,
    readyNodes,
    masterNodes,
    workerNodes,
    totalCapacity,
    totalAllocated,
    fetchClusterInfo,
    fetchNodes,
    getNodeById
  }
})
