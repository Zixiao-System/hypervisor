import { createApp } from 'vue'
import { createPinia } from 'pinia'
import App from './App.vue'
import router from './router'

// Import MDUI
import 'mdui/mdui.css'
import 'mdui'

// Import Zixiao theme
import './styles/theme.css'
import './styles/global.css'

const app = createApp(App)

app.use(createPinia())
app.use(router)

app.mount('#app')
