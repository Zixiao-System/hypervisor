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

// Connection state
const connected = ref(false)
const connecting = ref(true)
const error = ref<string | null>(null)

// UI state
const isFullscreen = ref(false)
const showControls = ref(true)
const showKeyboard = ref(false)
const isTouchDevice = ref(false)
const viewOnly = ref(false)

// noVNC RFB instance
let rfb: RFB | null = null

// Touch handling
const lastTouchTime = ref(0)
let touchCount = 0
let touchTimer: number | null = null

function getWebSocketUrl(): string {
  const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:'
  const wsPort = import.meta.env.VITE_WEBSOCKIFY_PORT || 6080
  const wsHost = window.location.hostname
  return `${protocol}//${wsHost}:${wsPort}/websockify?token=${desktopId.value}`
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
    const wsUrl = getWebSocketUrl()

    // Create RFB connection
    rfb = new RFB(containerRef.value, wsUrl, {
      credentials: undefined,
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

function toggleControls() {
  showControls.value = !showControls.value
}

function toggleKeyboard() {
  showKeyboard.value = !showKeyboard.value
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

function sendKey(keysym: number, down: boolean = true) {
  rfb?.sendKey(keysym, undefined, down)
  if (down) {
    setTimeout(() => rfb?.sendKey(keysym, undefined, false), 50)
  }
}

// Virtual keyboard key mappings
const keyMappings: Record<string, number> = {
  'Esc': 0xff1b,
  'F1': 0xffbe, 'F2': 0xffbf, 'F3': 0xffc0, 'F4': 0xffc1,
  'F5': 0xffc2, 'F6': 0xffc3, 'F7': 0xffc4, 'F8': 0xffc5,
  'F9': 0xffc6, 'F10': 0xffc7, 'F11': 0xffc8, 'F12': 0xffc9,
  'Tab': 0xff09, 'Ctrl': 0xffe3, 'Alt': 0xffe9,
  'Space': 0x0020, 'Win': 0xffeb, 'Del': 0xffff,
}

function handleVirtualKey(key: string) {
  const keysym = keyMappings[key]
  if (keysym) {
    sendKey(keysym)
  }
}

function handleKeyDown(event: KeyboardEvent) {
  // Ctrl+Alt+Shift to show controls
  if (event.ctrlKey && event.altKey && event.shiftKey) {
    toggleControls()
    event.preventDefault()
  }
}

// Touch handling for mobile
function handleTouchStart(event: TouchEvent) {
  touchCount = event.touches.length

  // Three-finger tap to show controls
  if (touchCount === 3) {
    if (touchTimer) clearTimeout(touchTimer)
    touchTimer = window.setTimeout(() => {
      toggleControls()
    }, 200)
  }
}

function handleTouchEnd() {
  if (touchTimer) {
    clearTimeout(touchTimer)
    touchTimer = null
  }
}

onMounted(() => {
  isTouchDevice.value = 'ontouchstart' in window || navigator.maxTouchPoints > 0
  window.addEventListener('keydown', handleKeyDown)
  connect()
})

onUnmounted(() => {
  window.removeEventListener('keydown', handleKeyDown)
  disconnect()
})
</script>

<template>
  <div
    class="desktop-view"
    @touchstart="handleTouchStart"
    @touchend="handleTouchEnd"
  >
    <!-- Control Panel -->
    <div v-if="showControls" class="control-panel">
      <mdui-button-icon @click="exitDesktop" title="Exit">
        <mdui-icon-close></mdui-icon-close>
      </mdui-button-icon>
      <mdui-button-icon @click="toggleFullscreen" title="Fullscreen">
        <mdui-icon-fullscreen v-if="!isFullscreen"></mdui-icon-fullscreen>
        <mdui-icon-fullscreen-exit v-else></mdui-icon-fullscreen-exit>
      </mdui-button-icon>
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
      <mdui-button-icon
        v-if="isTouchDevice"
        @click="toggleKeyboard"
        title="Virtual Keyboard"
      >
        <mdui-icon-keyboard></mdui-icon-keyboard>
      </mdui-button-icon>
    </div>

    <!-- Connection Status -->
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

    <!-- VNC Display Container -->
    <div
      ref="containerRef"
      class="vnc-container"
      @click="showControls = false"
    ></div>

    <!-- Virtual Keyboard (for mobile) -->
    <div v-if="showKeyboard && isTouchDevice" class="virtual-keyboard">
      <div class="keyboard-row">
        <button
          v-for="key in ['Esc', 'F1', 'F2', 'F3', 'F4', 'F5', 'F6', 'F7', 'F8', 'F9', 'F10', 'F11', 'F12']"
          :key="key"
          class="key"
          @click="handleVirtualKey(key)"
        >
          {{ key }}
        </button>
      </div>
      <div class="keyboard-row">
        <button class="key" @click="handleVirtualKey('Tab')">Tab</button>
        <button class="key" @click="handleVirtualKey('Ctrl')">Ctrl</button>
        <button class="key" @click="handleVirtualKey('Alt')">Alt</button>
        <button class="key wide" @click="handleVirtualKey('Space')">Space</button>
        <button class="key" @click="handleVirtualKey('Win')">Win</button>
        <button class="key" @click="handleVirtualKey('Del')">Del</button>
      </div>
    </div>

    <!-- Touch hint -->
    <div v-if="isTouchDevice && connected && !showControls" class="touch-hint">
      Three-finger tap to show controls
    </div>
  </div>
</template>

<style scoped>
.desktop-view {
  position: fixed;
  top: 0;
  left: 0;
  right: 0;
  bottom: 0;
  background: #000;
  overflow: hidden;
}

.control-panel {
  position: absolute;
  top: 16px;
  left: 50%;
  transform: translateX(-50%);
  display: flex;
  gap: 8px;
  padding: 8px 16px;
  background: rgba(139, 92, 246, 0.9);
  border-radius: 24px;
  z-index: 100;
  backdrop-filter: blur(10px);
}

.control-panel mdui-button-icon {
  color: white;
}

.control-panel mdui-button-icon.active {
  background: rgba(255, 255, 255, 0.2);
  border-radius: 50%;
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
  z-index: 50;
}

.overlay.error {
  color: #ef4444;
}

.overlay-actions {
  display: flex;
  gap: 8px;
  margin-top: 8px;
}

.vnc-container {
  width: 100%;
  height: 100%;
}

/* noVNC injects a canvas element */
.vnc-container :deep(canvas) {
  width: 100%;
  height: 100%;
  object-fit: contain;
}

.virtual-keyboard {
  position: absolute;
  bottom: 0;
  left: 0;
  right: 0;
  background: rgba(0, 0, 0, 0.9);
  padding: 8px;
  z-index: 100;
}

.keyboard-row {
  display: flex;
  justify-content: center;
  gap: 4px;
  margin-bottom: 4px;
}

.key {
  padding: 8px 12px;
  background: #333;
  color: white;
  border: none;
  border-radius: 4px;
  font-size: 12px;
  min-width: 40px;
  cursor: pointer;
  touch-action: manipulation;
}

.key:active {
  background: var(--zixiao-purple);
}

.key.wide {
  min-width: 120px;
}

.touch-hint {
  position: absolute;
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
