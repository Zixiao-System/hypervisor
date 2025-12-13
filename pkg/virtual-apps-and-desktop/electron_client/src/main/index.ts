import { app, BrowserWindow, ipcMain, Menu, Tray, nativeImage } from 'electron'
import { join } from 'path'
import Store from 'electron-store'

// Initialize store for persistent settings
const store = new Store({
  defaults: {
    theme: 'auto',
    bookmarks: [],
    recentConnections: []
  }
})

let mainWindow: BrowserWindow | null = null
let tray: Tray | null = null

const isDev = process.env.NODE_ENV === 'development'

function createWindow() {
  mainWindow = new BrowserWindow({
    width: 1200,
    height: 800,
    minWidth: 800,
    minHeight: 600,
    title: 'Zixiao Desktop',
    icon: join(__dirname, '../resources/icon.png'),
    webPreferences: {
      preload: join(__dirname, 'preload.js'),
      nodeIntegration: false,
      contextIsolation: true,
      sandbox: true
    },
    titleBarStyle: process.platform === 'darwin' ? 'hiddenInset' : 'default',
    backgroundColor: '#F5FAD8' // Zixiao light background color
  })

  // Load the app
  if (isDev) {
    mainWindow.loadURL('http://localhost:5173')
    mainWindow.webContents.openDevTools()
  } else {
    mainWindow.loadFile(join(__dirname, '../dist/index.html'))
  }

  // Handle window close
  mainWindow.on('close', (event) => {
    if (tray) {
      event.preventDefault()
      mainWindow?.hide()
    }
  })

  mainWindow.on('closed', () => {
    mainWindow = null
  })
}

function createTray() {
  const icon = nativeImage.createFromPath(join(__dirname, '../resources/icon.png'))
  tray = new Tray(icon.resize({ width: 16, height: 16 }))

  const contextMenu = Menu.buildFromTemplate([
    { label: 'Open Zixiao Desktop', click: () => mainWindow?.show() },
    { type: 'separator' },
    { label: 'Quit', click: () => {
      tray = null
      app.quit()
    }}
  ])

  tray.setToolTip('Zixiao Desktop')
  tray.setContextMenu(contextMenu)

  tray.on('click', () => {
    mainWindow?.show()
  })
}

// IPC Handlers
ipcMain.handle('get-settings', () => {
  return {
    theme: store.get('theme'),
    bookmarks: store.get('bookmarks'),
    recentConnections: store.get('recentConnections')
  }
})

ipcMain.handle('set-theme', (_event, theme: string) => {
  store.set('theme', theme)
  return true
})

ipcMain.handle('add-bookmark', (_event, bookmark: any) => {
  const bookmarks = store.get('bookmarks') as any[]
  bookmarks.push(bookmark)
  store.set('bookmarks', bookmarks)
  return bookmarks
})

ipcMain.handle('remove-bookmark', (_event, id: string) => {
  const bookmarks = (store.get('bookmarks') as any[]).filter(b => b.id !== id)
  store.set('bookmarks', bookmarks)
  return bookmarks
})

ipcMain.handle('add-recent-connection', (_event, connection: any) => {
  let recent = store.get('recentConnections') as any[]
  recent = recent.filter(c => c.id !== connection.id)
  recent.unshift(connection)
  if (recent.length > 10) recent = recent.slice(0, 10)
  store.set('recentConnections', recent)
  return recent
})

ipcMain.handle('open-fullscreen-desktop', (_event, desktopId: string) => {
  // Create a new fullscreen window for the desktop
  const desktopWindow = new BrowserWindow({
    fullscreen: true,
    webPreferences: {
      preload: join(__dirname, 'preload.js'),
      nodeIntegration: false,
      contextIsolation: true
    }
  })

  if (isDev) {
    desktopWindow.loadURL(`http://localhost:5173/#/desktop/${desktopId}`)
  } else {
    desktopWindow.loadFile(join(__dirname, '../dist/index.html'), {
      hash: `/desktop/${desktopId}`
    })
  }

  return true
})

// App lifecycle
app.whenReady().then(() => {
  createWindow()
  createTray()

  app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) {
      createWindow()
    } else {
      mainWindow?.show()
    }
  })
})

app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') {
    app.quit()
  }
})
