// Level, a new app: the board's motion sensor (QMI8658) as a spirit level.
// The accelerometer gives the direction of gravity, so the tilt is known in
// every position: 0° upright, 90° on its side, 180° upside down. Shown:
// - left to right: the angle, one decimal, green with "Level" within ±0.3°,
//   and a tube with three lines (the middle one is level) and a bubble that
//   moves with the tilt and stops at the tube's ends
// - front to back: a smaller tube, its ends marked, with its own angle
// - "Set as level": the sensor is never mounted perfectly straight
// The playground can't be tilted: `moving` makes the bubbles wander.
import { useEffect, useState } from 'react'
import { AppScreen, Pill } from '../components'
import { symbol } from '../icons'
import './level.css'

const levelWithin = 0.3  // degrees
const tubeDegrees = 10   // a bubble reaches its tube's end at this tilt

// Signed tilts: negative = the left side / the front is lower
type Tilt = { x: number; y: number }

// The bubble floats to the higher side: the tube's left end is down when tilt < 0
function Tube({ tilt, small = false, ends }: { tilt: number; small?: boolean; ends?: [string, string] }) {
  const along = Math.max(-1, Math.min(1, -tilt / tubeDegrees))
  const level = Math.abs(tilt) < levelWithin
  const reach = small ? 168 : 158  // px from the middle to a bubble's end stop
  const gap = small ? 34 : 46      // px from the middle to an outer line
  return (
    <div className={`lv-tube${small ? ' small' : ''}${level ? ' level' : ''}`}>
      {ends && <span className="lv-end">{ends[0]}</span>}
      <span className="lv-line" style={{ left: `calc(50% - ${gap}px)` }} />
      <span className="lv-line lv-middle" />
      <span className="lv-line" style={{ left: `calc(50% + ${gap}px)` }} />
      <span className="lv-bubble" style={{ left: `calc(50% + ${along * reach}px)` }} />
      {ends && <span className="lv-end right">{ends[1]}</span>}
    </div>
  )
}

export default function Level({ tilt: start = { x: 0, y: 0 }, moving = false }: { tilt?: Tilt; moving?: boolean }) {
  const [raw, setRaw] = useState<Tilt>(start)
  const [offset, setOffset] = useState<Tilt>({ x: 0, y: 0 })
  useEffect(() => {
    if (!moving) return
    const t0 = performance.now()
    const id = setInterval(() => {
      const s = (performance.now() - t0) / 1000
      setRaw({ x: 4 * Math.sin(s * 0.6) + 1.2 * Math.sin(s * 1.7), y: 2.5 * Math.sin(s * 0.9 + 1) })
    }, 50)
    return () => clearInterval(id)
  }, [moving])

  const x = raw.x - offset.x
  const y = raw.y - offset.y
  const level = Math.abs(x) < levelWithin
  const levelY = Math.abs(y) < levelWithin
  const side = level ? 'Level' : x < 0 ? 'Left side lower' : 'Right side lower'
  const sideY = levelY ? 'level' : y < 0 ? 'front lower' : 'back lower'

  return (
    <AppScreen title="Level">
      <div className={`lv${level ? ' level' : ''}`}>
        <div className="lv-angle">{Math.abs(x).toFixed(1)}°</div>
        <div className="lv-side">{side}</div>
        <Tube tilt={x} />
        <div className={`lv-row${levelY ? ' level' : ''}`}>
          <span>Front to back</span>
          <span>{Math.abs(y).toFixed(1)}° {sideY}</span>
        </div>
        <Tube tilt={y} small ends={['Front', 'Back']} />
        <div className="lv-buttons">
          <Pill icon={symbol.ok} label="Set as level" grow onClick={() => setOffset(raw)} />
          <Pill label="Reset" onClick={() => setOffset({ x: 0, y: 0 })} />
        </div>
      </div>
    </AppScreen>
  )
}
