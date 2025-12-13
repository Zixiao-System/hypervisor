import { defineStore } from 'pinia'
import { ref, computed } from 'vue'

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
  metadata: Record<string, string>
  createdAt: Date
  startedAt?: Date
}

export interface InstanceSpec {
  image: string
  cpuCores: number
  memoryBytes: number
  disks: DiskSpec[]
  network: NetworkSpec
  kernel?: string
  initrd?: string
  kernelArgs?: string
  command?: string[]
  args?: string[]
  env?: Record<string, string>
}

export interface DiskSpec {
  name: string
  sizeGb: number
  type: 'ssd' | 'hdd'
  sourcePath?: string
  boot?: boolean
}

export interface NetworkSpec {
  networkId?: string
  subnetId?: string
  securityGroups?: string[]
  assignPublicIp?: boolean
  macAddress?: string
  ipAddress?: string
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
      // TODO: Implement gRPC-Web call
      // const response = await computeClient.listInstances(filters)
      // instances.value = response.instances

      // Mock data for development
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
            disks: [{ name: 'root', sizeGb: 50, type: 'ssd', boot: true }],
            network: { assignPublicIp: true }
          },
          nodeId: 'node-001',
          ipAddress: '10.0.0.10',
          metadata: { owner: 'admin', purpose: 'development' },
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
          metadata: { owner: 'admin', purpose: 'proxy' },
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
            disks: [{ name: 'root', sizeGb: 10, type: 'ssd', boot: true }],
            network: {}
          },
          nodeId: 'node-003',
          metadata: { owner: 'admin', purpose: 'testing' },
          createdAt: new Date('2024-01-20')
        }
      ]
    } catch (e) {
      error.value = e instanceof Error ? e.message : 'Failed to fetch instances'
    } finally {
      loading.value = false
    }
  }

  async function createInstance(name: string, type: InstanceType, spec: InstanceSpec): Promise<Instance | null> {
    loading.value = true
    error.value = null
    try {
      // TODO: Implement gRPC-Web call
      // const response = await computeClient.createInstance({ name, type, spec })
      // instances.value.push(response)
      // return response

      // Mock implementation
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
    } catch (e) {
      error.value = e instanceof Error ? e.message : 'Failed to create instance'
      return null
    } finally {
      loading.value = false
    }
  }

  async function startInstance(id: string): Promise<boolean> {
    try {
      // TODO: Implement gRPC-Web call
      const instance = instances.value.find(i => i.id === id)
      if (instance) {
        instance.state = 'running'
        instance.startedAt = new Date()
      }
      return true
    } catch (e) {
      error.value = e instanceof Error ? e.message : 'Failed to start instance'
      return false
    }
  }

  async function stopInstance(id: string, force = false): Promise<boolean> {
    try {
      // TODO: Implement gRPC-Web call
      const instance = instances.value.find(i => i.id === id)
      if (instance) {
        instance.state = 'stopped'
      }
      return true
    } catch (e) {
      error.value = e instanceof Error ? e.message : 'Failed to stop instance'
      return false
    }
  }

  async function deleteInstance(id: string): Promise<boolean> {
    try {
      // TODO: Implement gRPC-Web call
      const index = instances.value.findIndex(i => i.id === id)
      if (index !== -1) {
        instances.value.splice(index, 1)
      }
      return true
    } catch (e) {
      error.value = e instanceof Error ? e.message : 'Failed to delete instance'
      return false
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
    deleteInstance,
    getInstanceById
  }
})
