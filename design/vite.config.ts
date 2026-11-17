import react from '@vitejs/plugin-react'
import { defineConfig } from 'vite'

export default defineConfig({
  plugins: [react()],
  // The fonts live in ../tools/fonts, shared with the firmware's font generator
  server: { fs: { allow: ['..'] } },
})
