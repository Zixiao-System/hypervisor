<script setup lang="ts">
import { onMounted, computed } from 'vue'
import { useClusterStore } from '../stores/cluster'
import { useComputeStore } from '../stores/compute'

// Import MDUI icons
import '@mdui/icons/dns.js'
import '@mdui/icons/memory.js'
import '@mdui/icons/storage.js'
import '@mdui/icons/speed.js'

const clusterStore = useClusterStore()
const computeStore = useComputeStore()

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

onMounted(() => {
  clusterStore.fetchClusterInfo()
  clusterStore.fetchNodes()
  computeStore.fetchInstances()
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

    <!-- Resource Usage -->
    <h2 class="section-title">Resource Usage</h2>
    <mdui-card class="resource-card">
      <div class="resource-item">
        <div class="resource-header">
          <span>CPU</span>
          <span>{{ cpuUsagePercent }}%</span>
        </div>
        <div class="resource-bar">
          <div class="resource-bar-fill cpu" :style="{ width: cpuUsagePercent + '%' }"></div>
        </div>
      </div>

      <div class="resource-item">
        <div class="resource-header">
          <span>Memory</span>
          <span>{{ memoryUsagePercent }}%</span>
        </div>
        <div class="resource-bar">
          <div class="resource-bar-fill memory" :style="{ width: memoryUsagePercent + '%' }"></div>
        </div>
      </div>

      <div class="resource-item">
        <div class="resource-header">
          <span>Disk</span>
          <span>{{ diskUsagePercent }}%</span>
        </div>
        <div class="resource-bar">
          <div class="resource-bar-fill disk" :style="{ width: diskUsagePercent + '%' }"></div>
        </div>
      </div>
    </mdui-card>

    <!-- Instance Types -->
    <h2 class="section-title">Instances by Type</h2>
    <div class="stats-grid">
      <mdui-card class="type-card">
        <div class="type-label">VMs</div>
        <div class="type-value">{{ computeStore.vmInstances.length }}</div>
      </mdui-card>

      <mdui-card class="type-card">
        <div class="type-label">Containers</div>
        <div class="type-value">{{ computeStore.containerInstances.length }}</div>
      </mdui-card>

      <mdui-card class="type-card">
        <div class="type-label">MicroVMs</div>
        <div class="type-value">{{ computeStore.microvmInstances.length }}</div>
      </mdui-card>
    </div>

    <!-- Cluster Info Card -->
    <h2 class="section-title">Cluster Information</h2>
    <mdui-card v-if="clusterStore.clusterInfo" class="info-card">
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

.resource-card {
  padding: 24px;
  margin-bottom: 24px;
}

.resource-item {
  margin-bottom: 20px;
}

.resource-item:last-child {
  margin-bottom: 0;
}

.resource-header {
  display: flex;
  justify-content: space-between;
  margin-bottom: 8px;
  font-weight: 500;
}

.type-card {
  padding: 24px;
  text-align: center;
}

.type-label {
  font-size: 14px;
  color: rgb(var(--mdui-color-on-surface-variant-light));
  margin-bottom: 8px;
}

.mdui-theme-dark .type-label {
  color: rgb(var(--mdui-color-on-surface-variant-dark));
}

.type-value {
  font-size: 36px;
  font-weight: 700;
  color: var(--zixiao-purple);
}

.info-card {
  padding: 0;
}
</style>
