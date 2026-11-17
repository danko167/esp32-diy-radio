// Control centre: swipe down from the top edge (on Home, or in any app) and a
// panel slides down over the screen, as on phones, all the way. A swipe up
// from its bottom bar or the middle button closes it.
// - Four toggles: Wi-Fi, the radio (plays the last station / stops),
//   the upload server (Settings > Display > Your picture) and the screen lock.
//   Without Wi-Fi the radio and the upload server can't go on: dimmed.
// - Brightness and volume
// On the device: a dark panel over a snapshot of the screen, nothing see-through.
import { useRef, useState } from 'react'
import { BigSlider } from '../components'
import { symbol } from '../icons'
import Home from './Home'
import './control-center.css'

const panelHeight = 480  // px: the whole screen
const edge = 40          // a drag starting this close to the top opens it

function Toggle({ icon, label, state, on, disabled, onClick }: {
  icon: string
  label: string
  state: string
  on: boolean
  disabled?: boolean
  onClick: () => void
}) {
  return (
    <button className={`cc-toggle${on ? ' on' : ''}${disabled ? ' disabled' : ''}`}
            onClick={disabled ? undefined : onClick}>
      <span className="cc-circle icon">{icon}</span>
      <span className="cc-text">
        <span className="cc-label">{label}</span>
        <span className="cc-state">{state}</span>
      </span>
    </button>
  )
}

export default function ControlCenter({ start = 'closed', wifiOff = false }: {
  start?: 'open' | 'closed'
  wifiOff?: boolean
}) {
  const [open, setOpen] = useState(start === 'open')
  const [drag, setDrag] = useState<number | null>(null)  // px the panel is pulled down, while dragging
  const from = useRef<{ y: number; open: boolean } | null>(null)

  const [wifi, setWifi] = useState(!wifiOff)
  const [radio, setRadio] = useState(!wifiOff)
  const [upload, setUpload] = useState(false)
  const [lock, setLock] = useState(true)
  const [brightness, setBrightness] = useState(70)
  const [volume, setVolume] = useState(40)

  const shown = drag ?? (open ? panelHeight : 0)
  const setWifiOn = (on: boolean) => {
    setWifi(on)
    if (!on) { setRadio(false); setUpload(false) }  // they need it
  }

  return (
    <div className="cc"
         onPointerDown={(e) => {
           const top = e.currentTarget.getBoundingClientRect().top
           const y = e.clientY - top
           if ((!open && y < edge) || (open && y > panelHeight - 70)) {
             e.currentTarget.setPointerCapture(e.pointerId)
             from.current = { y: e.clientY, open }
           }
         }}
         onPointerMove={(e) => {
           const f = from.current
           if (!f) return
           const base = f.open ? panelHeight : 0
           setDrag(Math.max(0, Math.min(panelHeight, base + e.clientY - f.y)))
         }}
         onPointerUp={() => {
           if (!from.current) return
           from.current = null
           setOpen(shown > panelHeight / 3)
           setDrag(null)
         }}>
      <Home playing={radio} uploading={upload} />
      {shown > 0 && (
        <div className="cc-shade" style={{ opacity: shown / panelHeight }} onClick={() => setOpen(false)} />
      )}
      <div className={`cc-panel${drag === null ? ' settle' : ''}`}
           style={{ height: panelHeight, transform: `translateY(${shown - panelHeight}px)` }}>
        <div className="cc-toggles">
          <Toggle icon={symbol.wifi} label="Wi-Fi" state={wifi ? 'HomeNet' : 'Off'} on={wifi}
                  onClick={() => setWifiOn(!wifi)} />
          <Toggle icon={symbol.audio} label="Radio" state={!wifi ? 'No Wi-Fi' : radio ? 'Radio Paradise' : 'Off'}
                  on={radio} disabled={!wifi} onClick={() => setRadio(!radio)} />
          <Toggle icon={symbol.upload} label="Upload" state={!wifi ? 'No Wi-Fi' : upload ? 'On' : 'Off'}
                  on={upload} disabled={!wifi} onClick={() => setUpload(!upload)} />
          <Toggle icon={symbol.lock} label="Lock" state={lock ? 'On' : 'Off'} on={lock}
                  onClick={() => setLock(!lock)} />
        </div>
        <BigSlider value={brightness} min={2} icon={symbol.sun} onChange={setBrightness} />
        <BigSlider value={volume} icon={volume === 0 ? symbol.mute : symbol.volumeMax} onChange={setVolume} />
        <div className="cc-handle" />
      </div>
      {!open && drag === null && <div className="cc-hint" />}
    </div>
  )
}
