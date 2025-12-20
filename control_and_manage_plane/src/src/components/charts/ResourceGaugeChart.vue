<script setup lang="ts">
import { computed } from 'vue'
import { Doughnut } from 'vue-chartjs'
import { Chart as ChartJS, ArcElement, Tooltip, Legend } from 'chart.js'

ChartJS.register(ArcElement, Tooltip, Legend)

const props = defineProps<{
  label: string
  used: number
  total: number
  color?: string
  unit?: string
}>()

const percentage = computed(() => {
  if (props.total === 0) return 0
  return Math.round((props.used / props.total) * 100)
})

const chartData = computed(() => ({
  labels: ['Used', 'Available'],
  datasets: [{
    data: [props.used, Math.max(0, props.total - props.used)],
    backgroundColor: [props.color || '#8B5CF6', '#E5E7EB'],
    borderWidth: 0,
    cutout: '75%',
  }]
}))

const chartOptions = {
  responsive: true,
  maintainAspectRatio: false,
  plugins: {
    legend: { display: false },
    tooltip: {
      enabled: true,
      callbacks: {
        label: (context: { label: string; raw: number }) => {
          const value = context.raw
          if (props.unit === 'bytes') {
            return `${context.label}: ${formatBytes(value)}`
          }
          return `${context.label}: ${value}`
        }
      }
    }
  }
}

function formatBytes(bytes: number): string {
  if (bytes === 0) return '0 B'
  const k = 1024
  const sizes = ['B', 'KB', 'MB', 'GB', 'TB']
  const i = Math.floor(Math.log(bytes) / Math.log(k))
  return parseFloat((bytes / Math.pow(k, i)).toFixed(1)) + ' ' + sizes[i]
}

function formatValue(value: number): string {
  if (props.unit === 'bytes') {
    return formatBytes(value)
  }
  return value.toString()
}
</script>

<template>
  <div class="gauge-chart">
    <div class="chart-container">
      <Doughnut :data="chartData" :options="chartOptions" />
      <div class="center-text">
        <span class="percentage">{{ percentage }}%</span>
        <span class="label">{{ label }}</span>
      </div>
    </div>
    <div class="chart-legend">
      <div class="legend-item">
        <span class="legend-dot" :style="{ backgroundColor: color || '#8B5CF6' }"></span>
        <span class="legend-label">Used: {{ formatValue(used) }}</span>
      </div>
      <div class="legend-item">
        <span class="legend-dot" style="background-color: #E5E7EB"></span>
        <span class="legend-label">Total: {{ formatValue(total) }}</span>
      </div>
    </div>
  </div>
</template>

<style scoped>
.gauge-chart {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 12px;
}

.chart-container {
  position: relative;
  width: 120px;
  height: 120px;
}

.center-text {
  position: absolute;
  top: 50%;
  left: 50%;
  transform: translate(-50%, -50%);
  text-align: center;
  display: flex;
  flex-direction: column;
  gap: 2px;
}

.percentage {
  font-size: 24px;
  font-weight: 600;
  color: var(--mdui-color-on-surface);
}

.label {
  font-size: 12px;
  color: var(--mdui-color-on-surface-variant);
}

.chart-legend {
  display: flex;
  flex-direction: column;
  gap: 4px;
}

.legend-item {
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: 12px;
}

.legend-dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
}

.legend-label {
  color: var(--mdui-color-on-surface-variant);
}
</style>
