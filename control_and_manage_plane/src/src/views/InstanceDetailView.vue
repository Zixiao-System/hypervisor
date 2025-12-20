<script setup lang="ts">
import { ref, computed, onMounted, onUnmounted, watch } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { useComputeStore, type InstanceStats } from '../stores/compute'

// Import MDUI icons
import '@mdui/icons/arrow-back.js'
import '@mdui/icons/play-arrow.js'
import '@mdui/icons/stop.js'
import '@mdui/icons/restart-alt.js'
import '@mdui/icons/delete.js'
import '@mdui/icons/terminal.js'
import '@mdui/icons/memory.js'
import '@mdui/icons/storage.js'
import '@mdui/icons/wifi.js'

const route = useRoute()
const router = useRouter()
const computeStore = useComputeStore()

const instanceId = computed(() => route.params.id as string)
const instance = computed(() => computeStore.getInstanceById(instanceId.value))

// Stats state
const stats = ref<InstanceStats | null>(null)
const statsLoading = ref(false)
let statsInterval: ReturnType<typeof setInterval> | null = null

function formatBytes(bytes: number): string {
  if (bytes === 0) return '0 B'
  const k = 1024
  const sizes = ['B', 'KB', 'MB', 'GB', 'TB']
  const i = Math.floor(Math.log(bytes) / Math.log(k))
  return parseFloat((bytes / Math.pow(k, i)).toFixed(1)) + ' ' + sizes[i]
}

function formatDate(date: Date): string {
  return new Date(date).toLocaleString()
}

function formatPercent(value: number): string {
  return value.toFixed(1) + '%'
}

function goBack() {
  router.push('/instances')
}

async function startInstance() {
  if (instance.value) {
    await computeStore.startInstance(instance.value.id)
  }
}

async function stopInstance() {
  if (instance.value) {
    await computeStore.stopInstance(instance.value.id)
  }
}

async function restartInstance() {
  if (instance.value) {
    await computeStore.restartInstance(instance.value.id)
  }
}

async function deleteInstance() {
  if (instance.value && confirm(`Are you sure you want to delete ${instance.value.name}?`)) {
    await computeStore.deleteInstance(instance.value.id)
    router.push('/instances')
  }
}

function openConsole() {
  router.push(`/instances/${instanceId.value}/console`)
}

async function fetchStats() {
  if (!instance.value || instance.value.state !== 'running') {
    stats.value = null
    return
  }

  statsLoading.value = true
  try {
    stats.value = await computeStore.getInstanceStats(instanceId.value)
  } finally {
    statsLoading.value = false
  }
}

// Watch for instance state changes to start/stop stats polling
watch(() => instance.value?.state, (newState) => {
  if (newState === 'running') {
    fetchStats()
    if (!statsInterval) {
      statsInterval = setInterval(fetchStats, 5000) // Refresh every 5 seconds
    }
  } else {
    stats.value = null
    if (statsInterval) {
      clearInterval(statsInterval)
      statsInterval = null
    }
  }
}, { immediate: true })

onMounted(() => {
  if (!computeStore.instances.length) {
    computeStore.fetchInstances()
  }
})

onUnmounted(() => {
  if (statsInterval) {
    clearInterval(statsInterval)
  }
})
</script>

<template>
  <div class="instance-detail-view">
    <div class="header">
      <mdui-button-icon @click="goBack">
        <mdui-icon-arrow-back></mdui-icon-arrow-back>
      </mdui-button-icon>
      <h1 class="section-title">Instance Detail</h1>
    </div>

    <div v-if="!instance" class="loading">
      <mdui-circular-progress></mdui-circular-progress>
    </div>

    <div v-else>
      <!-- Actions Bar -->
      <mdui-card class="actions-card">
        <div class="actions-bar">
          <div class="instance-title">
            <h2>{{ instance.name }}</h2>
            <span :class="['status-indicator', `status-${instance.state}`]">
              {{ instance.state }}
            </span>
          </div>
          <div class="actions">
            <mdui-button v-if="instance.state === 'stopped'" variant="tonal" @click="startInstance">
              <mdui-icon-play-arrow slot="icon"></mdui-icon-play-arrow>
              Start
            </mdui-button>
            <mdui-button v-if="instance.state === 'running'" variant="tonal" @click="stopInstance">
              <mdui-icon-stop slot="icon"></mdui-icon-stop>
              Stop
            </mdui-button>
            <mdui-button v-if="instance.state === 'running'" variant="tonal" @click="restartInstance">
              <mdui-icon-restart-alt slot="icon"></mdui-icon-restart-alt>
              Restart
            </mdui-button>
            <mdui-button v-if="instance.state === 'running'" variant="tonal" @click="openConsole">
              <mdui-icon-terminal slot="icon"></mdui-icon-terminal>
              Console
            </mdui-button>
            <mdui-button variant="outlined" @click="deleteInstance">
              <mdui-icon-delete slot="icon"></mdui-icon-delete>
              Delete
            </mdui-button>
          </div>
        </div>
      </mdui-card>

      <!-- Runtime Statistics (only when running) -->
      <mdui-card v-if="instance.state === 'running'" class="stats-card">
        <h3 class="card-title">Runtime Statistics</h3>
        <div v-if="statsLoading && !stats" class="stats-loading">
          <mdui-circular-progress></mdui-circular-progress>
        </div>
        <div v-else-if="stats" class="stats-grid">
          <div class="stat-item">
            <div class="stat-header">
              <mdui-icon-memory></mdui-icon-memory>
              <span>CPU Usage</span>
            </div>
            <div class="stat-value">{{ formatPercent(stats.cpuUsagePercent) }}</div>
            <mdui-linear-progress :value="stats.cpuUsagePercent / 100"></mdui-linear-progress>
          </div>
          <div class="stat-item">
            <div class="stat-header">
              <mdui-icon-memory></mdui-icon-memory>
              <span>Memory</span>
            </div>
            <div class="stat-value">{{ formatBytes(stats.memoryUsedBytes) }}</div>
            <div class="stat-sub">Cache: {{ formatBytes(stats.memoryCacheBytes) }}</div>
          </div>
          <div class="stat-item">
            <div class="stat-header">
              <mdui-icon-storage></mdui-icon-storage>
              <span>Disk I/O</span>
            </div>
            <div class="stat-value-dual">
              <div>
                <span class="label">Read:</span>
                <span>{{ formatBytes(stats.diskReadBytes) }}</span>
              </div>
              <div>
                <span class="label">Write:</span>
                <span>{{ formatBytes(stats.diskWriteBytes) }}</span>
              </div>
            </div>
          </div>
          <div class="stat-item">
            <div class="stat-header">
              <mdui-icon-wifi></mdui-icon-wifi>
              <span>Network</span>
            </div>
            <div class="stat-value-dual">
              <div>
                <span class="label">RX:</span>
                <span>{{ formatBytes(stats.networkRxBytes) }}</span>
              </div>
              <div>
                <span class="label">TX:</span>
                <span>{{ formatBytes(stats.networkTxBytes) }}</span>
              </div>
            </div>
          </div>
        </div>
        <div v-else class="stats-empty">
          No statistics available
        </div>
        <div v-if="stats" class="stats-footer">
          Last updated: {{ formatDate(stats.collectedAt) }}
        </div>
      </mdui-card>

      <!-- Basic Info -->
      <mdui-card class="info-card">
        <h3 class="card-title">Basic Information</h3>
        <mdui-list>
          <mdui-list-item>
            <span slot="headline">ID</span>
            <span slot="description">{{ instance.id }}</span>
          </mdui-list-item>
          <mdui-list-item>
            <span slot="headline">Type</span>
            <span slot="description">{{ instance.type }}</span>
          </mdui-list-item>
          <mdui-list-item>
            <span slot="headline">Image</span>
            <span slot="description">{{ instance.spec.image }}</span>
          </mdui-list-item>
          <mdui-list-item v-if="instance.ipAddress">
            <span slot="headline">IP Address</span>
            <span slot="description">{{ instance.ipAddress }}</span>
          </mdui-list-item>
          <mdui-list-item>
            <span slot="headline">Node</span>
            <span slot="description">{{ instance.nodeId }}</span>
          </mdui-list-item>
          <mdui-list-item>
            <span slot="headline">Created At</span>
            <span slot="description">{{ formatDate(instance.createdAt) }}</span>
          </mdui-list-item>
          <mdui-list-item v-if="instance.startedAt">
            <span slot="headline">Started At</span>
            <span slot="description">{{ formatDate(instance.startedAt) }}</span>
          </mdui-list-item>
        </mdui-list>
      </mdui-card>

      <!-- Resources -->
      <mdui-card class="info-card">
        <h3 class="card-title">Resources</h3>
        <div class="resources-grid">
          <div class="resource-item">
            <span class="resource-label">CPU</span>
            <span class="resource-value">{{ instance.spec.cpuCores }} vCPU</span>
          </div>
          <div class="resource-item">
            <span class="resource-label">Memory</span>
            <span class="resource-value">{{ formatBytes(instance.spec.memoryBytes) }}</span>
          </div>
          <div class="resource-item" v-for="disk in instance.spec.disks" :key="disk.name">
            <span class="resource-label">Disk ({{ disk.name }})</span>
            <span class="resource-value">{{ formatBytes(disk.sizeBytes) }} {{ disk.type.toUpperCase() }}</span>
          </div>
        </div>
      </mdui-card>

      <!-- Network -->
      <mdui-card class="info-card">
        <h3 class="card-title">Network</h3>
        <mdui-list>
          <mdui-list-item v-if="instance.spec.network?.networkId">
            <span slot="headline">Network ID</span>
            <span slot="description">{{ instance.spec.network.networkId }}</span>
          </mdui-list-item>
          <mdui-list-item>
            <span slot="headline">Public IP</span>
            <span slot="description">{{ instance.spec.network?.assignPublicIp ? 'Yes' : 'No' }}</span>
          </mdui-list-item>
        </mdui-list>
      </mdui-card>

      <!-- Metadata -->
      <mdui-card v-if="instance.metadata?.labels && Object.keys(instance.metadata.labels).length > 0" class="info-card">
        <h3 class="card-title">Labels</h3>
        <mdui-list>
          <mdui-list-item v-for="(value, key) in instance.metadata.labels" :key="key">
            <span slot="headline">{{ key }}</span>
            <span slot="description">{{ value }}</span>
          </mdui-list-item>
        </mdui-list>
      </mdui-card>
    </div>
  </div>
</template>

<style scoped>
.instance-detail-view {
  max-width: 1000px;
  margin: 0 auto;
}

.header {
  display: flex;
  align-items: center;
  gap: 16px;
  margin-bottom: 24px;
}

.loading {
  display: flex;
  justify-content: center;
  padding: 48px;
}

.actions-card {
  padding: 20px;
  margin-bottom: 24px;
}

.actions-bar {
  display: flex;
  justify-content: space-between;
  align-items: center;
  flex-wrap: wrap;
  gap: 16px;
}

.instance-title {
  display: flex;
  align-items: center;
  gap: 12px;
}

.instance-title h2 {
  font-size: 24px;
  font-weight: 600;
  margin: 0;
}

.actions {
  display: flex;
  gap: 8px;
  flex-wrap: wrap;
}

.stats-card {
  margin-bottom: 24px;
  padding: 24px;
}

.stats-loading,
.stats-empty {
  display: flex;
  justify-content: center;
  align-items: center;
  min-height: 100px;
  opacity: 0.7;
}

.stats-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
  gap: 24px;
}

.stat-item {
  padding: 16px;
  background: rgba(var(--mdui-color-surface-container-rgb), 0.5);
  border-radius: 12px;
}

.stat-header {
  display: flex;
  align-items: center;
  gap: 8px;
  font-size: 14px;
  opacity: 0.8;
  margin-bottom: 8px;
}

.stat-value {
  font-size: 24px;
  font-weight: 600;
  color: var(--zixiao-purple);
  margin-bottom: 8px;
}

.stat-sub {
  font-size: 12px;
  opacity: 0.7;
}

.stat-value-dual {
  display: flex;
  flex-direction: column;
  gap: 4px;
}

.stat-value-dual div {
  display: flex;
  gap: 8px;
}

.stat-value-dual .label {
  opacity: 0.7;
  min-width: 45px;
}

.stats-footer {
  margin-top: 16px;
  font-size: 12px;
  opacity: 0.6;
  text-align: right;
}

.info-card {
  margin-bottom: 24px;
  padding: 24px;
}

.card-title {
  font-size: 18px;
  font-weight: 600;
  margin-bottom: 16px;
  color: var(--zixiao-purple);
}

.resources-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(150px, 1fr));
  gap: 24px;
}

.resource-item {
  display: flex;
  flex-direction: column;
  gap: 4px;
}

.resource-label {
  font-size: 12px;
  text-transform: uppercase;
  opacity: 0.7;
}

.resource-value {
  font-size: 18px;
  font-weight: 600;
  color: var(--zixiao-purple);
}
</style>
