<script setup lang="ts">
import { ref, onMounted, onUnmounted, watch, nextTick } from 'vue'
import { Terminal } from '@xterm/xterm'
import { FitAddon } from '@xterm/addon-fit'
import { WebLinksAddon } from '@xterm/addon-web-links'
import '@xterm/xterm/css/xterm.css'

// Import MDUI icons
import '@mdui/icons/play-arrow.js'
import '@mdui/icons/stop.js'
import '@mdui/icons/close.js'

const props = defineProps<{
  instanceId: string
  visible: boolean
}>()

const emit = defineEmits<{
  (e: 'close'): void
  (e: 'connected'): void
  (e: 'disconnected'): void
  (e: 'error', message: string): void
}>()

const terminalRef = ref<HTMLDivElement | null>(null)
const connected = ref(false)
const connecting = ref(false)

let terminal: Terminal | null = null
let fitAddon: FitAddon | null = null
let websocket: WebSocket | null = null

function initTerminal() {
  if (!terminalRef.value) return

  terminal = new Terminal({
    cursorBlink: true,
    fontSize: 14,
    fontFamily: 'Menlo, Monaco, "Courier New", monospace',
    theme: {
      background: '#1e1e2e',
      foreground: '#cdd6f4',
      cursor: '#f5e0dc',
      cursorAccent: '#1e1e2e',
      selectionBackground: '#585b70',
      black: '#45475a',
      red: '#f38ba8',
      green: '#a6e3a1',
      yellow: '#f9e2af',
      blue: '#89b4fa',
      magenta: '#f5c2e7',
      cyan: '#94e2d5',
      white: '#bac2de',
      brightBlack: '#585b70',
      brightRed: '#f38ba8',
      brightGreen: '#a6e3a1',
      brightYellow: '#f9e2af',
      brightBlue: '#89b4fa',
      brightMagenta: '#f5c2e7',
      brightCyan: '#94e2d5',
      brightWhite: '#a6adc8',
    }
  })

  fitAddon = new FitAddon()
  terminal.loadAddon(fitAddon)
  terminal.loadAddon(new WebLinksAddon())

  terminal.open(terminalRef.value)
  fitAddon.fit()

  // Handle terminal input
  terminal.onData((data) => {
    if (websocket?.readyState === WebSocket.OPEN) {
      websocket.send(data)
    }
  })

  // Handle window resize
  window.addEventListener('resize', handleResize)
}

function handleResize() {
  fitAddon?.fit()
  if (websocket?.readyState === WebSocket.OPEN && terminal) {
    // Send terminal size to server
    const size = { type: 'resize', cols: terminal.cols, rows: terminal.rows }
    websocket.send(JSON.stringify(size))
  }
}

async function connect() {
  if (connecting.value || connected.value) return

  connecting.value = true

  try {
    // Build WebSocket URL for console attachment
    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:'
    const wsUrl = `${protocol}//${window.location.host}/api/v1/instances/${props.instanceId}/console`

    terminal?.write('\x1b[33mConnecting to instance console...\x1b[0m\r\n')

    websocket = new WebSocket(wsUrl)

    websocket.onopen = () => {
      connecting.value = false
      connected.value = true
      terminal?.write('\x1b[32mConnected.\x1b[0m\r\n\r\n')
      emit('connected')

      // Send initial terminal size
      if (terminal) {
        const size = { type: 'resize', cols: terminal.cols, rows: terminal.rows }
        websocket?.send(JSON.stringify(size))
      }
    }

    websocket.onmessage = (event) => {
      if (event.data instanceof Blob) {
        event.data.text().then((text) => terminal?.write(text))
      } else {
        terminal?.write(event.data)
      }
    }

    websocket.onclose = (event) => {
      connected.value = false
      connecting.value = false
      if (event.wasClean) {
        terminal?.write('\r\n\x1b[33mConnection closed.\x1b[0m\r\n')
      } else {
        terminal?.write('\r\n\x1b[31mConnection lost.\x1b[0m\r\n')
      }
      emit('disconnected')
    }

    websocket.onerror = () => {
      connecting.value = false
      terminal?.write('\r\n\x1b[31mConnection failed.\x1b[0m\r\n')
      emit('error', 'WebSocket connection failed')
    }
  } catch (e) {
    connecting.value = false
    const message = e instanceof Error ? e.message : 'Connection failed'
    terminal?.write(`\r\n\x1b[31mError: ${message}\x1b[0m\r\n`)
    emit('error', message)
  }
}

function disconnect() {
  if (websocket) {
    websocket.close()
    websocket = null
  }
}

function dispose() {
  disconnect()
  window.removeEventListener('resize', handleResize)
  terminal?.dispose()
  terminal = null
  fitAddon = null
}

onMounted(() => {
  nextTick(() => {
    initTerminal()
  })
})

onUnmounted(() => {
  dispose()
})

watch(() => props.visible, (visible) => {
  if (visible) {
    nextTick(() => {
      fitAddon?.fit()
      if (!connected.value && !connecting.value) {
        connect()
      }
    })
  }
})

defineExpose({ connect, disconnect })
</script>

<template>
  <div class="terminal-console">
    <div class="terminal-toolbar">
      <span class="terminal-title">Console: {{ instanceId }}</span>
      <div class="terminal-status">
        <span v-if="connecting" class="status connecting">Connecting...</span>
        <span v-else-if="connected" class="status connected">Connected</span>
        <span v-else class="status disconnected">Disconnected</span>
      </div>
      <div class="terminal-actions">
        <mdui-button-icon v-if="!connected && !connecting" @click="connect" title="Connect">
          <mdui-icon-play-arrow></mdui-icon-play-arrow>
        </mdui-button-icon>
        <mdui-button-icon v-else-if="connected" @click="disconnect" title="Disconnect">
          <mdui-icon-stop></mdui-icon-stop>
        </mdui-button-icon>
        <mdui-button-icon @click="emit('close')" title="Close">
          <mdui-icon-close></mdui-icon-close>
        </mdui-button-icon>
      </div>
    </div>
    <div ref="terminalRef" class="terminal-container"></div>
  </div>
</template>

<style scoped>
.terminal-console {
  display: flex;
  flex-direction: column;
  height: 100%;
  background: #1e1e2e;
  border-radius: 8px;
  overflow: hidden;
}

.terminal-toolbar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 8px 16px;
  background: #313244;
  border-bottom: 1px solid #45475a;
}

.terminal-title {
  color: #cdd6f4;
  font-weight: 500;
  font-size: 14px;
}

.terminal-status {
  flex: 1;
  text-align: center;
}

.terminal-status .status {
  font-size: 12px;
  padding: 2px 8px;
  border-radius: 4px;
}

.status.connecting {
  background: #f9e2af;
  color: #1e1e2e;
}

.status.connected {
  background: #a6e3a1;
  color: #1e1e2e;
}

.status.disconnected {
  background: #f38ba8;
  color: #1e1e2e;
}

.terminal-actions {
  display: flex;
  gap: 4px;
}

.terminal-actions mdui-button-icon {
  color: #cdd6f4;
}

.terminal-container {
  flex: 1;
  padding: 8px;
  overflow: hidden;
}
</style>
