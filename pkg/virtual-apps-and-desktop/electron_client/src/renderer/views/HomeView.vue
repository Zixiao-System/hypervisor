<script setup lang="ts">
import { ref, onMounted } from 'vue'

// Import MDUI icons
import '@mdui/icons/computer.js'
import '@mdui/icons/star.js'
import '@mdui/icons/star-outline.js'
import '@mdui/icons/history.js'
import '@mdui/icons/add.js'
import '@mdui/icons/open-in-full.js'

interface Desktop {
  id: string
  name: string
  type: 'vm' | 'container'
  status: 'running' | 'stopped'
  ip?: string
  isBookmarked: boolean
}

const desktops = ref<Desktop[]>([])
const recentConnections = ref<Desktop[]>([])
const bookmarks = ref<Desktop[]>([])
const loading = ref(true)

async function loadData() {
  loading.value = true

  // Mock data - replace with actual API calls
  desktops.value = [
    { id: 'vm-001', name: 'Windows 11 Dev', type: 'vm', status: 'running', ip: '192.168.1.100', isBookmarked: true },
    { id: 'vm-002', name: 'Ubuntu Desktop', type: 'vm', status: 'running', ip: '192.168.1.101', isBookmarked: false },
    { id: 'vm-003', name: 'macOS Workspace', type: 'vm', status: 'stopped', isBookmarked: true },
  ]

  if (window.electronAPI) {
    const settings = await window.electronAPI.getSettings()
    recentConnections.value = settings.recentConnections || []
  }

  bookmarks.value = desktops.value.filter(d => d.isBookmarked)
  loading.value = false
}

async function toggleBookmark(desktop: Desktop) {
  desktop.isBookmarked = !desktop.isBookmarked
  if (window.electronAPI) {
    if (desktop.isBookmarked) {
      await window.electronAPI.addBookmark(desktop)
    } else {
      await window.electronAPI.removeBookmark(desktop.id)
    }
  }
  bookmarks.value = desktops.value.filter(d => d.isBookmarked)
}

async function connectToDesktop(desktop: Desktop) {
  if (desktop.status !== 'running') {
    alert('Desktop is not running')
    return
  }

  if (window.electronAPI) {
    await window.electronAPI.addRecentConnection(desktop)
    await window.electronAPI.openFullscreenDesktop(desktop.id)
  }
}

onMounted(() => {
  loadData()
})
</script>

<template>
  <div class="home-view">
    <h1 class="page-title">Virtual Desktops</h1>

    <div v-if="loading" class="loading">
      <mdui-circular-progress></mdui-circular-progress>
    </div>

    <template v-else>
      <!-- Bookmarks Section -->
      <section v-if="bookmarks.length > 0" class="section">
        <h2 class="section-title">
          <mdui-icon-star></mdui-icon-star>
          Bookmarks
        </h2>
        <div class="desktop-grid">
          <mdui-card
            v-for="desktop in bookmarks"
            :key="desktop.id"
            class="desktop-card"
            clickable
            @click="connectToDesktop(desktop)"
          >
            <div class="desktop-icon">
              <mdui-icon-computer></mdui-icon-computer>
            </div>
            <div class="desktop-info">
              <div class="desktop-name">{{ desktop.name }}</div>
              <div class="desktop-meta">
                <span :class="['status', desktop.status]">{{ desktop.status }}</span>
                <span v-if="desktop.ip">{{ desktop.ip }}</span>
              </div>
            </div>
            <div class="desktop-actions">
              <mdui-button-icon @click.stop="toggleBookmark(desktop)">
                <mdui-icon-star v-if="desktop.isBookmarked"></mdui-icon-star>
                <mdui-icon-star-outline v-else></mdui-icon-star-outline>
              </mdui-button-icon>
              <mdui-button-icon
                v-if="desktop.status === 'running'"
                @click.stop="connectToDesktop(desktop)"
              >
                <mdui-icon-open-in-full></mdui-icon-open-in-full>
              </mdui-button-icon>
            </div>
          </mdui-card>
        </div>
      </section>

      <!-- All Desktops Section -->
      <section class="section">
        <h2 class="section-title">
          <mdui-icon-computer></mdui-icon-computer>
          All Desktops
        </h2>
        <div class="desktop-grid">
          <mdui-card
            v-for="desktop in desktops"
            :key="desktop.id"
            class="desktop-card"
            clickable
            @click="connectToDesktop(desktop)"
          >
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
            <div class="desktop-actions">
              <mdui-button-icon @click.stop="toggleBookmark(desktop)">
                <mdui-icon-star v-if="desktop.isBookmarked"></mdui-icon-star>
                <mdui-icon-star-outline v-else></mdui-icon-star-outline>
              </mdui-button-icon>
              <mdui-button-icon
                v-if="desktop.status === 'running'"
                @click.stop="connectToDesktop(desktop)"
              >
                <mdui-icon-open-in-full></mdui-icon-open-in-full>
              </mdui-button-icon>
            </div>
          </mdui-card>
        </div>
      </section>

      <!-- Recent Connections -->
      <section v-if="recentConnections.length > 0" class="section">
        <h2 class="section-title">
          <mdui-icon-history></mdui-icon-history>
          Recent Connections
        </h2>
        <mdui-list>
          <mdui-list-item
            v-for="connection in recentConnections"
            :key="connection.id"
            @click="connectToDesktop(connection)"
          >
            <mdui-icon-computer slot="icon"></mdui-icon-computer>
            <span slot="headline">{{ connection.name }}</span>
            <span slot="description">{{ connection.ip }}</span>
          </mdui-list-item>
        </mdui-list>
      </section>
    </template>
  </div>
</template>

<style scoped>
.home-view {
  max-width: 1200px;
  margin: 0 auto;
}

.page-title {
  font-size: 28px;
  font-weight: 600;
  color: var(--zixiao-purple);
  margin-bottom: 24px;
}

.loading {
  display: flex;
  justify-content: center;
  padding: 48px;
}

.section {
  margin-bottom: 32px;
}

.section-title {
  display: flex;
  align-items: center;
  gap: 8px;
  font-size: 18px;
  font-weight: 600;
  margin-bottom: 16px;
  color: var(--zixiao-purple-dark);
}

.desktop-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(280px, 1fr));
  gap: 16px;
}

.desktop-card {
  padding: 16px;
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

.desktop-actions {
  display: flex;
  gap: 4px;
}
</style>
