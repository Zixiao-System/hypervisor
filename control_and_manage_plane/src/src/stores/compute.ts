import { defineStore } from 'pinia'
import { ref, computed } from 'vue'
import { computeClient } from '../api/clients'
import { protoInstanceToInstance, instanceTypeToProto } from '../api/converters'
import { InstanceType as ProtoInstanceType, InstanceState as ProtoInstanceState } from '../gen/common_pb'

// Types matching proto definitions
export type InstanceType = 'vm' | 'container' | 'microvm'
export type InstanceState = 'unknown' | 'pending' | 'creating' | 'running' | 'stopped' | 'paused' | 'failed'

export interface Instance {
  id: string
  name: string
  type: InstanceType
  state: InstanceState
  stateReason?: string
  spec: InstanceSpec
  nodeId: string
  ipAddress?: string
  metadata?: { labels?: Record<string, string>; annotations?: Record<string, string> }
  createdAt: Date
  startedAt?: Date
}

export interface InstanceSpec {
  image: string
  cpuCores: number
  memoryBytes: number
  disks: DiskSpec[]
  network?: NetworkSpec
  kernel?: string
  initrd?: string
  kernelArgs?: string
  command?: string[]
  args?: string[]
  env?: Record<string, string>
}

export interface DiskSpec {
  name: string
  sizeBytes: number
  type: string
  boot?: boolean
}

export interface NetworkSpec {
  networkId?: string
  subnetId?: string
  securityGroups?: string[]
  assignPublicIp?: boolean
}

export interface InstanceStats {
  instanceId: string
  cpuUsagePercent: number
  cpuTimeNs: number
  memoryUsedBytes: number
  memoryCacheBytes: number
  diskReadBytes: number
  diskWriteBytes: number
  networkRxBytes: number
  networkTxBytes: number
  collectedAt: Date
}

export const useComputeStore = defineStore('compute', () => {
  const instances = ref<Instance[]>([])
  const loading = ref(false)
  const error = ref<string | null>(null)

  const runningInstances = computed(() => instances.value.filter(i => i.state === 'running'))
  const stoppedInstances = computed(() => instances.value.filter(i => i.state === 'stopped'))
  const vmInstances = computed(() => instances.value.filter(i => i.type === 'vm'))
  const containerInstances = computed(() => instances.value.filter(i => i.type === 'container'))
  const microvmInstances = computed(() => instances.value.filter(i => i.type === 'microvm'))

  async function fetchInstances(filters?: { type?: InstanceType; state?: InstanceState; nodeId?: string }) {
    loading.value = true
    error.value = null
    try {
      const request: Record<string, unknown> = {}
      if (filters?.type) {
        request.type = instanceTypeToProto(filters.type)
      }
      if (filters?.nodeId) {
        request.nodeId = filters.nodeId
      }
      const response = await computeClient.listInstances(request)
      instances.value = response.instances.map(protoInstanceToInstance)
    } catch (e) {
      error.value = e instanceof Error ? e.message : 'Failed to fetch instances'
      // Fallback to mock data in development
      if (import.meta.env.DEV) {
        instances.value = [
          {
            id: 'inst-001',
            name: 'ubuntu-dev-01',
            type: 'vm',
            state: 'running',
            spec: {
              image: 'ubuntu:22.04',
              cpuCores: 4,
              memoryBytes: 8 * 1024 * 1024 * 1024,
              disks: [{ name: 'root', sizeBytes: 50 * 1024 * 1024 * 1024, type: 'ssd', boot: true }],
              network: { assignPublicIp: true }
            },
            nodeId: 'node-001',
            ipAddress: '10.0.0.10',
            metadata: { labels: { owner: 'admin', purpose: 'development' } },
            createdAt: new Date('2024-01-10'),
            startedAt: new Date('2024-01-10')
          },
          {
            id: 'inst-002',
            name: 'nginx-proxy',
            type: 'container',
            state: 'running',
            spec: {
              image: 'nginx:alpine',
              cpuCores: 1,
              memoryBytes: 512 * 1024 * 1024,
              disks: [],
              network: { assignPublicIp: true }
            },
            nodeId: 'node-002',
            ipAddress: '10.0.0.20',
            metadata: { labels: { owner: 'admin', purpose: 'proxy' } },
            createdAt: new Date('2024-01-15'),
            startedAt: new Date('2024-01-15')
          },
          {
            id: 'inst-003',
            name: 'firecracker-test',
            type: 'microvm',
            state: 'stopped',
            spec: {
              image: 'alpine:3.18',
              cpuCores: 2,
              memoryBytes: 1024 * 1024 * 1024,
              disks: [{ name: 'root', sizeBytes: 10 * 1024 * 1024 * 1024, type: 'ssd', boot: true }],
              network: {}
            },
            nodeId: 'node-003',
            metadata: { labels: { owner: 'admin', purpose: 'testing' } },
            createdAt: new Date('2024-01-20')
          }
        ]
      }
    } finally {
      loading.value = false
    }
  }

  async function createInstance(name: string, type: InstanceType, spec: InstanceSpec): Promise<Instance | null> {
    loading.value = true
    error.value = null
    try {
      const response = await computeClient.createInstance({
        name,
        type: instanceTypeToProto(type),
        spec: {
          image: spec.image,
          cpuCores: spec.cpuCores,
          memoryBytes: BigInt(spec.memoryBytes),
          disks: spec.disks.map(d => ({
            name: d.name,
            sizeBytes: BigInt(d.sizeBytes),
            type: d.type,
            boot: d.boot ?? false,
          })),
          network: spec.network ? {
            networkId: spec.network.networkId ?? '',
            subnetId: spec.network.subnetId ?? '',
            securityGroups: spec.network.securityGroups ?? [],
            assignPublicIp: spec.network.assignPublicIp ?? false,
          } : undefined,
        },
      })
      const newInstance = protoInstanceToInstance(response)
      instances.value.push(newInstance)
      return newInstance
    } catch (e) {
      error.value = e instanceof Error ? e.message : 'Failed to create instance'
      // Fallback mock implementation in development
      if (import.meta.env.DEV) {
        const newInstance: Instance = {
          id: `inst-${Date.now()}`,
          name,
          type,
          state: 'creating',
          spec,
          nodeId: 'node-001',
          metadata: {},
          createdAt: new Date()
        }
        instances.value.push(newInstance)
        return newInstance
      }
      return null
    } finally {
      loading.value = false
    }
  }

  async function startInstance(id: string): Promise<boolean> {
    try {
      const response = await computeClient.startInstance({ instanceId: id })
      const index = instances.value.findIndex(i => i.id === id)
      if (index !== -1) {
        instances.value[index] = protoInstanceToInstance(response)
      }
      return true
    } catch (e) {
      error.value = e instanceof Error ? e.message : 'Failed to start instance'
      // Fallback mock implementation in development
      if (import.meta.env.DEV) {
        const instance = instances.value.find(i => i.id === id)
        if (instance) {
          instance.state = 'running'
          instance.startedAt = new Date()
        }
        return true
      }
      return false
    }
  }

  async function stopInstance(id: string, force = false): Promise<boolean> {
    try {
      const response = await computeClient.stopInstance({ instanceId: id, force })
      const index = instances.value.findIndex(i => i.id === id)
      if (index !== -1) {
        instances.value[index] = protoInstanceToInstance(response)
      }
      return true
    } catch (e) {
      error.value = e instanceof Error ? e.message : 'Failed to stop instance'
      // Fallback mock implementation in development
      if (import.meta.env.DEV) {
        const instance = instances.value.find(i => i.id === id)
        if (instance) {
          instance.state = 'stopped'
        }
        return true
      }
      return false
    }
  }

  async function deleteInstance(id: string): Promise<boolean> {
    try {
      await computeClient.deleteInstance({ instanceId: id })
      const index = instances.value.findIndex(i => i.id === id)
      if (index !== -1) {
        instances.value.splice(index, 1)
      }
      return true
    } catch (e) {
      error.value = e instanceof Error ? e.message : 'Failed to delete instance'
      // Fallback mock implementation in development
      if (import.meta.env.DEV) {
        const index = instances.value.findIndex(i => i.id === id)
        if (index !== -1) {
          instances.value.splice(index, 1)
        }
        return true
      }
      return false
    }
  }

  async function restartInstance(id: string, force = false): Promise<boolean> {
    try {
      const response = await computeClient.restartInstance({ instanceId: id, force })
      const index = instances.value.findIndex(i => i.id === id)
      if (index !== -1) {
        instances.value[index] = protoInstanceToInstance(response)
      }
      return true
    } catch (e) {
      error.value = e instanceof Error ? e.message : 'Failed to restart instance'
      // Fallback mock implementation in development
      if (import.meta.env.DEV) {
        const instance = instances.value.find(i => i.id === id)
        if (instance) {
          instance.state = 'running'
          instance.startedAt = new Date()
        }
        return true
      }
      return false
    }
  }

  async function getInstanceStats(id: string): Promise<InstanceStats | null> {
    try {
      const response = await computeClient.getInstanceStats({ instanceId: id })
      return {
        instanceId: response.instanceId,
        cpuUsagePercent: response.cpuUsagePercent,
        cpuTimeNs: Number(response.cpuTimeNs),
        memoryUsedBytes: Number(response.memoryUsedBytes),
        memoryCacheBytes: Number(response.memoryCacheBytes),
        diskReadBytes: Number(response.diskReadBytes),
        diskWriteBytes: Number(response.diskWriteBytes),
        networkRxBytes: Number(response.networkRxBytes),
        networkTxBytes: Number(response.networkTxBytes),
        collectedAt: response.collectedAt?.toDate() ?? new Date()
      }
    } catch (e) {
      error.value = e instanceof Error ? e.message : 'Failed to get instance stats'
      // Fallback mock implementation in development
      if (import.meta.env.DEV) {
        return {
          instanceId: id,
          cpuUsagePercent: Math.random() * 100,
          cpuTimeNs: Date.now() * 1000000,
          memoryUsedBytes: Math.floor(Math.random() * 4 * 1024 * 1024 * 1024),
          memoryCacheBytes: Math.floor(Math.random() * 512 * 1024 * 1024),
          diskReadBytes: Math.floor(Math.random() * 10 * 1024 * 1024 * 1024),
          diskWriteBytes: Math.floor(Math.random() * 5 * 1024 * 1024 * 1024),
          networkRxBytes: Math.floor(Math.random() * 1 * 1024 * 1024 * 1024),
          networkTxBytes: Math.floor(Math.random() * 500 * 1024 * 1024),
          collectedAt: new Date()
        }
      }
      return null
    }
  }

  function getInstanceById(id: string): Instance | undefined {
    return instances.value.find(i => i.id === id)
  }

  return {
    instances,
    loading,
    error,
    runningInstances,
    stoppedInstances,
    vmInstances,
    containerInstances,
    microvmInstances,
    fetchInstances,
    createInstance,
    startInstance,
    stopInstance,
    restartInstance,
    deleteInstance,
    getInstanceById,
    getInstanceStats
  }
})
