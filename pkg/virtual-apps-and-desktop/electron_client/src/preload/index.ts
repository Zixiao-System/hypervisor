import { contextBridge, ipcRenderer } from 'electron'

// Expose protected methods that allow the renderer process to use
// the ipcRenderer without exposing the entire object
contextBridge.exposeInMainWorld('electronAPI', {
  // Settings
  getSettings: () => ipcRenderer.invoke('get-settings'),
  setTheme: (theme: string) => ipcRenderer.invoke('set-theme', theme),

  // Bookmarks
  addBookmark: (bookmark: any) => ipcRenderer.invoke('add-bookmark', bookmark),
  removeBookmark: (id: string) => ipcRenderer.invoke('remove-bookmark', id),

  // Recent connections
  addRecentConnection: (connection: any) => ipcRenderer.invoke('add-recent-connection', connection),

  // Desktop operations
  openFullscreenDesktop: (desktopId: string) => ipcRenderer.invoke('open-fullscreen-desktop', desktopId),

  // Platform info
  platform: process.platform
})

// Type definitions for the exposed API
declare global {
  interface Window {
    electronAPI: {
      getSettings: () => Promise<{
        theme: string
        bookmarks: any[]
        recentConnections: any[]
      }>
      setTheme: (theme: string) => Promise<boolean>
      addBookmark: (bookmark: any) => Promise<any[]>
      removeBookmark: (id: string) => Promise<any[]>
      addRecentConnection: (connection: any) => Promise<any[]>
      openFullscreenDesktop: (desktopId: string) => Promise<boolean>
      platform: string
    }
  }
}
