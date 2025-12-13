<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { useRouter } from 'vue-router'
import { useThemeStore } from './stores/theme'

// Import MDUI icons
import '@mdui/icons/dashboard.js'
import '@mdui/icons/dns.js'
import '@mdui/icons/memory.js'
import '@mdui/icons/settings.js'
import '@mdui/icons/light-mode.js'
import '@mdui/icons/dark-mode.js'
import '@mdui/icons/menu.js'

const router = useRouter()
const themeStore = useThemeStore()
const drawerOpen = ref(false)

const navItems = [
  { icon: 'dashboard', label: 'Dashboard', path: '/' },
  { icon: 'dns', label: 'Nodes', path: '/nodes' },
  { icon: 'memory', label: 'Instances', path: '/instances' },
  { icon: 'settings', label: 'Settings', path: '/settings' }
]

function navigate(path: string) {
  router.push(path)
  drawerOpen.value = false
}

function toggleTheme() {
  themeStore.toggleTheme()
}

onMounted(() => {
  themeStore.initTheme()
})
</script>

<template>
  <mdui-layout>
    <!-- Top App Bar -->
    <mdui-top-app-bar>
      <mdui-button-icon @click="drawerOpen = !drawerOpen">
        <mdui-icon-menu></mdui-icon-menu>
      </mdui-button-icon>
      <mdui-top-app-bar-title>Zixiao Hypervisor</mdui-top-app-bar-title>
      <div style="flex-grow: 1"></div>
      <mdui-button-icon @click="toggleTheme">
        <mdui-icon-light-mode v-if="themeStore.isDark"></mdui-icon-light-mode>
        <mdui-icon-dark-mode v-else></mdui-icon-dark-mode>
      </mdui-button-icon>
    </mdui-top-app-bar>

    <!-- Navigation Drawer -->
    <mdui-navigation-drawer :open="drawerOpen" @close="drawerOpen = false">
      <mdui-list>
        <mdui-list-item
          v-for="item in navItems"
          :key="item.path"
          :active="$route.path === item.path"
          @click="navigate(item.path)"
        >
          <mdui-icon :name="item.icon" slot="icon"></mdui-icon>
          {{ item.label }}
        </mdui-list-item>
      </mdui-list>
    </mdui-navigation-drawer>

    <!-- Main Content -->
    <mdui-layout-main>
      <router-view />
    </mdui-layout-main>
  </mdui-layout>
</template>

<style scoped>
mdui-layout {
  min-height: 100vh;
}

mdui-layout-main {
  padding: 24px;
}

mdui-top-app-bar-title {
  font-weight: 600;
  color: var(--zixiao-purple);
}
</style>
