<script setup lang="ts">
import { useThemeStore } from '../stores/theme'

// Import MDUI icons
import '@mdui/icons/light-mode.js'
import '@mdui/icons/dark-mode.js'
import '@mdui/icons/brightness-auto.js'

const themeStore = useThemeStore()

function setTheme(theme: 'light' | 'dark' | 'auto') {
  themeStore.setTheme(theme)
}
</script>

<template>
  <div class="settings-view">
    <h1 class="section-title">Settings</h1>

    <!-- Theme Settings -->
    <mdui-card class="settings-card">
      <h3 class="card-title">Appearance</h3>
      <div class="theme-options">
        <mdui-card
          :class="['theme-option', { selected: themeStore.theme === 'light' }]"
          clickable
          @click="setTheme('light')"
        >
          <mdui-icon-light-mode class="theme-icon"></mdui-icon-light-mode>
          <div class="theme-label">Light</div>
        </mdui-card>

        <mdui-card
          :class="['theme-option', { selected: themeStore.theme === 'dark' }]"
          clickable
          @click="setTheme('dark')"
        >
          <mdui-icon-dark-mode class="theme-icon"></mdui-icon-dark-mode>
          <div class="theme-label">Dark</div>
        </mdui-card>

        <mdui-card
          :class="['theme-option', { selected: themeStore.theme === 'auto' }]"
          clickable
          @click="setTheme('auto')"
        >
          <mdui-icon-brightness-auto class="theme-icon"></mdui-icon-brightness-auto>
          <div class="theme-label">Auto</div>
        </mdui-card>
      </div>
    </mdui-card>

    <!-- About -->
    <mdui-card class="settings-card">
      <h3 class="card-title">About</h3>
      <mdui-list>
        <mdui-list-item>
          <span slot="headline">Application</span>
          <span slot="description">Zixiao Hypervisor Control Plane</span>
        </mdui-list-item>
        <mdui-list-item>
          <span slot="headline">Version</span>
          <span slot="description">0.1.0</span>
        </mdui-list-item>
        <mdui-list-item>
          <span slot="headline">UI Framework</span>
          <span slot="description">Vue 3 + MDUI 2</span>
        </mdui-list-item>
        <mdui-list-item>
          <span slot="headline">Theme</span>
          <span slot="description">ZiXiao System VI</span>
        </mdui-list-item>
      </mdui-list>
    </mdui-card>

    <!-- Color Palette Preview -->
    <mdui-card class="settings-card">
      <h3 class="card-title">Color Palette</h3>
      <div class="color-palette">
        <div class="color-item">
          <div class="color-swatch" style="background: var(--zixiao-purple);"></div>
          <div class="color-info">
            <div class="color-name">Zixiao Purple</div>
            <div class="color-hex">#8B5CF6</div>
          </div>
        </div>
        <div class="color-item">
          <div class="color-swatch" style="background: var(--lightning-gold);"></div>
          <div class="color-info">
            <div class="color-name">Lightning Gold</div>
            <div class="color-hex">#FCD34D</div>
          </div>
        </div>
        <div class="color-item">
          <div class="color-swatch" style="background: var(--deep-space-black);"></div>
          <div class="color-info">
            <div class="color-name">Deep Space Black</div>
            <div class="color-hex">#0F172A</div>
          </div>
        </div>
        <div class="color-item">
          <div class="color-swatch" style="background: var(--cloud-white);"></div>
          <div class="color-info">
            <div class="color-name">Cloud White</div>
            <div class="color-hex">#F8FAFC</div>
          </div>
        </div>
      </div>
    </mdui-card>
  </div>
</template>

<style scoped>
.settings-view {
  max-width: 800px;
  margin: 0 auto;
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

.color-palette {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(180px, 1fr));
  gap: 16px;
}

.color-item {
  display: flex;
  align-items: center;
  gap: 12px;
}

.color-swatch {
  width: 48px;
  height: 48px;
  border-radius: 8px;
  border: 1px solid rgba(0, 0, 0, 0.1);
}

.color-name {
  font-size: 14px;
  font-weight: 500;
}

.color-hex {
  font-size: 12px;
  font-family: monospace;
  opacity: 0.7;
}
</style>
