import { StrictMode } from 'react'
import { createRoot } from 'react-dom/client'
import './theme.css'
import App from './App.tsx'
import { applyAccent, savedAccent } from './accent'

applyAccent(savedAccent())  // the one picked in Settings > Display

createRoot(document.getElementById('root')!).render(
  <StrictMode>
    <App />
  </StrictMode>,
)
