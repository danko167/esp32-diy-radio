// Faces for the clock screen (src/ui/ambient_faces.cpp), picked in
// Settings > Clock screen. Digital is the one on the device now.
// - Analog: dial, hands and the date inside; on the device the hands are
//   lv_line end points (no images, no rotation), moved once a minute, or each
//   second for the seconds hand when Show seconds is on
// - Split: hours over minutes on the left, date, alarm and weather beside them
// - Night: small and dim red, for a dark bedroom; lights the fewest pixels
// - Segment: Night in 7-segment LED digits (DSEG7), over their faint unlit
//   "88:88", like an old alarm clock
// Night and Segment are red or green (nightColour, Settings > Clock face).
// All of them still move a few pixels each minute on the device (burn-in).
import { symbol, weather } from '../icons'
import './clock-faces.css'
import type { Face, NightColour } from './faces'

// The mockup's moment: Sunday 1 November, 17:42:05, next alarm 07:30
const hour = 17
const minute = 42
const second = 5
const two = (n: number) => String(n).padStart(2, '0')

// showSeconds: Settings > Clock screen > Show seconds
type Props = {
  face: Face
  showWeather?: boolean
  showSeconds?: boolean
  nightColour?: NightColour
}

// What the faces show of the time: the hour and the next alarm (nextAlarmText())
type Clock = { hour: string; alarm: string }
const clock: Clock = { hour: two(hour), alarm: '07:30' }

export default function ClockFace({ face, showWeather = true, showSeconds = false, nightColour = 'red' }: Props) {
  return (
    <div className={`cf cf-${face} cf-${nightColour}`}>
      {face === 'digital' && <Digital c={clock} showWeather={showWeather} />}
      {face === 'analog' && <Analog c={clock} showWeather={showWeather} showSeconds={showSeconds} />}
      {face === 'split' && <Split c={clock} showWeather={showWeather} />}
      {face === 'night' && <Night c={clock} showWeather={showWeather} />}
      {face === 'segment' && <Segment c={clock} showWeather={showWeather} />}
    </div>
  )
}

const Alarm = ({ c }: { c: Clock }) => <span className="cf-alarm"><span className="icon">{symbol.bell}</span> {c.alarm}</span>

const Weather = ({ text = true }: { text?: boolean }) => (
  <span className="cf-weather">
    <span className="weather-icon">{weather.partlyCloudyDay}</span>
    <span>12°{text && '  Partly cloudy'}</span>
  </span>
)

// As on the device today
function Digital({ c, showWeather }: { c: Clock; showWeather: boolean }) {
  return (
    <div className="cf-column">
      <div className="cf-time">{c.hour}:{two(minute)}</div>
      <div className="cf-date">Sunday, 1 November</div>
      <Alarm c={c} />
      {showWeather && <Weather />}
    </div>
  )
}

function Analog({ c: clock, showWeather, showSeconds }: { c: Clock; showWeather: boolean; showSeconds: boolean }) {
  const r = 150  // dial radius
  const c = 165  // centre, in the svg's own pixels
  const hourAngle = ((hour % 12) + minute / 60) * 30
  const minuteAngle = (minute + second / 60) * 6
  const secondAngle = second * 6
  const point = (angle: number, length: number) => {
    const a = ((angle - 90) * Math.PI) / 180
    return { x: c + length * Math.cos(a), y: c + length * Math.sin(a) }
  }
  const hand = (angle: number, length: number, width: number, colour: string) => {
    const end = point(angle, length)
    return <line x1={c} y1={c} x2={end.x} y2={end.y} stroke={colour} strokeWidth={width} strokeLinecap="round" />
  }
  return (
    <div className="cf-analog-wrap">
      <div className="cf-dial">
        <svg width={2 * c} height={2 * c}>
          {Array.from({ length: 12 }, (_, i) => {
            const major = i % 3 === 0
            const from = point(i * 30, r - (major ? 26 : 16))
            const to = point(i * 30, r)
            return <line key={i} x1={from.x} y1={from.y} x2={to.x} y2={to.y}
                         stroke={major ? 'var(--text)' : 'var(--text-dim)'} strokeWidth={major ? 6 : 3}
                         strokeLinecap="round" />
          })}
          {hand(hourAngle, 82, 10, 'var(--text)')}
          {hand(minuteAngle, 124, 6, 'var(--text)')}
          {showSeconds && hand(secondAngle, 136, 2, '#ff453a')}  {/* theme::kRed */}
          <circle cx={c} cy={c} r={8} fill="var(--accent)" />
        </svg>
        <div className="cf-dial-date">SUN 1</div>
      </div>
      <div className="cf-analog-info">
        <Alarm c={clock} />
        {showWeather && <Weather text={false} />}
      </div>
    </div>
  )
}

function Split({ c, showWeather }: { c: Clock; showWeather: boolean }) {
  return (
    <div className="cf-split">
      <div className="cf-split-time">
        <span>{c.hour}</span>
        <span className="cf-split-minutes">{two(minute)}</span>
      </div>
      <div className="cf-split-info">
        <span className="cf-split-day">Sunday</span>
        <span className="cf-date">1 November</span>
        <Alarm c={c} />
        {showWeather && <Weather text={false} />}
      </div>
    </div>
  )
}

// The night faces' weather: small, in their own dim colour like the date line
const NightWeather = () => (
  <div className="cf-night-date cf-night-weather">
    <span className="weather-icon">{weather.partlyCloudyDay}</span> 12°
  </div>
)

function Night({ c, showWeather }: { c: Clock; showWeather: boolean }) {
  return (
    <div className="cf-column">
      <div className="cf-night-time">{c.hour}:{two(minute)}</div>
      <div className="cf-night-date">Sun 1 Nov · <span className="icon">{symbol.bell}</span> {c.alarm}</div>
      {showWeather && <NightWeather />}
    </div>
  )
}

// Night's, in LED digits. Every digit is as wide, so the lit time always
// covers the unlit "88:88".
function Segment({ c, showWeather }: { c: Clock; showWeather: boolean }) {
  return (
    <div className="cf-column">
      <div className="cf-segment-time">
        <span className="cf-segment-unlit">88:88</span>
        <span>{c.hour}:{two(minute)}</span>
      </div>
      <div className="cf-night-date">Sun 1 Nov · <span className="icon">{symbol.bell}</span> {c.alarm}</div>
      {showWeather && <NightWeather />}
    </div>
  )
}
