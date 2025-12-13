<script setup lang="ts">
import { onMounted } from 'vue'
import { useRouter } from 'vue-router'
import { useClusterStore, type Node } from '../stores/cluster'

// Import MDUI icons
import '@mdui/icons/check-circle.js'
import '@mdui/icons/error.js'
import '@mdui/icons/schedule.js'
import '@mdui/icons/build.js'

const router = useRouter()
const clusterStore = useClusterStore()

function getStatusIcon(status: string) {
  switch (status) {
    case 'ready': return 'check-circle'
    case 'not_ready': return 'error'
    case 'maintenance': return 'build'
    default: return 'schedule'
  }
}

function getStatusClass(status: string) {
  switch (status) {
    case 'ready': return 'status-ready'
    case 'not_ready': return 'status-not-ready'
    case 'maintenance': return 'status-maintenance'
    default: return 'status-pending'
  }
}

function formatBytes(bytes: number): string {
  if (bytes === 0) return '0 B'
  const k = 1024
  const sizes = ['B', 'KB', 'MB', 'GB', 'TB']
  const i = Math.floor(Math.log(bytes) / Math.log(k))
  return parseFloat((bytes / Math.pow(k, i)).toFixed(1)) + ' ' + sizes[i]
}

function viewNode(node: Node) {
  router.push(`/nodes/${node.id}`)
}

onMounted(() => {
  clusterStore.fetchNodes()
})
</script>

<template>
  <div class="nodes-view">
    <div class="header">
      <h1 class="section-title">Nodes</h1>
      <div class="header-stats">
        <span class="stat">{{ clusterStore.readyNodes.length }} Ready</span>
        <span class="stat">{{ clusterStore.masterNodes.length }} Master</span>
        <span class="stat">{{ clusterStore.workerNodes.length }} Worker</span>
      </div>
    </div>

    <div v-if="clusterStore.loading" class="loading">
      <mdui-circular-progress></mdui-circular-progress>
    </div>

    <div v-else class="nodes-grid">
      <mdui-card
        v-for="node in clusterStore.nodes"
        :key="node.id"
        class="node-card"
        clickable
        @click="viewNode(node)"
      >
        <div class="node-header">
          <div class="node-info">
            <div class="node-name">{{ node.hostname }}</div>
            <div class="node-ip">{{ node.ip }}:{{ node.port }}</div>
          </div>
          <span :class="['status-indicator', getStatusClass(node.status)]">
            <mdui-icon :name="getStatusIcon(node.status)"></mdui-icon>
            {{ node.status }}
          </span>
        </div>

        <div class="node-meta">
          <mdui-chip>{{ node.role }}</mdui-chip>
          <mdui-chip>{{ node.region }}/{{ node.zone }}</mdui-chip>
        </div>

        <div class="node-resources">
          <div class="resource-row">
            <span class="resource-label">CPU</span>
            <div class="resource-bar">
              <div
                class="resource-bar-fill cpu"
                :style="{ width: (node.allocated.cpuCores / node.capacity.cpuCores * 100) + '%' }"
              ></div>
            </div>
            <span class="resource-text">{{ node.allocated.cpuCores }}/{{ node.capacity.cpuCores }} cores</span>
          </div>

          <div class="resource-row">
            <span class="resource-label">Memory</span>
            <div class="resource-bar">
              <div
                class="resource-bar-fill memory"
                :style="{ width: (node.allocated.memoryBytes / node.capacity.memoryBytes * 100) + '%' }"
              ></div>
            </div>
            <span class="resource-text">{{ formatBytes(node.allocated.memoryBytes) }}/{{ formatBytes(node.capacity.memoryBytes) }}</span>
          </div>

          <div class="resource-row">
            <span class="resource-label">Disk</span>
            <div class="resource-bar">
              <div
                class="resource-bar-fill disk"
                :style="{ width: (node.allocated.diskBytes / node.capacity.diskBytes * 100) + '%' }"
              ></div>
            </div>
            <span class="resource-text">{{ formatBytes(node.allocated.diskBytes) }}/{{ formatBytes(node.capacity.diskBytes) }}</span>
          </div>
        </div>

        <div class="node-types">
          <span class="types-label">Supported:</span>
          <mdui-chip v-for="type in node.supportedInstanceTypes" :key="type" variant="outlined">
            {{ type }}
          </mdui-chip>
        </div>
      </mdui-card>
    </div>
  </div>
</template>

<style scoped>
.nodes-view {
  max-width: 1400px;
  margin: 0 auto;
}

.header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 24px;
}

.header-stats {
  display: flex;
  gap: 16px;
}

.stat {
  padding: 6px 12px;
  background: rgba(139, 92, 246, 0.1);
  border-radius: 16px;
  font-size: 14px;
  color: var(--zixiao-purple);
}

.loading {
  display: flex;
  justify-content: center;
  padding: 48px;
}

.node-card {
  padding: 20px;
}

.node-header {
  display: flex;
  justify-content: space-between;
  align-items: flex-start;
  margin-bottom: 16px;
}

.node-name {
  font-size: 18px;
  font-weight: 600;
  color: var(--zixiao-purple);
}

.node-ip {
  font-size: 14px;
  opacity: 0.7;
  margin-top: 4px;
}

.node-meta {
  display: flex;
  gap: 8px;
  margin-bottom: 16px;
}

.node-resources {
  margin-bottom: 16px;
}

.resource-row {
  display: grid;
  grid-template-columns: 60px 1fr 120px;
  align-items: center;
  gap: 12px;
  margin-bottom: 8px;
}

.resource-label {
  font-size: 12px;
  font-weight: 500;
  text-transform: uppercase;
}

.resource-text {
  font-size: 12px;
  text-align: right;
  opacity: 0.8;
}

.node-types {
  display: flex;
  align-items: center;
  gap: 8px;
  flex-wrap: wrap;
}

.types-label {
  font-size: 12px;
  opacity: 0.7;
}
</style>
