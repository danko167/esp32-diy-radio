// What the radio says about its battery, on battery only (src/ui/ui.cpp,
// checkBattery()):
// - Low: a notice at the top for 8 s, again every 10 minutes while it stays
//   low. Over whatever is open, the clock screen too, which keeps its dim
//   level and stays (the notice isn't a touch)
// - Empty: sound off, everything saved, this for 3 s, then the radio
//   switches itself off. At whatever brightness the screen had.
import { symbol } from '../icons'
import ClockFace from './ClockFaces'
import './battery.css'

export default function Battery({ start = 'low' }: { start?: 'low' | 'empty' }) {
  if (start === 'empty') {
    return (
      <div className="battery-empty">
        <span className="icon">{symbol.batteryEmpty}</span>
        <span>Battery empty · switching off</span>
      </div>
    )
  }
  return (
    <div className="battery-over">
      <ClockFace face="digital" />
      {/* Like the volume bar, wider */}
      <div className="battery-notice">
        <span className="icon">{symbol.batteryEmpty}</span>
        <span>Battery low · plug in</span>
      </div>
    </div>
  )
}
