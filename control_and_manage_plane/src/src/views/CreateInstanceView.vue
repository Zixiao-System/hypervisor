<script setup lang="ts">
import { ref, computed, onMounted } from 'vue'
import { useRouter } from 'vue-router'
import { useComputeStore, type InstanceType, type InstanceSpec, type DiskSpec, type NetworkSpec } from '../stores/compute'

// Import MDUI icons
import '@mdui/icons/arrow-back.js'
import '@mdui/icons/computer.js'
import '@mdui/icons/code.js'
import '@mdui/icons/bolt.js'

const router = useRouter()
const computeStore = useComputeStore()

const instanceName = ref('')
const instanceType = ref<InstanceType>('vm')
const image = ref('')
const cpuCores = ref(2)
const memoryGb = ref(4)
const diskSizeGb = ref(50)
const assignPublicIp = ref(true)

const creating = ref(false)
const error = ref<string | null>(null)

const instanceTypes = [
  { value: 'vm', label: 'Virtual Machine', icon: 'computer', description: 'Full virtualization with KVM/QEMU' },
  { value: 'container', label: 'Container', icon: 'code', description: 'Lightweight isolation with containerd' },
  { value: 'microvm', label: 'MicroVM', icon: 'bolt', description: 'Fast boot with Firecracker' }
]

const images = computed(() => {
  switch (instanceType.value) {
    case 'vm':
      return ['ubuntu:22.04', 'debian:12', 'centos:9', 'rocky:9', 'windows:11']
    case 'container':
      return ['nginx:alpine', 'redis:7', 'postgres:16', 'node:20', 'python:3.12']
    case 'microvm':
      return ['alpine:3.19', 'ubuntu:22.04-minimal', 'fedora:39-minimal']
    default:
      return []
  }
})

function goBack() {
  router.push('/instances')
}

async function createInstance() {
  if (!instanceName.value || !image.value) {
    error.value = 'Please fill in all required fields'
    return
  }

  creating.value = true
  error.value = null

  const spec: InstanceSpec = {
    image: image.value,
    cpuCores: cpuCores.value,
    memoryBytes: memoryGb.value * 1024 * 1024 * 1024,
    disks: instanceType.value !== 'container' ? [
      {
        name: 'root',
        sizeGb: diskSizeGb.value,
        type: 'ssd',
        boot: true
      }
    ] : [],
    network: {
      assignPublicIp: assignPublicIp.value
    }
  }

  const result = await computeStore.createInstance(instanceName.value, instanceType.value, spec)

  creating.value = false

  if (result) {
    router.push(`/instances/${result.id}`)
  } else {
    error.value = computeStore.error || 'Failed to create instance'
  }
}

onMounted(() => {
  // Set default image
  if (images.value.length > 0) {
    image.value = images.value[0]
  }
})
</script>

<template>
  <div class="create-instance-view">
    <div class="header">
      <mdui-button-icon @click="goBack">
        <mdui-icon-arrow-back></mdui-icon-arrow-back>
      </mdui-button-icon>
      <h1 class="section-title">Create Instance</h1>
    </div>

    <mdui-card class="form-card">
      <!-- Instance Type Selection -->
      <div class="form-section">
        <h3 class="form-section-title">Instance Type</h3>
        <div class="type-grid">
          <mdui-card
            v-for="type in instanceTypes"
            :key="type.value"
            :class="['type-option', { selected: instanceType === type.value }]"
            clickable
            @click="instanceType = type.value as InstanceType; image = images[0]"
          >
            <mdui-icon :name="type.icon" class="type-icon"></mdui-icon>
            <div class="type-label">{{ type.label }}</div>
            <div class="type-description">{{ type.description }}</div>
          </mdui-card>
        </div>
      </div>

      <!-- Basic Information -->
      <div class="form-section">
        <h3 class="form-section-title">Basic Information</h3>
        <div class="form-grid">
          <mdui-text-field
            v-model="instanceName"
            label="Instance Name"
            placeholder="my-instance"
            required
          ></mdui-text-field>

          <mdui-select v-model="image" label="Image" required>
            <mdui-menu-item v-for="img in images" :key="img" :value="img">
              {{ img }}
            </mdui-menu-item>
          </mdui-select>
        </div>
      </div>

      <!-- Resources -->
      <div class="form-section">
        <h3 class="form-section-title">Resources</h3>
        <div class="form-grid">
          <div class="slider-field">
            <label>CPU Cores: {{ cpuCores }}</label>
            <mdui-slider v-model="cpuCores" :min="1" :max="32" :step="1"></mdui-slider>
          </div>

          <div class="slider-field">
            <label>Memory: {{ memoryGb }} GB</label>
            <mdui-slider v-model="memoryGb" :min="1" :max="128" :step="1"></mdui-slider>
          </div>

          <div class="slider-field" v-if="instanceType !== 'container'">
            <label>Disk Size: {{ diskSizeGb }} GB</label>
            <mdui-slider v-model="diskSizeGb" :min="10" :max="2000" :step="10"></mdui-slider>
          </div>
        </div>
      </div>

      <!-- Network -->
      <div class="form-section">
        <h3 class="form-section-title">Network</h3>
        <mdui-switch v-model="assignPublicIp">
          Assign Public IP Address
        </mdui-switch>
      </div>

      <!-- Error Message -->
      <div v-if="error" class="error-message">
        {{ error }}
      </div>

      <!-- Actions -->
      <div class="form-actions">
        <mdui-button variant="outlined" @click="goBack">Cancel</mdui-button>
        <mdui-button variant="filled" :loading="creating" @click="createInstance">
          Create Instance
        </mdui-button>
      </div>
    </mdui-card>
  </div>
</template>

<style scoped>
.create-instance-view {
  max-width: 800px;
  margin: 0 auto;
}

.header {
  display: flex;
  align-items: center;
  gap: 16px;
  margin-bottom: 24px;
}

.form-card {
  padding: 24px;
}

.form-section {
  margin-bottom: 32px;
}

.form-section-title {
  font-size: 16px;
  font-weight: 600;
  margin-bottom: 16px;
  color: var(--zixiao-purple);
}

.type-grid {
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 16px;
}

.type-option {
  padding: 20px;
  text-align: center;
  cursor: pointer;
  border: 2px solid transparent;
  transition: all 0.2s;
}

.type-option:hover {
  border-color: var(--zixiao-purple-light);
}

.type-option.selected {
  border-color: var(--zixiao-purple);
  background: rgba(139, 92, 246, 0.1);
}

.type-icon {
  font-size: 40px;
  color: var(--zixiao-purple);
  margin-bottom: 12px;
}

.type-label {
  font-size: 16px;
  font-weight: 600;
  margin-bottom: 4px;
}

.type-description {
  font-size: 12px;
  opacity: 0.7;
}

.form-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(250px, 1fr));
  gap: 16px;
}

.slider-field {
  padding: 16px 0;
}

.slider-field label {
  display: block;
  margin-bottom: 8px;
  font-weight: 500;
}

.error-message {
  padding: 12px 16px;
  background: rgba(239, 68, 68, 0.1);
  color: #ef4444;
  border-radius: 8px;
  margin-bottom: 24px;
}

.form-actions {
  display: flex;
  justify-content: flex-end;
  gap: 12px;
  padding-top: 16px;
  border-top: 1px solid rgb(var(--mdui-color-outline-light));
}

.mdui-theme-dark .form-actions {
  border-color: rgb(var(--mdui-color-outline-dark));
}

@media (max-width: 640px) {
  .type-grid {
    grid-template-columns: 1fr;
  }
}
</style>
