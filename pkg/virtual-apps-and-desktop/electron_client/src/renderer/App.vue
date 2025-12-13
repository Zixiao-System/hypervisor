<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { useRouter } from 'vue-router'

// Import MDUI icons
import '@mdui/icons/computer.js'
import '@mdui/icons/settings.js'
import '@mdui/icons/info.js'
import '@mdui/icons/light-mode.js'
import '@mdui/icons/dark-mode.js'

const router = useRouter()
const theme = ref<'light' | 'dark' | 'auto'>('auto')

const navItems = [
  { icon: 'computer', label: 'Desktops', path: '/' },
  { icon: 'settings', label: 'Settings', path: '/settings' },
  { icon: 'info', label: 'About', path: '/about' }
]

function navigate(path: string) {
  router.push(path)
}

async function toggleTheme() {
  const themes: ('light' | 'dark' | 'auto')[] = ['light', 'dark', 'auto']
  const currentIndex = themes.indexOf(theme.value)
  theme.value = themes[(currentIndex + 1) % themes.length]

  if (window.electronAPI) {
    await window.electronAPI.setTheme(theme.value)
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
    applyTheme()
  }
})
</script>

<template>
  <mdui-layout class="app-layout">
    <!-- Navigation Rail -->
    <mdui-navigation-rail>
      <mdui-navigation-rail-item
        v-for="item in navItems"
        :key="item.path"
        :active="$route.path === item.path"
        @click="navigate(item.path)"
      >
        <mdui-icon :name="item.icon" slot="icon"></mdui-icon>
        {{ item.label }}
      </mdui-navigation-rail-item>

      <div style="flex-grow: 1"></div>

      <mdui-button-icon @click="toggleTheme" style="margin-bottom: 16px;">
        <mdui-icon-light-mode v-if="theme === 'dark'"></mdui-icon-light-mode>
        <mdui-icon-dark-mode v-else></mdui-icon-dark-mode>
      </mdui-button-icon>
    </mdui-navigation-rail>

    <!-- Main Content -->
    <mdui-layout-main class="main-content">
      <router-view />
    </mdui-layout-main>
  </mdui-layout>
</template>

<style scoped>
.app-layout {
  height: 100vh;
}

.main-content {
  padding: 24px;
  overflow-y: auto;
}

mdui-navigation-rail {
  --mdui-color-surface-container: var(--zixiao-purple);
}
</style>
