<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { useRouter } from 'vue-router'

// Import MDUI icons
import '@mdui/icons/computer.js'
import '@mdui/icons/open-in-new.js'
import '@mdui/icons/refresh.js'

interface Desktop {
  id: string
  name: string
  type: 'vm' | 'container'
  status: 'running' | 'stopped'
  ip?: string
}

const router = useRouter()
const desktops = ref<Desktop[]>([])
const loading = ref(true)
const isMobile = ref(false)

async function loadDesktops() {
  loading.value = true

  // Mock data - replace with actual API calls
  desktops.value = [
    { id: 'vm-001', name: 'Windows 11 Dev', type: 'vm', status: 'running', ip: '192.168.1.100' },
    { id: 'vm-002', name: 'Ubuntu Desktop', type: 'vm', status: 'running', ip: '192.168.1.101' },
    { id: 'vm-003', name: 'CentOS Server', type: 'vm', status: 'stopped' },
  ]

  loading.value = false
}

function connectToDesktop(desktop: Desktop) {
  if (desktop.status !== 'running') {
    alert('Desktop is not running')
    return
  }
  router.push(`/desktop/${desktop.id}`)
}

function checkMobile() {
  isMobile.value = window.innerWidth < 768 ||
    /Android|webOS|iPhone|iPad|iPod|BlackBerry|IEMobile|Opera Mini/i.test(navigator.userAgent)
}

onMounted(() => {
  checkMobile()
  window.addEventListener('resize', checkMobile)
  loadDesktops()
})
</script>

<template>
  <div class="home-view">
    <div class="header">
      <h1 class="page-title">Available Desktops</h1>
      <mdui-button-icon @click="loadDesktops" :loading="loading">
        <mdui-icon-refresh></mdui-icon-refresh>
      </mdui-button-icon>
    </div>

    <!-- Mobile notice -->
    <mdui-card v-if="isMobile" class="mobile-notice">
      <p>You're accessing from a mobile device. Touch controls are enabled.</p>
    </mdui-card>

    <div v-if="loading" class="loading">
      <mdui-circular-progress></mdui-circular-progress>
    </div>

    <div v-else class="desktop-list">
      <mdui-card
        v-for="desktop in desktops"
        :key="desktop.id"
        class="desktop-card"
        :class="{ disabled: desktop.status !== 'running' }"
        clickable
        @click="connectToDesktop(desktop)"
      >
        <div class="desktop-content">
          <div class="desktop-icon">
            <mdui-icon-computer></mdui-icon-computer>
          </div>
          <div class="desktop-info">
            <div class="desktop-name">{{ desktop.name }}</div>
            <div class="desktop-meta">
              <span :class="['status', desktop.status]">{{ desktop.status }}</span>
              <span class="type">{{ desktop.type }}</span>
              <span v-if="desktop.ip">{{ desktop.ip }}</span>
            </div>
          </div>
          <mdui-icon-open-in-new v-if="desktop.status === 'running'" class="connect-icon"></mdui-icon-open-in-new>
        </div>
      </mdui-card>
    </div>

    <!-- Instructions -->
    <mdui-card class="instructions">
      <h3>How to use</h3>
      <ul>
        <li>Click on a running desktop to connect</li>
        <li>Use keyboard and mouse as normal</li>
        <li>On mobile: tap to click, two-finger pinch to zoom</li>
        <li>Press Ctrl+Alt+Shift to show the control panel</li>
      </ul>
    </mdui-card>
  </div>
</template>

<style scoped>
.home-view {
  max-width: 800px;
  margin: 0 auto;
}

.header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 24px;
}

.page-title {
  font-size: 24px;
  font-weight: 600;
  color: var(--zixiao-purple);
  margin: 0;
}

.mobile-notice {
  padding: 12px 16px;
  margin-bottom: 16px;
  background: rgba(139, 92, 246, 0.1);
}

.mobile-notice p {
  margin: 0;
  font-size: 14px;
  color: var(--zixiao-purple);
}

.loading {
  display: flex;
  justify-content: center;
  padding: 48px;
}

.desktop-list {
  display: flex;
  flex-direction: column;
  gap: 12px;
  margin-bottom: 24px;
}

.desktop-card {
  padding: 16px;
}

.desktop-card.disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.desktop-content {
  display: flex;
  align-items: center;
  gap: 16px;
}

.desktop-icon {
  width: 48px;
  height: 48px;
  display: flex;
  align-items: center;
  justify-content: center;
  background: rgba(139, 92, 246, 0.1);
  border-radius: 12px;
  color: var(--zixiao-purple);
  font-size: 24px;
}

.desktop-info {
  flex: 1;
}

.desktop-name {
  font-size: 16px;
  font-weight: 600;
  margin-bottom: 4px;
}

.desktop-meta {
  display: flex;
  gap: 8px;
  font-size: 12px;
  opacity: 0.7;
}

.status {
  padding: 2px 8px;
  border-radius: 4px;
  text-transform: capitalize;
}

.status.running {
  background: rgba(34, 197, 94, 0.15);
  color: #22c55e;
}

.status.stopped {
  background: rgba(239, 68, 68, 0.15);
  color: #ef4444;
}

.type {
  padding: 2px 8px;
  background: rgba(139, 92, 246, 0.1);
  border-radius: 4px;
  text-transform: uppercase;
}

.connect-icon {
  color: var(--zixiao-purple);
}

.instructions {
  padding: 20px;
}

.instructions h3 {
  font-size: 16px;
  font-weight: 600;
  margin: 0 0 12px 0;
  color: var(--zixiao-purple);
}

.instructions ul {
  margin: 0;
  padding-left: 20px;
}

.instructions li {
  margin-bottom: 8px;
  font-size: 14px;
  opacity: 0.8;
}
</style>
