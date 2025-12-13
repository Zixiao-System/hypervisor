<script setup lang="ts">
import { onMounted } from 'vue'
import { useRouter } from 'vue-router'
import { useComputeStore, type Instance } from '../stores/compute'

// Import MDUI icons
import '@mdui/icons/play-arrow.js'
import '@mdui/icons/stop.js'
import '@mdui/icons/delete.js'
import '@mdui/icons/add.js'
import '@mdui/icons/computer.js'
import '@mdui/icons/code.js'
import '@mdui/icons/bolt.js'

const router = useRouter()
const computeStore = useComputeStore()

function getTypeIcon(type: string) {
  switch (type) {
    case 'vm': return 'computer'
    case 'container': return 'code'
    case 'microvm': return 'bolt'
    default: return 'computer'
  }
}

function getStatusClass(state: string) {
  switch (state) {
    case 'running': return 'status-running'
    case 'stopped': return 'status-stopped'
    case 'creating':
    case 'pending': return 'status-pending'
    case 'paused': return 'status-paused'
    case 'failed': return 'status-not-ready'
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

function viewInstance(instance: Instance) {
  router.push(`/instances/${instance.id}`)
}

function createInstance() {
  router.push('/instances/create')
}

async function startInstance(instance: Instance, event: Event) {
  event.stopPropagation()
  await computeStore.startInstance(instance.id)
}

async function stopInstance(instance: Instance, event: Event) {
  event.stopPropagation()
  await computeStore.stopInstance(instance.id)
}

async function deleteInstance(instance: Instance, event: Event) {
  event.stopPropagation()
  if (confirm(`Are you sure you want to delete ${instance.name}?`)) {
    await computeStore.deleteInstance(instance.id)
  }
}

onMounted(() => {
  computeStore.fetchInstances()
})
</script>

<template>
  <div class="instances-view">
    <div class="header">
      <h1 class="section-title">Instances</h1>
      <mdui-button variant="filled" @click="createInstance">
        <mdui-icon-add slot="icon"></mdui-icon-add>
        Create Instance
      </mdui-button>
    </div>

    <div class="filter-bar">
      <mdui-chip-group selectable>
        <mdui-chip>All ({{ computeStore.instances.length }})</mdui-chip>
        <mdui-chip>Running ({{ computeStore.runningInstances.length }})</mdui-chip>
        <mdui-chip>Stopped ({{ computeStore.stoppedInstances.length }})</mdui-chip>
      </mdui-chip-group>
    </div>

    <div v-if="computeStore.loading" class="loading">
      <mdui-circular-progress></mdui-circular-progress>
    </div>

    <div v-else class="instances-list">
      <mdui-card
        v-for="instance in computeStore.instances"
        :key="instance.id"
        class="instance-card"
        clickable
        @click="viewInstance(instance)"
      >
        <div class="instance-header">
          <div class="instance-info">
            <mdui-icon :name="getTypeIcon(instance.type)" class="instance-icon"></mdui-icon>
            <div>
              <div class="instance-name">{{ instance.name }}</div>
              <div class="instance-id">{{ instance.id }}</div>
            </div>
          </div>
          <span :class="['status-indicator', getStatusClass(instance.state)]">
            {{ instance.state }}
          </span>
        </div>

        <div class="instance-details">
          <div class="detail-row">
            <span class="detail-label">Type</span>
            <span class="detail-value">{{ instance.type }}</span>
          </div>
          <div class="detail-row">
            <span class="detail-label">Image</span>
            <span class="detail-value">{{ instance.spec.image }}</span>
          </div>
          <div class="detail-row">
            <span class="detail-label">Resources</span>
            <span class="detail-value">{{ instance.spec.cpuCores }} vCPU / {{ formatBytes(instance.spec.memoryBytes) }}</span>
          </div>
          <div class="detail-row" v-if="instance.ipAddress">
            <span class="detail-label">IP Address</span>
            <span class="detail-value">{{ instance.ipAddress }}</span>
          </div>
          <div class="detail-row">
            <span class="detail-label">Node</span>
            <span class="detail-value">{{ instance.nodeId }}</span>
          </div>
        </div>

        <div class="instance-actions">
          <mdui-button-icon
            v-if="instance.state === 'stopped'"
            @click="startInstance(instance, $event)"
            title="Start"
          >
            <mdui-icon-play-arrow></mdui-icon-play-arrow>
          </mdui-button-icon>
          <mdui-button-icon
            v-if="instance.state === 'running'"
            @click="stopInstance(instance, $event)"
            title="Stop"
          >
            <mdui-icon-stop></mdui-icon-stop>
          </mdui-button-icon>
          <mdui-button-icon
            @click="deleteInstance(instance, $event)"
            title="Delete"
          >
            <mdui-icon-delete></mdui-icon-delete>
          </mdui-button-icon>
        </div>
      </mdui-card>
    </div>
  </div>
</template>

<style scoped>
.instances-view {
  max-width: 1400px;
  margin: 0 auto;
}

.header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 16px;
}

.filter-bar {
  margin-bottom: 24px;
}

.loading {
  display: flex;
  justify-content: center;
  padding: 48px;
}

.instances-list {
  display: flex;
  flex-direction: column;
  gap: 16px;
}

.instance-card {
  padding: 20px;
}

.instance-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 16px;
}

.instance-info {
  display: flex;
  align-items: center;
  gap: 12px;
}

.instance-icon {
  font-size: 32px;
  color: var(--zixiao-purple);
}

.instance-name {
  font-size: 18px;
  font-weight: 600;
}

.instance-id {
  font-size: 12px;
  opacity: 0.6;
  font-family: monospace;
}

.instance-details {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
  gap: 12px;
  margin-bottom: 16px;
}

.detail-row {
  display: flex;
  flex-direction: column;
  gap: 4px;
}

.detail-label {
  font-size: 12px;
  text-transform: uppercase;
  opacity: 0.6;
}

.detail-value {
  font-size: 14px;
}

.instance-actions {
  display: flex;
  gap: 8px;
  justify-content: flex-end;
  border-top: 1px solid rgb(var(--mdui-color-outline-light));
  padding-top: 12px;
  margin-top: 12px;
}

.mdui-theme-dark .instance-actions {
  border-color: rgb(var(--mdui-color-outline-dark));
}
</style>
