<script setup lang="ts">
import { ref, computed, onMounted } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { useComputeStore } from '../stores/compute'

// Import MDUI icons
import '@mdui/icons/arrow-back.js'
import '@mdui/icons/play-arrow.js'
import '@mdui/icons/stop.js'
import '@mdui/icons/restart-alt.js'
import '@mdui/icons/delete.js'
import '@mdui/icons/terminal.js'

const route = useRoute()
const router = useRouter()
const computeStore = useComputeStore()

const instanceId = computed(() => route.params.id as string)
const instance = computed(() => computeStore.getInstanceById(instanceId.value))

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

async function deleteInstance() {
  if (instance.value && confirm(`Are you sure you want to delete ${instance.value.name}?`)) {
    await computeStore.deleteInstance(instance.value.id)
    router.push('/instances')
  }
}

function openConsole() {
  // TODO: Implement console view
  alert('Console feature coming soon!')
}

onMounted(() => {
  if (!computeStore.instances.length) {
    computeStore.fetchInstances()
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
            <span class="resource-value">{{ disk.sizeGb }} GB {{ disk.type.toUpperCase() }}</span>
          </div>
        </div>
      </mdui-card>

      <!-- Network -->
      <mdui-card class="info-card">
        <h3 class="card-title">Network</h3>
        <mdui-list>
          <mdui-list-item v-if="instance.spec.network.networkId">
            <span slot="headline">Network ID</span>
            <span slot="description">{{ instance.spec.network.networkId }}</span>
          </mdui-list-item>
          <mdui-list-item>
            <span slot="headline">Public IP</span>
            <span slot="description">{{ instance.spec.network.assignPublicIp ? 'Yes' : 'No' }}</span>
          </mdui-list-item>
        </mdui-list>
      </mdui-card>

      <!-- Metadata -->
      <mdui-card v-if="Object.keys(instance.metadata).length > 0" class="info-card">
        <h3 class="card-title">Metadata</h3>
        <mdui-list>
          <mdui-list-item v-for="(value, key) in instance.metadata" :key="key">
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
