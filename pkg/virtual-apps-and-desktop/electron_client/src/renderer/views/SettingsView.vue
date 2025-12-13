<script setup lang="ts">
import { ref, onMounted } from 'vue'

// Import MDUI icons
import '@mdui/icons/light-mode.js'
import '@mdui/icons/dark-mode.js'
import '@mdui/icons/brightness-auto.js'

const theme = ref<'light' | 'dark' | 'auto'>('auto')

async function setTheme(newTheme: 'light' | 'dark' | 'auto') {
  theme.value = newTheme
  if (window.electronAPI) {
    await window.electronAPI.setTheme(newTheme)
  }
  applyTheme()
}

function applyTheme() {
  const body = document.body
  body.classList.remove('mdui-theme-light', 'mdui-theme-dark', 'mdui-theme-auto')
  body.classList.add(`mdui-theme-${theme.value}`)
}

onMounted(async () => {
  if (window.electronAPI) {
    const settings = await window.electronAPI.getSettings()
    theme.value = settings.theme as 'light' | 'dark' | 'auto'
  }
})
</script>

<template>
  <div class="settings-view">
    <h1 class="page-title">Settings</h1>

    <mdui-card class="settings-card">
      <h3 class="card-title">Appearance</h3>
      <div class="theme-options">
        <mdui-card
          :class="['theme-option', { selected: theme === 'light' }]"
          clickable
          @click="setTheme('light')"
        >
          <mdui-icon-light-mode class="theme-icon"></mdui-icon-light-mode>
          <div class="theme-label">Light</div>
        </mdui-card>

        <mdui-card
          :class="['theme-option', { selected: theme === 'dark' }]"
          clickable
          @click="setTheme('dark')"
        >
          <mdui-icon-dark-mode class="theme-icon"></mdui-icon-dark-mode>
          <div class="theme-label">Dark</div>
        </mdui-card>

        <mdui-card
          :class="['theme-option', { selected: theme === 'auto' }]"
          clickable
          @click="setTheme('auto')"
        >
          <mdui-icon-brightness-auto class="theme-icon"></mdui-icon-brightness-auto>
          <div class="theme-label">Auto</div>
        </mdui-card>
      </div>
    </mdui-card>

    <mdui-card class="settings-card">
      <h3 class="card-title">Connection</h3>
      <mdui-list>
        <mdui-list-item>
          <span slot="headline">Default Protocol</span>
          <mdui-select slot="end-icon" value="spice">
            <mdui-menu-item value="spice">SPICE</mdui-menu-item>
            <mdui-menu-item value="vnc">VNC</mdui-menu-item>
            <mdui-menu-item value="rdp">RDP</mdui-menu-item>
          </mdui-select>
        </mdui-list-item>
        <mdui-list-item>
          <span slot="headline">Auto-reconnect</span>
          <mdui-switch slot="end-icon" checked></mdui-switch>
        </mdui-list-item>
        <mdui-list-item>
          <span slot="headline">Enable USB Redirection</span>
          <mdui-switch slot="end-icon"></mdui-switch>
        </mdui-list-item>
        <mdui-list-item>
          <span slot="headline">Enable Clipboard Sharing</span>
          <mdui-switch slot="end-icon" checked></mdui-switch>
        </mdui-list-item>
      </mdui-list>
    </mdui-card>

    <mdui-card class="settings-card">
      <h3 class="card-title">Display</h3>
      <mdui-list>
        <mdui-list-item>
          <span slot="headline">Resolution</span>
          <mdui-select slot="end-icon" value="auto">
            <mdui-menu-item value="auto">Auto</mdui-menu-item>
            <mdui-menu-item value="1920x1080">1920x1080</mdui-menu-item>
            <mdui-menu-item value="1680x1050">1680x1050</mdui-menu-item>
            <mdui-menu-item value="1280x720">1280x720</mdui-menu-item>
          </mdui-select>
        </mdui-list-item>
        <mdui-list-item>
          <span slot="headline">Scaling Mode</span>
          <mdui-select slot="end-icon" value="fit">
            <mdui-menu-item value="fit">Fit to Window</mdui-menu-item>
            <mdui-menu-item value="stretch">Stretch</mdui-menu-item>
            <mdui-menu-item value="none">No Scaling</mdui-menu-item>
          </mdui-select>
        </mdui-list-item>
      </mdui-list>
    </mdui-card>
  </div>
</template>

<style scoped>
.settings-view {
  max-width: 800px;
  margin: 0 auto;
}

.page-title {
  font-size: 28px;
  font-weight: 600;
  color: var(--zixiao-purple);
  margin-bottom: 24px;
}

.settings-card {
  margin-bottom: 24px;
  padding: 24px;
}

.card-title {
  font-size: 18px;
  font-weight: 600;
  margin-bottom: 16px;
  color: var(--zixiao-purple);
}

.theme-options {
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 16px;
}

.theme-option {
  padding: 24px;
  text-align: center;
  cursor: pointer;
  border: 2px solid transparent;
  transition: all 0.2s;
}

.theme-option:hover {
  border-color: var(--zixiao-purple-light);
}

.theme-option.selected {
  border-color: var(--zixiao-purple);
  background: rgba(139, 92, 246, 0.1);
}

.theme-icon {
  font-size: 40px;
  color: var(--zixiao-purple);
  margin-bottom: 12px;
}

.theme-label {
  font-size: 16px;
  font-weight: 500;
}
</style>
