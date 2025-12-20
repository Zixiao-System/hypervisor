import { createRouter, createWebHistory } from 'vue-router'
import type { RouteRecordRaw } from 'vue-router'

const routes: RouteRecordRaw[] = [
  {
    path: '/',
    name: 'Dashboard',
    component: () => import('../views/DashboardView.vue'),
    meta: { title: 'Dashboard' }
  },
  {
    path: '/nodes',
    name: 'Nodes',
    component: () => import('../views/NodesView.vue'),
    meta: { title: 'Nodes' }
  },
  {
    path: '/nodes/:id',
    name: 'NodeDetail',
    component: () => import('../views/NodeDetailView.vue'),
    meta: { title: 'Node Detail' }
  },
  {
    path: '/instances',
    name: 'Instances',
    component: () => import('../views/InstancesView.vue'),
    meta: { title: 'Instances' }
  },
  {
    path: '/instances/create',
    name: 'CreateInstance',
    component: () => import('../views/CreateInstanceView.vue'),
    meta: { title: 'Create Instance' }
  },
  {
    path: '/instances/:id',
    name: 'InstanceDetail',
    component: () => import('../views/InstanceDetailView.vue'),
    meta: { title: 'Instance Detail' }
  },
  {
    path: '/instances/:id/console',
    name: 'Console',
    component: () => import('../views/ConsoleView.vue'),
    meta: { title: 'Console' }
  },
  {
    path: '/settings',
    name: 'Settings',
    component: () => import('../views/SettingsView.vue'),
    meta: { title: 'Settings' }
  }
]

const router = createRouter({
  history: createWebHistory(),
  routes
})

// Update page title
router.beforeEach((to, _from, next) => {
  document.title = `${to.meta.title || 'Zixiao'} - Zixiao Hypervisor`
  next()
})

export default router
