// Draws a face file (src/faceFile.ts), as the radio's face renderer will:
// a background, then each element where the file puts it. Same mockup moment
// as ClockFaces.tsx: Sunday 1 November, 17:42, next alarm 07:30, 12° and
// partly cloudy. The editor passes `selected` and `onPointerDown` to move
// elements around.
import type { PointerEvent } from 'react'
import { facePictures } from '../faceFile'
import type { ColourRef, FaceElement, FaceFile, FontRef } from '../faceFile'
import { symbol, weather } from '../icons'
import './custom-face.css'

const hour = 17
const minute = 42
const two = (n: number) => String(n).padStart(2, '0')

const fontCss = (f: FontRef) => {
  if (typeof f !== 'string') return { fontFamily: `'${f.file}', Montserrat`, fontSize: f.size }
  const size = { clock96: 96, clock140: 140, ui20: 20, ui22: 22, ui30: 30, ui50: 50 }[f]
  return { fontSize: size, lineHeight: f.startsWith('clock') ? 0.85 : 1.25 }
}

const colourCss = (c: ColourRef) =>
  c === 'accent' ? 'var(--accent)' : c === 'text' ? 'var(--text)' : c === 'dim' ? 'var(--text-dim)' : c

const dateText = (format: string) =>
  ({ long: 'Sunday, 1 November', short: 'Sun 1 Nov', day: 'Sunday', date: '1 November', numeric: '1. 11.' })[format]

function content(e: FaceElement) {
  switch (e.kind) {
    case 'time': return <>{hour}:{two(minute)}{e.seconds && <span className="cfx-seconds">05</span>}</>
    case 'date': return dateText(e.format)
    case 'alarm': return <><span className="icon">{symbol.bell}</span> 07:30</>
    case 'weather': return <><span className="weather-icon">{weather.partlyCloudyDay}</span> 12°{e.words && '  Partly cloudy'}</>
    case 'text': return e.text
    default: return null
  }
}

function Analog({ e }: { e: Extract<FaceElement, { kind: 'analog' }> }) {
  const point = (angle: number, length: number) => {
    const a = ((angle - 90) * Math.PI) / 180
    return { x: e.cx + length * Math.cos(a), y: e.cy + length * Math.sin(a) }
  }
  const line = (key: string, angle: number, from: number, to: number, width: number, colour: ColourRef) => {
    const a = point(angle, from)
    const b = point(angle, to)
    return <line key={key} x1={a.x} y1={a.y} x2={b.x} y2={b.y} stroke={colourCss(colour)} strokeWidth={width}
                 strokeLinecap="round" />
  }
  const count = { none: 0, quarters: 4, hours: 12, minutes: 60 }[e.ticks]
  return (
    <svg className="cfx-analog" width={480} height={480}>
      {Array.from({ length: count }, (_, i) => {
        const angle = (i * 360) / count
        const major = angle % 90 === 0
        return line(`t${i}`, angle, e.radius - (major ? 26 : count === 60 ? 8 : 16), e.radius, major ? 6 : 3, e.tickColour)
      })}
      {line('h', ((hour % 12) + minute / 60) * 30, 0, e.hour.length, e.hour.width, e.hour.colour)}
      {line('m', minute * 6, 0, e.minute.length, e.minute.width, e.minute.colour)}
      {e.second && line('s', 30, -20, e.second.length, e.second.width, e.second.colour)}
      {e.dot && <circle cx={e.cx} cy={e.cy} r={8} fill={colourCss(e.dot)} />}
    </svg>
  )
}

export default function CustomFace({ file, selected, onPointerDown }: {
  file: FaceFile
  selected?: number
  onPointerDown?: (index: number, e: PointerEvent) => void
}) {
  const bg = 'picture' in file.background
    ? `url("${facePictures[file.background.picture]}") center / cover`
    : file.background.colour
  return (
    <div className="cfx" style={{ background: bg }}>
      {file.elements.map((e, i) => {
        const pick = onPointerDown && ((ev: PointerEvent) => onPointerDown(i, ev))
        if (e.kind === 'analog') {
          return (
            <div key={i} className={`cfx-item cfx-dial${i === selected ? ' selected' : ''}`} onPointerDown={pick}
                 style={{ left: e.cx - e.radius - 6, top: e.cy - e.radius - 6, width: 2 * e.radius + 12, height: 2 * e.radius + 12 }}>
              <div style={{ position: 'absolute', left: -(e.cx - e.radius - 6), top: -(e.cy - e.radius - 6) }}>
                <Analog e={e} />
              </div>
            </div>
          )
        }
        const shift = { left: '0', centre: '-50%', right: '-100%' }[e.align]
        return (
          <div key={i} className={`cfx-item${i === selected ? ' selected' : ''}`} onPointerDown={pick}
               style={{ left: e.x, top: e.y, transform: `translateX(${shift})`, color: colourCss(e.colour), ...fontCss(e.font) }}>
            {content(e)}
          </div>
        )
      })}
    </div>
  )
}
