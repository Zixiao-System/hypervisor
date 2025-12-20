<script setup lang="ts">
import { computed } from 'vue'
import { Line } from 'vue-chartjs'
import {
  Chart as ChartJS,
  CategoryScale,
  LinearScale,
  PointElement,
  LineElement,
  Title,
  Tooltip,
  Legend,
  Filler
} from 'chart.js'

ChartJS.register(
  CategoryScale,
  LinearScale,
  PointElement,
  LineElement,
  Title,
  Tooltip,
  Legend,
  Filler
)

const props = defineProps<{
  title: string
  dataPoints: number[]
  labels: string[]
  color: string
  unit?: string
  maxValue?: number
}>()

const chartData = computed(() => ({
  labels: props.labels,
  datasets: [{
    label: props.title,
    data: props.dataPoints,
    borderColor: props.color,
    backgroundColor: `${props.color}20`,
    fill: true,
    tension: 0.4,
    pointRadius: 0,
    pointHoverRadius: 4,
    borderWidth: 2,
  }]
}))

const chartOptions = computed(() => ({
  responsive: true,
  maintainAspectRatio: false,
  plugins: {
    legend: { display: false },
    title: {
      display: true,
      text: props.title,
      color: 'var(--mdui-color-on-surface)',
      font: { size: 14, weight: '500' },
      padding: { bottom: 16 }
    },
    tooltip: {
      callbacks: {
        label: (context: { raw: number }) => {
          return `${context.raw}${props.unit || '%'}`
        }
      }
    }
  },
  scales: {
    x: {
      display: true,
      grid: { display: false },
      ticks: {
        color: 'var(--mdui-color-on-surface-variant)',
        maxRotation: 0,
        maxTicksLimit: 6
      }
    },
    y: {
      display: true,
      beginAtZero: true,
      max: props.maxValue || 100,
      grid: {
        color: 'var(--mdui-color-outline-variant)',
        drawBorder: false
      },
      ticks: {
        color: 'var(--mdui-color-on-surface-variant)',
        callback: (value: number) => `${value}${props.unit || '%'}`
      }
    }
  },
  interaction: {
    intersect: false,
    mode: 'index' as const
  }
}))
</script>

<template>
  <div class="trend-chart">
    <Line :data="chartData" :options="chartOptions" />
  </div>
</template>

<style scoped>
.trend-chart {
  width: 100%;
  height: 200px;
}
</style>
