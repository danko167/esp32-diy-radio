// Building blocks that match the firmware's helpers in src/ui/common.cpp,
// so a design made from them maps straight onto existing code.
import { useRef } from 'react'
import type { ReactNode } from 'react'
import './components.css'
import { symbol } from './icons'

// createAppScreen(): back button, title, scrollable body below a 72 px header,
// an optional `caption` above the title (setAppTitle), `action` on the right
// of the header (addHeaderButton), `footer` pinned below the body (addFooter).
export function AppScreen({ title, caption, action, footer, onBack, children }: {
  title: string
  caption?: string
  action?: ReactNode
  footer?: ReactNode
  onBack?: () => void
  children: ReactNode
}) {
  return (
    <div className="app-screen">
      <header>
        <button className="back icon" onClick={onBack}>{symbol.left}</button>
        <div className="titles">
          {caption && <div className="caption">{caption}</div>}
          <h2>{title}</h2>
        </div>
        {action}
      </header>
      <div className="body">{children}</div>
      {footer && <footer>{footer}</footer>}
    </div>
  )
}

// addSection(): small accent caption above a group of cards
export function Section({ children }: { children: ReactNode }) {
  return <div className="section">{children}</div>
}

// A section caption with something small on its right, such as a value or a refresh icon
export function SectionRow({ title, children }: { title: string; children: ReactNode }) {
  return (
    <div className="section-row">
      <Section>{title}</Section>
      {children}
    </div>
  )
}

// addHint(): small dim wrapped text under a group; `big` for a page's own explanation
export function Hint({ big, children }: { big?: boolean; children: ReactNode }) {
  return <div className={big ? 'hint big' : 'hint'}>{children}</div>
}

// addPill(): a rounded button with a label and maybe an icon. Grey, or accent
// with black text if `primary`; `on` colours the text instead. `grow` shares
// a row with others, `wide` takes the whole width.
export function Pill({ icon, label, primary, on, size = 'normal', grow, wide, onClick }: {
  icon?: string
  label?: string
  primary?: boolean
  on?: boolean
  size?: 'small' | 'normal' | 'big'
  grow?: boolean
  wide?: boolean
  onClick?: () => void
}) {
  const classes = ['pill', `pill-${size}`, primary && 'primary', on && 'on', grow && 'grow', wide && 'wide']
  return (
    <button className={classes.filter(Boolean).join(' ')} onClick={onClick}>
      {icon && <span className="icon">{icon}</span>}
      {label && <span>{label}</span>}
    </button>
  )
}

// addCard(): rounded surface; `vertical` stacks the title above the content
export function Card({ title, vertical, onClick, children }: {
  title?: string
  vertical?: boolean
  onClick?: () => void
  children?: ReactNode
}) {
  return (
    <div className={`card${vertical ? ' vertical' : ''}${onClick ? ' tappable' : ''}`} onClick={onClick}>
      {title && <span>{title}</span>}
      {children}
    </div>
  )
}

export function RoundButton({ icon, size = 48, primary, className, onClick }: {
  icon: string
  size?: number
  primary?: boolean
  className?: string
  onClick?: (e: React.MouseEvent) => void
}) {
  return (
    <button className={`round icon${primary ? ' primary' : ''}${className ? ` ${className}` : ''}`}
            style={{ width: size, height: size }} onClick={onClick}>
      {icon}
    </button>
  )
}

// lv_switch
export function Switch({ on, onChange }: { on: boolean; onChange: (on: boolean) => void }) {
  return (
    <button className={`switch${on ? ' on' : ''}`} onClick={(e) => { e.stopPropagation(); onChange(!on) }}>
      <span />
    </button>
  )
}

// addBigSlider(): lv_slider as one thick bar with no knob, drag or tap anywhere on it
export function BigSlider({ value, min = 0, max = 100, icon, onChange }: {
  value: number
  min?: number
  max?: number
  icon?: string
  onChange: (value: number) => void
}) {
  const set = (e: React.PointerEvent<HTMLDivElement>) => {
    const r = e.currentTarget.getBoundingClientRect()
    const f = Math.min(1, Math.max(0, (e.clientX - r.left) / r.width))
    onChange(Math.round(min + f * (max - min)))
  }
  const percent = ((value - min) / (max - min)) * 100

  return (
    <div className="big-slider"
         onPointerDown={(e) => { e.currentTarget.setPointerCapture(e.pointerId); set(e) }}
         onPointerMove={(e) => { if (e.buttons) set(e) }}>
      <div className="fill" style={{ width: `${percent}%` }} />
      {icon && <span className={`icon${percent > 12 ? ' on-fill' : ''}`}>{icon}</span>}
      <span className="value">{value}%</span>
    </div>
  )
}

// lv_roller: drag up or down, or tap the number above or below. "00" … for
// `count` numbers, or `labels` (e.g. 12, 1 … 11); `stops` for
// LV_ROLLER_MODE_NORMAL, which doesn't go round (nothing above the first)
export function Roller({ value, count, labels, stops, width, small, onChange }: {
  value: number
  count?: number
  labels?: string[]
  stops?: boolean
  width?: number
  small?: boolean  // the 22 / 30 px fonts instead of 30 / 50 (Set by hand's date)
  onChange: (value: number) => void
}) {
  const step = 40  // px of drag per number
  const drag = useRef<{ y: number; moved: boolean } | null>(null)
  const n = labels?.length ?? count ?? 0
  const wrap = (v: number) => (stops ? Math.min(n - 1, Math.max(0, v)) : (v % n + n) % n)
  const show = (v: number) => {
    if (stops && (v < 0 || v >= n)) return ''
    return labels ? labels[wrap(v)] : String(wrap(v)).padStart(2, '0')
  }
  const tap = (delta: number) => { if (!drag.current?.moved) onChange(wrap(value + delta)) }

  return (
    <div className={`roller${small ? ' small' : ''}`} style={width ? { width } : undefined}
         onPointerDown={(e) => { e.currentTarget.setPointerCapture(e.pointerId); drag.current = { y: e.clientY, moved: false } }}
         onPointerMove={(e) => {
           const d = drag.current
           if (!d) return
           const steps = Math.trunc((d.y - e.clientY) / step)
           if (steps) {
             d.y -= steps * step
             d.moved = true
             onChange(wrap(value + steps))
           }
         }}
         onPointerUp={() => setTimeout(() => { drag.current = null })}>
      <span onClick={() => tap(-1)}>{show(value - 1)}</span>
      <span className="selected">{show(value)}</span>
      <span onClick={() => tap(1)}>{show(value + 1)}</span>
    </div>
  )
}
