<script setup lang="ts">
import { ref, onMounted, onUnmounted, watch } from 'vue'
import { useRoute, useRouter } from 'vue-router'

// Import MDUI icons
import '@mdui/icons/close.js'
import '@mdui/icons/fullscreen.js'
import '@mdui/icons/fullscreen-exit.js'
import '@mdui/icons/keyboard.js'
import '@mdui/icons/content-paste.js'
import '@mdui/icons/settings.js'

const route = useRoute()
const router = useRouter()

const desktopId = ref(route.params.id as string)
const canvasRef = ref<HTMLCanvasElement | null>(null)

// Connection state
const connected = ref(false)
const connecting = ref(true)
const error = ref<string | null>(null)

// UI state
const isFullscreen = ref(false)
const showControls = ref(false)
const showKeyboard = ref(false)
const isTouchDevice = ref(false)

// Touch handling
const lastTouchTime = ref(0)
const touchStartPos = ref({ x: 0, y: 0 })

function exitDesktop() {
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

function handleKeyDown(event: KeyboardEvent) {
  // Ctrl+Alt+Shift to show controls
  if (event.ctrlKey && event.altKey && event.shiftKey) {
    toggleControls()
    event.preventDefault()
  }
}

// Touch event handlers for mobile
function handleTouchStart(event: TouchEvent) {
  if (event.touches.length === 1) {
    touchStartPos.value = {
      x: event.touches[0].clientX,
      y: event.touches[0].clientY
    }
  }
}

function handleTouchEnd(event: TouchEvent) {
  const now = Date.now()
  if (now - lastTouchTime.value < 300) {
    // Double tap - right click
    sendRightClick()
  } else {
    // Single tap - left click
    sendLeftClick()
  }
  lastTouchTime.value = now
}

function sendLeftClick() {
  // TODO: Send left click via VNC/SPICE
  console.log('Left click')
}

function sendRightClick() {
  // TODO: Send right click via VNC/SPICE
  console.log('Right click')
}

async function connect() {
  connecting.value = true
  error.value = null

  try {
    // TODO: Implement actual noVNC connection
    // import RFB from '@novnc/novnc/core/rfb'
    // const rfb = new RFB(canvasRef.value, `ws://localhost:6080/websockify?token=${desktopId.value}`)

    // Simulate connection for now
    await new Promise(resolve => setTimeout(resolve, 1500))
    connected.value = true
    connecting.value = false

    drawPlaceholder()
  } catch (e) {
    error.value = e instanceof Error ? e.message : 'Connection failed'
    connecting.value = false
  }
}

function drawPlaceholder() {
  const canvas = canvasRef.value
  if (!canvas) return

  const ctx = canvas.getContext('2d')
  if (!ctx) return

  // Set canvas size
  canvas.width = window.innerWidth
  canvas.height = window.innerHeight

  // Draw placeholder
  ctx.fillStyle = '#204C63'
  ctx.fillRect(0, 0, canvas.width, canvas.height)

  ctx.fillStyle = '#F5FAD8'
  ctx.font = '24px sans-serif'
  ctx.textAlign = 'center'
  ctx.fillText('Web Desktop Viewer', canvas.width / 2, canvas.height / 2 - 40)
  ctx.font = '16px sans-serif'
  ctx.fillText(`Desktop: ${desktopId.value}`, canvas.width / 2, canvas.height / 2)
  ctx.fillText('noVNC integration coming soon', canvas.width / 2, canvas.height / 2 + 30)
  ctx.font = '14px sans-serif'
  ctx.fillStyle = '#FCD34D'
  ctx.fillText('Press Ctrl+Alt+Shift to show controls', canvas.width / 2, canvas.height / 2 + 70)
}

function handleResize() {
  if (connected.value) {
    drawPlaceholder()
  }
}

onMounted(() => {
  isTouchDevice.value = 'ontouchstart' in window || navigator.maxTouchPoints > 0
  window.addEventListener('keydown', handleKeyDown)
  window.addEventListener('resize', handleResize)
  connect()
})

onUnmounted(() => {
  window.removeEventListener('keydown', handleKeyDown)
  window.removeEventListener('resize', handleResize)
})
</script>

<template>
  <div class="desktop-view" @touchstart="handleTouchStart" @touchend="handleTouchEnd">
    <!-- Control Panel -->
    <div v-if="showControls" class="control-panel">
      <mdui-button-icon @click="exitDesktop" title="Exit">
        <mdui-icon-close></mdui-icon-close>
      </mdui-button-icon>
      <mdui-button-icon @click="toggleFullscreen" title="Fullscreen">
        <mdui-icon-fullscreen v-if="!isFullscreen"></mdui-icon-fullscreen>
        <mdui-icon-fullscreen-exit v-else></mdui-icon-fullscreen-exit>
      </mdui-button-icon>
      <mdui-button-icon v-if="isTouchDevice" @click="toggleKeyboard" title="Keyboard">
        <mdui-icon-keyboard></mdui-icon-keyboard>
      </mdui-button-icon>
      <mdui-button-icon title="Clipboard">
        <mdui-icon-content-paste></mdui-icon-content-paste>
      </mdui-button-icon>
    </div>

    <!-- Connection Status -->
    <div v-if="connecting" class="overlay">
      <mdui-circular-progress></mdui-circular-progress>
      <p>Connecting to desktop...</p>
    </div>

    <div v-else-if="error" class="overlay error">
      <p>{{ error }}</p>
      <mdui-button @click="connect">Retry</mdui-button>
      <mdui-button variant="outlined" @click="exitDesktop">Back</mdui-button>
    </div>

    <!-- Desktop Canvas -->
    <canvas
      ref="canvasRef"
      class="desktop-canvas"
      @click="showControls = false"
    ></canvas>

    <!-- Virtual Keyboard (for mobile) -->
    <div v-if="showKeyboard && isTouchDevice" class="virtual-keyboard">
      <div class="keyboard-row">
        <button v-for="key in ['Esc', 'F1', 'F2', 'F3', 'F4', 'F5', 'F6', 'F7', 'F8', 'F9', 'F10', 'F11', 'F12']" :key="key" class="key">{{ key }}</button>
      </div>
      <div class="keyboard-row">
        <button class="key">Tab</button>
        <button class="key">Ctrl</button>
        <button class="key">Alt</button>
        <button class="key wide">Space</button>
        <button class="key">Win</button>
        <button class="key">Del</button>
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

.overlay.error mdui-button {
  margin-top: 8px;
}

.desktop-canvas {
  width: 100%;
  height: 100%;
  touch-action: none;
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
