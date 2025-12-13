<script setup lang="ts">
import { ref, onMounted, onUnmounted } from 'vue'
import { useRoute, useRouter } from 'vue-router'

// Import MDUI icons
import '@mdui/icons/close.js'
import '@mdui/icons/fullscreen.js'
import '@mdui/icons/fullscreen-exit.js'
import '@mdui/icons/keyboard.js'
import '@mdui/icons/mouse.js'

const route = useRoute()
const router = useRouter()

const desktopId = ref(route.params.id as string)
const isFullscreen = ref(false)
const showToolbar = ref(true)
const canvasRef = ref<HTMLCanvasElement | null>(null)

// Connection state
const connected = ref(false)
const connecting = ref(true)
const error = ref<string | null>(null)

function exitDesktop() {
  router.push('/')
}

function toggleFullscreen() {
  if (!document.fullscreenElement) {
    document.documentElement.requestFullscreen()
    isFullscreen.value = true
  } else {
    document.exitFullscreen()
    isFullscreen.value = false
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

async function connect() {
  connecting.value = true
  error.value = null

  try {
    // TODO: Implement actual SPICE/VNC connection
    // For now, simulate connection
    await new Promise(resolve => setTimeout(resolve, 1500))
    connected.value = true
    connecting.value = false

    // Start rendering loop
    startRendering()
  } catch (e) {
    error.value = e instanceof Error ? e.message : 'Connection failed'
    connecting.value = false
  }
}

function startRendering() {
  const canvas = canvasRef.value
  if (!canvas) return

  const ctx = canvas.getContext('2d')
  if (!ctx) return

  // Set canvas size
  canvas.width = window.innerWidth
  canvas.height = window.innerHeight - (showToolbar.value ? 48 : 0)

  // Draw placeholder
  ctx.fillStyle = '#204C63'
  ctx.fillRect(0, 0, canvas.width, canvas.height)

  ctx.fillStyle = '#F5FAD8'
  ctx.font = '24px sans-serif'
  ctx.textAlign = 'center'
  ctx.fillText('Desktop Viewer', canvas.width / 2, canvas.height / 2 - 20)
  ctx.font = '16px sans-serif'
  ctx.fillText(`Connected to: ${desktopId.value}`, canvas.width / 2, canvas.height / 2 + 20)
  ctx.fillText('SPICE/VNC protocol integration coming soon', canvas.width / 2, canvas.height / 2 + 50)
}

onMounted(() => {
  window.addEventListener('keydown', handleKeyDown)
  connect()
})

onUnmounted(() => {
  window.removeEventListener('keydown', handleKeyDown)
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
      </div>
      <div class="toolbar-right">
        <mdui-button-icon title="Keyboard">
          <mdui-icon-keyboard></mdui-icon-keyboard>
        </mdui-button-icon>
        <mdui-button-icon title="Mouse">
          <mdui-icon-mouse></mdui-icon-mouse>
        </mdui-button-icon>
        <mdui-button-icon @click="toggleFullscreen">
          <mdui-icon-fullscreen v-if="!isFullscreen"></mdui-icon-fullscreen>
          <mdui-icon-fullscreen-exit v-else></mdui-icon-fullscreen-exit>
        </mdui-button-icon>
      </div>
    </div>

    <!-- Desktop Canvas -->
    <div class="canvas-container">
      <div v-if="connecting" class="overlay">
        <mdui-circular-progress></mdui-circular-progress>
        <p>Connecting to desktop...</p>
      </div>

      <div v-else-if="error" class="overlay error">
        <p>{{ error }}</p>
        <mdui-button @click="connect">Retry</mdui-button>
      </div>

      <canvas ref="canvasRef" class="desktop-canvas"></canvas>
    </div>

    <!-- Hint -->
    <div v-if="showToolbar" class="hint">
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
  background: var(--zixiao-purple);
  color: white;
}

.toolbar-left,
.toolbar-right {
  display: flex;
  align-items: center;
  gap: 8px;
}

.desktop-title {
  font-weight: 500;
}

.canvas-container {
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
  background: rgba(0, 0, 0, 0.8);
  color: white;
  z-index: 10;
}

.overlay.error {
  color: #ef4444;
}

.desktop-canvas {
  width: 100%;
  height: 100%;
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
}
</style>
