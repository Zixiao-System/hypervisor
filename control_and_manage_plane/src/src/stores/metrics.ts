import { defineStore } from 'pinia'
import { ref, computed } from 'vue'

interface MetricPoint {
  timestamp: Date
  value: number
}

export const useMetricsStore = defineStore('metrics', () => {
  const cpuHistory = ref<MetricPoint[]>([])
  const memoryHistory = ref<MetricPoint[]>([])
  const diskHistory = ref<MetricPoint[]>([])
  const networkRxHistory = ref<MetricPoint[]>([])
  const networkTxHistory = ref<MetricPoint[]>([])

  const maxHistoryPoints = 60 // Keep 60 data points (e.g., 1 minute at 1s intervals)

  function addCpuDataPoint(value: number) {
    cpuHistory.value.push({ timestamp: new Date(), value })
    if (cpuHistory.value.length > maxHistoryPoints) {
      cpuHistory.value.shift()
    }
  }

  function addMemoryDataPoint(value: number) {
    memoryHistory.value.push({ timestamp: new Date(), value })
    if (memoryHistory.value.length > maxHistoryPoints) {
      memoryHistory.value.shift()
    }
  }

  function addDiskDataPoint(value: number) {
    diskHistory.value.push({ timestamp: new Date(), value })
    if (diskHistory.value.length > maxHistoryPoints) {
      diskHistory.value.shift()
    }
  }

  function addNetworkDataPoint(rx: number, tx: number) {
    const now = new Date()
    networkRxHistory.value.push({ timestamp: now, value: rx })
    networkTxHistory.value.push({ timestamp: now, value: tx })
    if (networkRxHistory.value.length > maxHistoryPoints) {
      networkRxHistory.value.shift()
    }
    if (networkTxHistory.value.length > maxHistoryPoints) {
      networkTxHistory.value.shift()
    }
  }

  // Computed properties for chart data
  const cpuChartData = computed(() => cpuHistory.value.map(p => p.value))
  const cpuChartLabels = computed(() =>
    cpuHistory.value.map(p => p.timestamp.toLocaleTimeString('en-US', {
      hour12: false,
      hour: '2-digit',
      minute: '2-digit',
      second: '2-digit'
    }))
  )

  const memoryChartData = computed(() => memoryHistory.value.map(p => p.value))
  const memoryChartLabels = computed(() =>
    memoryHistory.value.map(p => p.timestamp.toLocaleTimeString('en-US', {
      hour12: false,
      hour: '2-digit',
      minute: '2-digit',
      second: '2-digit'
    }))
  )

  const diskChartData = computed(() => diskHistory.value.map(p => p.value))
  const diskChartLabels = computed(() =>
    diskHistory.value.map(p => p.timestamp.toLocaleTimeString('en-US', {
      hour12: false,
      hour: '2-digit',
      minute: '2-digit',
      second: '2-digit'
    }))
  )

  // Latest values
  const latestCpu = computed(() =>
    cpuHistory.value.length > 0 ? cpuHistory.value[cpuHistory.value.length - 1].value : 0
  )
  const latestMemory = computed(() =>
    memoryHistory.value.length > 0 ? memoryHistory.value[memoryHistory.value.length - 1].value : 0
  )
  const latestDisk = computed(() =>
    diskHistory.value.length > 0 ? diskHistory.value[diskHistory.value.length - 1].value : 0
  )

  function clearHistory() {
    cpuHistory.value = []
    memoryHistory.value = []
    diskHistory.value = []
    networkRxHistory.value = []
    networkTxHistory.value = []
  }

  return {
    cpuHistory,
    memoryHistory,
    diskHistory,
    networkRxHistory,
    networkTxHistory,
    addCpuDataPoint,
    addMemoryDataPoint,
    addDiskDataPoint,
    addNetworkDataPoint,
    cpuChartData,
    cpuChartLabels,
    memoryChartData,
    memoryChartLabels,
    diskChartData,
    diskChartLabels,
    latestCpu,
    latestMemory,
    latestDisk,
    clearHistory,
  }
})
