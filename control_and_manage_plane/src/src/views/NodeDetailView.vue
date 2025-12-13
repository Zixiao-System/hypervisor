<script setup lang="ts">
import { ref, computed, onMounted } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { useClusterStore } from '../stores/cluster'

// Import MDUI icons
import '@mdui/icons/arrow-back.js'

const route = useRoute()
const router = useRouter()
const clusterStore = useClusterStore()

const nodeId = computed(() => route.params.id as string)
const node = computed(() => clusterStore.getNodeById(nodeId.value))

function formatBytes(bytes: number): string {
  if (bytes === 0) return '0 B'
  const k = 1024
  const sizes = ['B', 'KB', 'MB', 'GB', 'TB']
  const i = Math.floor(Math.log(bytes) / Math.log(k))
  return parseFloat((bytes / Math.pow(k, i)).toFixed(1)) + ' ' + sizes[i]
}

function goBack() {
  router.push('/nodes')
}

onMounted(() => {
  if (!clusterStore.nodes.length) {
    clusterStore.fetchNodes()
  }
})
</script>

<template>
  <div class="node-detail-view">
    <div class="header">
      <mdui-button-icon @click="goBack">
        <mdui-icon-arrow-back></mdui-icon-arrow-back>
      </mdui-button-icon>
      <h1 class="section-title">Node Detail</h1>
    </div>

    <div v-if="!node" class="loading">
      <mdui-circular-progress></mdui-circular-progress>
    </div>

    <div v-else>
      <!-- Basic Info -->
      <mdui-card class="info-card">
        <h3 class="card-title">Basic Information</h3>
        <mdui-list>
          <mdui-list-item>
            <span slot="headline">Hostname</span>
            <span slot="description">{{ node.hostname }}</span>
          </mdui-list-item>
          <mdui-list-item>
            <span slot="headline">ID</span>
            <span slot="description">{{ node.id }}</span>
          </mdui-list-item>
          <mdui-list-item>
            <span slot="headline">Address</span>
            <span slot="description">{{ node.ip }}:{{ node.port }}</span>
          </mdui-list-item>
          <mdui-list-item>
            <span slot="headline">Role</span>
            <span slot="description">{{ node.role }}</span>
          </mdui-list-item>
          <mdui-list-item>
            <span slot="headline">Status</span>
            <span slot="description">{{ node.status }}</span>
          </mdui-list-item>
          <mdui-list-item>
            <span slot="headline">Location</span>
            <span slot="description">{{ node.region }} / {{ node.zone }}</span>
          </mdui-list-item>
        </mdui-list>
      </mdui-card>

      <!-- Resources -->
      <mdui-card class="info-card">
        <h3 class="card-title">Resources</h3>
        <div class="resources-grid">
          <div class="resource-section">
            <h4>CPU</h4>
            <div class="resource-values">
              <div class="resource-item">
                <span class="resource-label">Capacity</span>
                <span class="resource-value">{{ node.capacity.cpuCores }} cores</span>
              </div>
              <div class="resource-item">
                <span class="resource-label">Allocatable</span>
                <span class="resource-value">{{ node.allocatable.cpuCores }} cores</span>
              </div>
              <div class="resource-item">
                <span class="resource-label">Allocated</span>
                <span class="resource-value">{{ node.allocated.cpuCores }} cores</span>
              </div>
            </div>
          </div>

          <div class="resource-section">
            <h4>Memory</h4>
            <div class="resource-values">
              <div class="resource-item">
                <span class="resource-label">Capacity</span>
                <span class="resource-value">{{ formatBytes(node.capacity.memoryBytes) }}</span>
              </div>
              <div class="resource-item">
                <span class="resource-label">Allocatable</span>
                <span class="resource-value">{{ formatBytes(node.allocatable.memoryBytes) }}</span>
              </div>
              <div class="resource-item">
                <span class="resource-label">Allocated</span>
                <span class="resource-value">{{ formatBytes(node.allocated.memoryBytes) }}</span>
              </div>
            </div>
          </div>

          <div class="resource-section">
            <h4>Disk</h4>
            <div class="resource-values">
              <div class="resource-item">
                <span class="resource-label">Capacity</span>
                <span class="resource-value">{{ formatBytes(node.capacity.diskBytes) }}</span>
              </div>
              <div class="resource-item">
                <span class="resource-label">Allocatable</span>
                <span class="resource-value">{{ formatBytes(node.allocatable.diskBytes) }}</span>
              </div>
              <div class="resource-item">
                <span class="resource-label">Allocated</span>
                <span class="resource-value">{{ formatBytes(node.allocated.diskBytes) }}</span>
              </div>
            </div>
          </div>

          <div class="resource-section" v-if="node.capacity.gpuCount > 0">
            <h4>GPU</h4>
            <div class="resource-values">
              <div class="resource-item">
                <span class="resource-label">Capacity</span>
                <span class="resource-value">{{ node.capacity.gpuCount }}</span>
              </div>
              <div class="resource-item">
                <span class="resource-label">Allocated</span>
                <span class="resource-value">{{ node.allocated.gpuCount }}</span>
              </div>
            </div>
          </div>
        </div>
      </mdui-card>

      <!-- Supported Instance Types -->
      <mdui-card class="info-card">
        <h3 class="card-title">Supported Instance Types</h3>
        <div class="chip-group">
          <mdui-chip v-for="type in node.supportedInstanceTypes" :key="type">
            {{ type }}
          </mdui-chip>
        </div>
      </mdui-card>

      <!-- Conditions -->
      <mdui-card class="info-card">
        <h3 class="card-title">Conditions</h3>
        <mdui-list>
          <mdui-list-item v-for="condition in node.conditions" :key="condition.type">
            <span slot="headline">{{ condition.type }}</span>
            <span slot="description">{{ condition.message }}</span>
            <mdui-chip slot="end-icon" :variant="condition.status ? 'filled' : 'outlined'">
              {{ condition.status ? 'True' : 'False' }}
            </mdui-chip>
          </mdui-list-item>
        </mdui-list>
      </mdui-card>
    </div>
  </div>
</template>

<style scoped>
.node-detail-view {
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
  grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
  gap: 24px;
}

.resource-section h4 {
  font-size: 14px;
  font-weight: 600;
  margin-bottom: 12px;
  text-transform: uppercase;
  opacity: 0.8;
}

.resource-values {
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.resource-item {
  display: flex;
  justify-content: space-between;
}

.resource-label {
  font-size: 14px;
  opacity: 0.7;
}

.resource-value {
  font-size: 14px;
  font-weight: 500;
}

.chip-group {
  display: flex;
  gap: 8px;
  flex-wrap: wrap;
}
</style>
