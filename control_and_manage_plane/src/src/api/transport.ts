import { createGrpcWebTransport } from '@connectrpc/connect-web'

export const transport = createGrpcWebTransport({
  baseUrl: import.meta.env.VITE_API_URL || 'http://localhost:8080',
})