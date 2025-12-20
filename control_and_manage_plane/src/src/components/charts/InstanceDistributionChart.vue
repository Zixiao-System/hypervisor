<script setup lang="ts">
import { computed } from 'vue'
import { Pie } from 'vue-chartjs'
import { Chart as ChartJS, ArcElement, Tooltip, Legend } from 'chart.js'

ChartJS.register(ArcElement, Tooltip, Legend)

const props = defineProps<{
  vmCount: number
  containerCount: number
  microvmCount: number
}>()

const total = computed(() => props.vmCount + props.containerCount + props.microvmCount)

const chartData = computed(() => ({
  labels: ['VMs', 'Containers', 'MicroVMs'],
  datasets: [{
    data: [props.vmCount, props.containerCount, props.microvmCount],
    backgroundColor: ['#8B5CF6', '#10B981', '#F59E0B'],
    borderWidth: 2,
    borderColor: 'var(--mdui-color-surface)'
  }]
}))

const chartOptions = {
  responsive: true,
  maintainAspectRatio: false,
  plugins: {
    legend: {
      display: true,
      position: 'bottom' as const,
      labels: {
        usePointStyle: true,
        pointStyle: 'circle',
        padding: 16,
        color: 'var(--mdui-color-on-surface)'
      }
    },
    tooltip: {
      callbacks: {
        label: (context: { label: string; raw: number }) => {
          const percentage = total.value > 0
            ? Math.round((context.raw / total.value) * 100)
            : 0
          return `${context.label}: ${context.raw} (${percentage}%)`
        }
      }
    }
  }
}
</script>

<template>
  <div class="distribution-chart">
    <div class="chart-header">
      <span class="chart-title">Instance Distribution</span>
      <span class="total-count">Total: {{ total }}</span>
    </div>
    <div class="chart-container">
      <Pie v-if="total > 0" :data="chartData" :options="chartOptions" />
      <div v-else class="empty-state">
        <span>No instances</span>
      </div>
    </div>
  </div>
</template>

<style scoped>
.distribution-chart {
  display: flex;
  flex-direction: column;
  gap: 16px;
}

.chart-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.chart-title {
  font-size: 14px;
  font-weight: 500;
  color: var(--mdui-color-on-surface);
}

.total-count {
  font-size: 12px;
  color: var(--mdui-color-on-surface-variant);
}

.chart-container {
  height: 200px;
  display: flex;
  align-items: center;
  justify-content: center;
}

.empty-state {
  color: var(--mdui-color-on-surface-variant);
  font-size: 14px;
}
</style>
