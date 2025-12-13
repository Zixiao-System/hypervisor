<script setup lang="ts">
import { ref } from 'vue'
import { useRouter, useRoute } from 'vue-router'

// Import MDUI icons
import '@mdui/icons/computer.js'
import '@mdui/icons/settings.js'
import '@mdui/icons/light-mode.js'
import '@mdui/icons/dark-mode.js'
import '@mdui/icons/menu.js'

const router = useRouter()
const route = useRoute()
const drawerOpen = ref(false)
const theme = ref<'light' | 'dark' | 'auto'>('auto')

// Hide navigation when in desktop view
const showNav = ref(true)

router.beforeEach((to) => {
  showNav.value = to.name !== 'Desktop'
})

function navigate(path: string) {
  router.push(path)
  drawerOpen.value = false
}

function toggleTheme() {
  const themes: ('light' | 'dark' | 'auto')[] = ['light', 'dark', 'auto']
  const currentIndex = themes.indexOf(theme.value)
  theme.value = themes[(currentIndex + 1) % themes.length]
  applyTheme()
}

function applyTheme() {
  const body = document.body
  body.classList.remove('mdui-theme-light', 'mdui-theme-dark', 'mdui-theme-auto')
  body.classList.add(`mdui-theme-${theme.value}`)
}
</script>

<template>
  <mdui-layout class="app-layout">
    <!-- Top App Bar (hidden in desktop view) -->
    <mdui-top-app-bar v-if="showNav">
      <mdui-button-icon @click="drawerOpen = !drawerOpen">
        <mdui-icon-menu></mdui-icon-menu>
      </mdui-button-icon>
      <mdui-top-app-bar-title>Zixiao Web Access</mdui-top-app-bar-title>
      <div style="flex-grow: 1"></div>
      <mdui-button-icon @click="toggleTheme">
        <mdui-icon-light-mode v-if="theme === 'dark'"></mdui-icon-light-mode>
        <mdui-icon-dark-mode v-else></mdui-icon-dark-mode>
      </mdui-button-icon>
    </mdui-top-app-bar>

    <!-- Navigation Drawer -->
    <mdui-navigation-drawer v-if="showNav" :open="drawerOpen" @close="drawerOpen = false">
      <mdui-list>
        <mdui-list-item :active="route.path === '/'" @click="navigate('/')">
          <mdui-icon-computer slot="icon"></mdui-icon-computer>
          Desktops
        </mdui-list-item>
        <mdui-list-item :active="route.path === '/settings'" @click="navigate('/settings')">
          <mdui-icon-settings slot="icon"></mdui-icon-settings>
          Settings
        </mdui-list-item>
      </mdui-list>
    </mdui-navigation-drawer>

    <!-- Main Content -->
    <mdui-layout-main :class="{ 'no-padding': !showNav }">
      <router-view />
    </mdui-layout-main>
  </mdui-layout>
</template>

<style scoped>
.app-layout {
  min-height: 100vh;
}

mdui-layout-main {
  padding: 24px;
}

mdui-layout-main.no-padding {
  padding: 0;
}

mdui-top-app-bar-title {
  font-weight: 600;
  color: var(--zixiao-purple);
}
</style>
