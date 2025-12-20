<script setup lang="ts">
import { ref, onMounted, onUnmounted, nextTick } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import RFB from '@novnc/novnc/core/rfb'

// Import MDUI icons
import '@mdui/icons/close.js'
import '@mdui/icons/fullscreen.js'
import '@mdui/icons/fullscreen-exit.js'
import '@mdui/icons/keyboard.js'
import '@mdui/icons/content-paste.js'
import '@mdui/icons/screen-share.js'

const route = useRoute()
const router = useRouter()

const desktopId = ref(route.params.id as string)
const containerRef = ref<HTMLDivElement | null>(null)
const isFullscreen = ref(false)
const showToolbar = ref(true)

// Connection state
const connected = ref(false)
const connecting = ref(true)
const error = ref<string | null>(null)
const protocol = ref<'vnc' | 'spice'>('vnc')
const viewOnly = ref(false)

// noVNC RFB instance
let rfb: RFB | null = null

interface DesktopConfig {
  protocol: 'vnc' | 'spice'
  host: string
  port: number
  password?: string
  token?: string
}

async function fetchDesktopConfig(): Promise<DesktopConfig> {
  // In production, fetch from hypervisor API
  // const response = await fetch(`/api/v1/desktops/${desktopId.value}/connection`)
  // return response.json()

  return {
    protocol: 'vnc',
    host: 'localhost',
    port: 6080,
    token: desktopId.value,
  }
}

function getWebSocketUrl(config: DesktopConfig): string {
  const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:'
  return `${protocol}//${config.host}:${config.port}/websockify?token=${config.token}`
}

async function connect() {
  connecting.value = true
  error.value = null

  await nextTick()

  if (!containerRef.value) {
    error.value = 'Display container not found'
    connecting.value = false
    return
  }

  try {
    const config = await fetchDesktopConfig()
    protocol.value = config.protocol

    const wsUrl = getWebSocketUrl(config)

    // Create RFB connection
    rfb = new RFB(containerRef.value, wsUrl, {
      credentials: config.password ? { password: config.password } : undefined,
    })

    // Configure RFB options
    rfb.viewOnly = viewOnly.value
    rfb.scaleViewport = true
    rfb.clipViewport = true
    rfb.resizeSession = true
    rfb.showDotCursor = true

    // Event handlers
    rfb.addEventListener('connect', handleConnect)
    rfb.addEventListener('disconnect', handleDisconnect)
    rfb.addEventListener('credentialsrequired', handleCredentialsRequired)
    rfb.addEventListener('securityfailure', handleSecurityFailure)
    rfb.addEventListener('clipboard', handleClipboard)

  } catch (e) {
    error.value = e instanceof Error ? e.message : 'Connection failed'
    connecting.value = false
  }
}

function handleConnect() {
  connecting.value = false
  connected.value = true
  console.log('VNC connected')
}

function handleDisconnect(e: CustomEvent) {
  connected.value = false
  connecting.value = false

  if (e.detail.clean) {
    console.log('VNC disconnected cleanly')
  } else {
    error.value = 'Connection lost unexpectedly'
  }
}

function handleCredentialsRequired() {
  const password = prompt('VNC Password:')
  if (password && rfb) {
    rfb.sendCredentials({ password })
  } else {
    disconnect()
  }
}

function handleSecurityFailure(e: CustomEvent) {
  error.value = `Security failure: ${e.detail.reason}`
  connecting.value = false
}

function handleClipboard(e: CustomEvent) {
  navigator.clipboard.writeText(e.detail.text).catch(console.error)
}

function disconnect() {
  if (rfb) {
    rfb.disconnect()
    rfb = null
  }
}

function exitDesktop() {
  disconnect()
  router.push('/')
}

async function toggleFullscreen() {
  try {
    if (!document.fullscreenElement) {
      await document.documentElement.requestFullscreen()
      isFullscreen.value = true
    } else {
      await document.exitFullscreen()
      isFullscreen.value = false
    }
  } catch (e) {
    console.error('Fullscreen error:', e)
  }
}

function toggleViewOnly() {
  viewOnly.value = !viewOnly.value
  if (rfb) {
    rfb.viewOnly = viewOnly.value
  }
}

function sendCtrlAltDel() {
  rfb?.sendCtrlAltDel()
}

async function sendClipboard() {
  try {
    const text = await navigator.clipboard.readText()
    rfb?.clipboardPasteFrom(text)
  } catch (e) {
    console.error('Clipboard error:', e)
  }
}

function handleKeyDown(event: KeyboardEvent) {
  // Show toolbar on Ctrl+Alt
  if (event.ctrlKey && event.altKey) {
    showToolbar.value = !showToolbar.value
  }

  // Exit fullscreen on Escape
  if (event.key === 'Escape' && isFullscreen.value) {
    document.exitFullscreen()
    isFullscreen.value = false
  }
}

onMounted(() => {
  window.addEventListener('keydown', handleKeyDown)
  connect()
})

onUnmounted(() => {
  window.removeEventListener('keydown', handleKeyDown)
  disconnect()
})
</script>

<template>
  <div class="desktop-view">
    <!-- Toolbar -->
    <div v-if="showToolbar" class="toolbar">
      <div class="toolbar-left">
        <mdui-button-icon @click="exitDesktop">
          <mdui-icon-close></mdui-icon-close>
        </mdui-button-icon>
        <span class="desktop-title">{{ desktopId }}</span>
        <span class="protocol-badge">{{ protocol.toUpperCase() }}</span>
      </div>
      <div class="toolbar-right">
        <mdui-button-icon @click="sendCtrlAltDel" title="Ctrl+Alt+Del">
          <mdui-icon-keyboard></mdui-icon-keyboard>
        </mdui-button-icon>
        <mdui-button-icon @click="sendClipboard" title="Paste Clipboard">
          <mdui-icon-content-paste></mdui-icon-content-paste>
        </mdui-button-icon>
        <mdui-button-icon
          @click="toggleViewOnly"
          :class="{ active: viewOnly }"
          title="View Only"
        >
          <mdui-icon-screen-share></mdui-icon-screen-share>
        </mdui-button-icon>
        <mdui-button-icon @click="toggleFullscreen">
          <mdui-icon-fullscreen v-if="!isFullscreen"></mdui-icon-fullscreen>
          <mdui-icon-fullscreen-exit v-else></mdui-icon-fullscreen-exit>
        </mdui-button-icon>
      </div>
    </div>

    <!-- Desktop Display Container -->
    <div class="display-container">
      <div v-if="connecting" class="overlay">
        <mdui-circular-progress></mdui-circular-progress>
        <p>Connecting to desktop...</p>
      </div>

      <div v-else-if="error" class="overlay error">
        <p>{{ error }}</p>
        <div class="overlay-actions">
          <mdui-button @click="connect">Retry</mdui-button>
          <mdui-button variant="outlined" @click="exitDesktop">Back</mdui-button>
        </div>
      </div>

      <div ref="containerRef" class="vnc-display"></div>
    </div>

    <!-- Hint -->
    <div v-if="showToolbar && connected" class="hint">
      Press Ctrl+Alt to toggle toolbar
    </div>
  </div>
</template>

<style scoped>
.desktop-view {
  height: 100vh;
  display: flex;
  flex-direction: column;
  background: #000;
}

.toolbar {
  height: 48px;
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 0 8px;
  background: var(--zixiao-purple, #8B5CF6);
  color: white;
  flex-shrink: 0;
}

.toolbar-left,
.toolbar-right {
  display: flex;
  align-items: center;
  gap: 8px;
}

.toolbar-left mdui-button-icon,
.toolbar-right mdui-button-icon {
  color: white;
}

.toolbar-right mdui-button-icon.active {
  background: rgba(255, 255, 255, 0.2);
  border-radius: 50%;
}

.desktop-title {
  font-weight: 500;
}

.protocol-badge {
  font-size: 10px;
  padding: 2px 6px;
  background: rgba(255, 255, 255, 0.2);
  border-radius: 4px;
}

.display-container {
  flex: 1;
  position: relative;
  overflow: hidden;
}

.overlay {
  position: absolute;
  top: 0;
  left: 0;
  right: 0;
  bottom: 0;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 16px;
  background: rgba(0, 0, 0, 0.9);
  color: white;
  z-index: 10;
}

.overlay.error {
  color: #ef4444;
}

.overlay-actions {
  display: flex;
  gap: 8px;
  margin-top: 8px;
}

.vnc-display {
  width: 100%;
  height: 100%;
}

/* noVNC injects a canvas element */
.vnc-display :deep(canvas) {
  width: 100%;
  height: 100%;
  object-fit: contain;
}

.hint {
  position: fixed;
  bottom: 16px;
  left: 50%;
  transform: translateX(-50%);
  padding: 8px 16px;
  background: rgba(0, 0, 0, 0.7);
  color: white;
  border-radius: 8px;
  font-size: 12px;
  pointer-events: none;
}
</style>
