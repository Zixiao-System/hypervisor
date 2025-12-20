<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import TerminalConsole from '../components/TerminalConsole.vue'

const route = useRoute()
const router = useRouter()

const instanceId = ref(route.params.id as string)
const consoleRef = ref<InstanceType<typeof TerminalConsole> | null>(null)

function handleClose() {
  router.push(`/instances/${instanceId.value}`)
}

function handleError(message: string) {
  console.error('Console error:', message)
}

onMounted(() => {
  consoleRef.value?.connect()
})
</script>

<template>
  <div class="console-view">
    <TerminalConsole
      ref="consoleRef"
      :instanceId="instanceId"
      :visible="true"
      @close="handleClose"
      @error="handleError"
    />
  </div>
</template>

<style scoped>
.console-view {
  position: fixed;
  top: 0;
  left: 0;
  right: 0;
  bottom: 0;
  z-index: 1000;
  background: #1e1e2e;
}
</style>
