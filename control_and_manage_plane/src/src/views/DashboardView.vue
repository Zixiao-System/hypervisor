<script setup lang="ts">
import { onMounted, onUnmounted, computed, ref } from 'vue'
import { useClusterStore } from '../stores/cluster'
import { useComputeStore } from '../stores/compute'
import { useMetricsStore } from '../stores/metrics'
import ResourceGaugeChart from '../components/charts/ResourceGaugeChart.vue'
import ResourceTrendChart from '../components/charts/ResourceTrendChart.vue'
import InstanceDistributionChart from '../components/charts/InstanceDistributionChart.vue'

// Import MDUI icons
import '@mdui/icons/dns.js'
import '@mdui/icons/memory.js'
import '@mdui/icons/storage.js'
import '@mdui/icons/speed.js'

const clusterStore = useClusterStore()
const computeStore = useComputeStore()
const metricsStore = useMetricsStore()

let metricsInterval: number | null = null

const cpuUsagePercent = computed(() => {
  if (!clusterStore.totalCapacity.cpuCores) return 0
  return Math.round((clusterStore.totalAllocated.cpuCores / clusterStore.totalCapacity.cpuCores) * 100)
})

const memoryUsagePercent = computed(() => {
  if (!clusterStore.totalCapacity.memoryBytes) return 0
  return Math.round((clusterStore.totalAllocated.memoryBytes / clusterStore.totalCapacity.memoryBytes) * 100)
})

const diskUsagePercent = computed(() => {
  if (!clusterStore.totalCapacity.diskBytes) return 0
  return Math.round((clusterStore.totalAllocated.diskBytes / clusterStore.totalCapacity.diskBytes) * 100)
})

function formatBytes(bytes: number): string {
  if (bytes === 0) return '0 B'
  const k = 1024
  const sizes = ['B', 'KB', 'MB', 'GB', 'TB']
  const i = Math.floor(Math.log(bytes) / Math.log(k))
  return parseFloat((bytes / Math.pow(k, i)).toFixed(1)) + ' ' + sizes[i]
}

function collectMetrics() {
  metricsStore.addCpuDataPoint(cpuUsagePercent.value)
  metricsStore.addMemoryDataPoint(memoryUsagePercent.value)
  metricsStore.addDiskDataPoint(diskUsagePercent.value)
}

onMounted(() => {
  clusterStore.fetchClusterInfo()
  clusterStore.fetchNodes()
  computeStore.fetchInstances()

  // Collect initial metrics
  collectMetrics()

  // Poll for metrics every 5 seconds
  metricsInterval = window.setInterval(() => {
    collectMetrics()
  }, 5000)
})

onUnmounted(() => {
  if (metricsInterval) {
    clearInterval(metricsInterval)
  }
})
</script>

<template>
  <div class="dashboard">
    <h1 class="section-title">Dashboard</h1>

    <!-- Cluster Info -->
    <div class="stats-grid">
      <mdui-card class="stat-card">
        <div class="stat-content">
          <mdui-icon-dns class="stat-icon"></mdui-icon-dns>
          <div>
            <div class="stat-value">{{ clusterStore.readyNodes.length }}/{{ clusterStore.nodes.length }}</div>
            <div class="stat-label">Ready Nodes</div>
          </div>
        </div>
      </mdui-card>

      <mdui-card class="stat-card">
        <div class="stat-content">
          <mdui-icon-memory class="stat-icon"></mdui-icon-memory>
          <div>
            <div class="stat-value">{{ computeStore.runningInstances.length }}</div>
            <div class="stat-label">Running Instances</div>
          </div>
        </div>
      </mdui-card>

      <mdui-card class="stat-card">
        <div class="stat-content">
          <mdui-icon-speed class="stat-icon"></mdui-icon-speed>
          <div>
            <div class="stat-value">{{ clusterStore.totalAllocated.cpuCores }}/{{ clusterStore.totalCapacity.cpuCores }}</div>
            <div class="stat-label">CPU Cores Used</div>
          </div>
        </div>
      </mdui-card>

      <mdui-card class="stat-card">
        <div class="stat-content">
          <mdui-icon-storage class="stat-icon"></mdui-icon-storage>
          <div>
            <div class="stat-value">{{ formatBytes(clusterStore.totalAllocated.memoryBytes) }}</div>
            <div class="stat-label">Memory Used</div>
          </div>
        </div>
      </mdui-card>
    </div>

    <!-- Resource Gauges -->
    <h2 class="section-title">Resource Usage</h2>
    <mdui-card class="gauge-card">
      <div class="gauge-grid">
        <ResourceGaugeChart
          label="CPU"
          :used="clusterStore.totalAllocated.cpuCores"
          :total="clusterStore.totalCapacity.cpuCores"
          color="#8B5CF6"
        />
        <ResourceGaugeChart
          label="Memory"
          :used="clusterStore.totalAllocated.memoryBytes"
          :total="clusterStore.totalCapacity.memoryBytes"
          color="#10B981"
          unit="bytes"
        />
        <ResourceGaugeChart
          label="Disk"
          :used="clusterStore.totalAllocated.diskBytes"
          :total="clusterStore.totalCapacity.diskBytes"
          color="#F59E0B"
          unit="bytes"
        />
      </div>
    </mdui-card>

    <!-- Resource Trends -->
    <h2 class="section-title">Resource Trends</h2>
    <div class="trend-grid">
      <mdui-card class="trend-card">
        <ResourceTrendChart
          title="CPU Usage"
          :dataPoints="metricsStore.cpuChartData"
          :labels="metricsStore.cpuChartLabels"
          color="#8B5CF6"
        />
      </mdui-card>
      <mdui-card class="trend-card">
        <ResourceTrendChart
          title="Memory Usage"
          :dataPoints="metricsStore.memoryChartData"
          :labels="metricsStore.memoryChartLabels"
          color="#10B981"
        />
      </mdui-card>
    </div>

    <!-- Instance Distribution -->
    <h2 class="section-title">Instances</h2>
    <div class="distribution-grid">
      <mdui-card class="distribution-card">
        <InstanceDistributionChart
          :vmCount="computeStore.vmInstances.length"
          :containerCount="computeStore.containerInstances.length"
          :microvmCount="computeStore.microvmInstances.length"
        />
      </mdui-card>

      <!-- Cluster Info Card -->
      <mdui-card v-if="clusterStore.clusterInfo" class="info-card">
        <div class="info-header">Cluster Information</div>
        <mdui-list>
          <mdui-list-item>
            <span slot="headline">Cluster ID</span>
            <span slot="description">{{ clusterStore.clusterInfo.clusterId }}</span>
          </mdui-list-item>
          <mdui-list-item>
            <span slot="headline">Cluster Name</span>
            <span slot="description">{{ clusterStore.clusterInfo.clusterName }}</span>
          </mdui-list-item>
          <mdui-list-item>
            <span slot="headline">Version</span>
            <span slot="description">{{ clusterStore.clusterInfo.version }}</span>
          </mdui-list-item>
        </mdui-list>
      </mdui-card>
    </div>
  </div>
</template>

<style scoped>
.dashboard {
  max-width: 1400px;
  margin: 0 auto;
}

.stat-card {
  padding: 20px;
}

.stat-content {
  display: flex;
  align-items: center;
  gap: 16px;
}

.stat-icon {
  font-size: 48px;
  color: var(--zixiao-purple);
}

.gauge-card {
  padding: 32px;
  margin-bottom: 24px;
}

.gauge-grid {
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 48px;
  justify-items: center;
}

@media (max-width: 768px) {
  .gauge-grid {
    grid-template-columns: 1fr;
    gap: 32px;
  }
}

.trend-grid {
  display: grid;
  grid-template-columns: repeat(2, 1fr);
  gap: 24px;
  margin-bottom: 24px;
}

@media (max-width: 768px) {
  .trend-grid {
    grid-template-columns: 1fr;
  }
}

.trend-card {
  padding: 24px;
}

.distribution-grid {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 24px;
  margin-bottom: 24px;
}

@media (max-width: 768px) {
  .distribution-grid {
    grid-template-columns: 1fr;
  }
}

.distribution-card {
  padding: 24px;
}

.info-card {
  padding: 0;
}

.info-header {
  padding: 16px 24px;
  font-weight: 500;
  font-size: 14px;
  border-bottom: 1px solid var(--mdui-color-outline-variant);
}
</style>
