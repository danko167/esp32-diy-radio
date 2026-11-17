// The clock screen with the screen lock on (Settings > Clock screen > Lock):
// a tap doesn't open Home, it shows "slide to unlock" at the bottom, as older
// iPhones had it. Pulling the knob to the end unlocks; let go before, and it
// springs back; left alone, the control goes after 5 s. It stays at the
// clock screen's brightness. The buttons and a ringing alarm don't need it.
import { useEffect, useRef, useState } from 'react'
import { symbol } from '../icons'
import ClockFace from './ClockFaces'
import './lock-screen.css'

const track = 400  // px
const knob = 58
const travel = track - 2 * 6 - knob  // inside the track's 6 px padding
const hideAfterMs = 5000

export default function LockScreen({ start = 'hidden' }: { start?: 'hidden' | 'shown' }) {
  const [shown, setShown] = useState(start === 'shown')
  const [x, setX] = useState(0)
  const drag = useRef<{ from: number; x: number } | null>(null)
  const [dragging, setDragging] = useState(false)
  const hideTimer = useRef(0)

  // Gone 5 s after the last touch
  const hideLater = () => {
    window.clearTimeout(hideTimer.current)
    hideTimer.current = window.setTimeout(() => { setShown(false); setX(0) }, hideAfterMs)
  }
  // Shown again by each tap
  const show = () => {
    setShown(true)
    hideLater()
  }
  useEffect(() => {
    if (start === 'shown') hideLater()
    return () => window.clearTimeout(hideTimer.current)
  }, [start])

  const release = () => {
    if (!drag.current) return
    drag.current = null
    setDragging(false)
    if (x >= travel * 0.9) {
      location.hash = 'Home / First tiles'  // unlocked: Home, at full brightness
    } else {
      setX(0)  // springs back
      show()
    }
  }

  return (
    <div className="lock" onPointerDown={() => show()}>
      <ClockFace face="digital" />
      {shown && (
        <div className="lock-track"
             onPointerMove={(e) => {
               const d = drag.current
               if (d) setX(Math.max(0, Math.min(travel, d.x + e.clientX - d.from)))
             }}
             onPointerUp={release} onPointerCancel={release}>
          <span className="lock-text" style={{ opacity: 1 - x / travel }}>slide to unlock</span>
          <span className={`lock-knob icon${dragging ? '' : ' spring'}`} style={{ transform: `translateX(${x}px)` }}
                onPointerDown={(e) => {
                  e.currentTarget.parentElement!.setPointerCapture(e.pointerId)
                  drag.current = { from: e.clientX, x }
                  setDragging(true)
                  window.clearTimeout(hideTimer.current)
                }}>
            {symbol.right}
          </span>
        </div>
      )}
    </div>
  )
}
