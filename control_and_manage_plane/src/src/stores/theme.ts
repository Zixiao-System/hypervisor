import { defineStore } from 'pinia'
import { ref, computed } from 'vue'

export const useThemeStore = defineStore('theme', () => {
  const theme = ref<'light' | 'dark' | 'auto'>('auto')

  const isDark = computed(() => {
    if (theme.value === 'auto') {
      return window.matchMedia('(prefers-color-scheme: dark)').matches
    }
    return theme.value === 'dark'
  })

  function initTheme() {
    const saved = localStorage.getItem('zixiao-theme') as 'light' | 'dark' | 'auto' | null
    if (saved) {
      theme.value = saved
    }
    applyTheme()

    // Listen for system theme changes
    window.matchMedia('(prefers-color-scheme: dark)').addEventListener('change', () => {
      if (theme.value === 'auto') {
        applyTheme()
      }
    })
  }

  function setTheme(newTheme: 'light' | 'dark' | 'auto') {
    theme.value = newTheme
    localStorage.setItem('zixiao-theme', newTheme)
    applyTheme()
  }

  function toggleTheme() {
    if (isDark.value) {
      setTheme('light')
    } else {
      setTheme('dark')
    }
  }

  function applyTheme() {
    const body = document.body
    body.classList.remove('mdui-theme-light', 'mdui-theme-dark', 'mdui-theme-auto')

    if (theme.value === 'auto') {
      body.classList.add('mdui-theme-auto')
    } else if (theme.value === 'dark') {
      body.classList.add('mdui-theme-dark')
    } else {
      body.classList.add('mdui-theme-light')
    }
  }

  return {
    theme,
    isDark,
    initTheme,
    setTheme,
    toggleTheme
  }
})
