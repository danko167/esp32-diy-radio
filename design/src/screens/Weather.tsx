// Weather (src/ui/screen_weather.cpp):
// - Now: big icon and temperature, the next day at a glance, and pills
//   to the other pages; fits the screen without scrolling
// - Hourly: a temperature curve for the next 24 hours, then hour by hour
// - Next days: one row per day with a min–max bar on a shared scale
// - Details: feels like, humidity, wind, sunrise, sunset, daylight
// All from weather::Forecast (Open-Meteo). The first hour is the current
// one, so it's labelled Now, like on the device.
import { useState } from 'react'
import type { ReactNode } from 'react'
import { AppScreen, Card, Hint, Pill, Section } from '../components'
import { symbol, weather as wi } from '../icons'
import './weather.css'

type Hour = { hour: number; temp: number; icon: string; rain: number }
type Day = { name: string; min: number; max: number; icon: string; rain: number; text: string }

// The mockup's "now": Sunday 17:42 in Zagreb, after sunset
const now = { temp: 12, icon: wi.partlyCloudyNight, text: 'Partly cloudy', feels: 10, humidity: 78, wind: 14 }
const sunrise = '06:48'
const sunset = '16:42'
const updated = '17:30'

const hours: Hour[] = [
  [18, 11, wi.partlyCloudyNight, 0], [19, 10, wi.partlyCloudyNight, 0], [20, 9, wi.clearNight, 0],
  [21, 9, wi.clearNight, 0], [22, 8, wi.clearNight, 0], [23, 8, wi.clearNight, 0],
  [0, 7, wi.clearNight, 0], [1, 7, wi.clearNight, 0], [2, 7, wi.partlyCloudyNight, 0],
  [3, 6, wi.partlyCloudyNight, 0], [4, 6, wi.fog, 0], [5, 6, wi.fog, 0],
  [6, 7, wi.fog, 0], [7, 8, wi.fog, 0], [8, 10, wi.partlyCloudyDay, 0],
  [9, 12, wi.clearDay, 0], [10, 13, wi.clearDay, 0], [11, 14, wi.clearDay, 0],
  [12, 15, wi.partlyCloudyDay, 10], [13, 15, wi.partlyCloudyDay, 10], [14, 14, wi.cloudy, 20],
  [15, 13, wi.cloudy, 30], [16, 12, wi.showers, 40], [17, 11, wi.showers, 50],
].map(([hour, temp, icon, rain]) => ({ hour, temp, icon, rain }) as Hour)

const days: Day[] = [
  { name: 'Today', min: 6, max: 14, icon: wi.partlyCloudyDay, rain: 10, text: 'Partly cloudy' },
  { name: 'Monday', min: 6, max: 15, icon: wi.showers, rain: 50, text: 'Showers later' },
  { name: 'Tuesday', min: 8, max: 13, icon: wi.cloudy, rain: 20, text: 'Overcast' },
  { name: 'Wednesday', min: 9, max: 12, icon: wi.rain, rain: 80, text: 'Rain' },
  { name: 'Thursday', min: 5, max: 10, icon: wi.showers, rain: 60, text: 'Showers' },
  { name: 'Friday', min: 2, max: 8, icon: wi.clearDay, rain: 0, text: 'Clear' },
]

const two = (n: number) => String(n).padStart(2, '0')
// formatHour(): a column's "Now" or "15"
const columnHour = (h: number, first: boolean) => (first ? 'Now' : two(h))
// The hour list's "Now" or "15:00"
const rowHour = (h: number, first: boolean) => (first ? 'Now' : `${two(h)}:00`)

export type WeatherView = 'now' | 'hourly' | 'days' | 'details' | 'offline' | 'loading'

export default function Weather({ start = 'now' }: { start?: WeatherView }) {
  const [view, setView] = useState<WeatherView>(start)
  const back = () => setView('now')
  if (view === 'hourly') return <Hourly onBack={back} />
  if (view === 'days') return <Days onBack={back} />
  if (view === 'details') return <Details onBack={back} />
  if (view === 'offline' || view === 'loading') return <Waiting offline={view === 'offline'} />
  return <Now go={setView} />
}

function Now({ go }: { go: (v: WeatherView) => void }) {
  const glance = hours.filter((_, i) => i % 3 === 0)  // every third hour, a day ahead
  const today = days[0]
  return (
    <AppScreen caption="WEATHER" title="Zagreb">
      <div className="wx-now">
        <div className="wx-hero">
          <span className="weather-icon wx-hero-icon">{now.icon}</span>
          <div className="wx-hero-text">
            <div className="wx-temp">{now.temp}°</div>
            <div className="wx-desc">{now.text}</div>
            <div className="dim">Feels {now.feels}° · today {today.min}° – {today.max}°</div>
          </div>
        </div>

        <Card onClick={() => go('hourly')}>
          <div className="wx-glance">
            {glance.map((h, i) => (
              <div key={h.hour} className="wx-glance-hour">
                <span className="small dim">{columnHour(h.hour, i === 0)}</span>
                <span className="weather-icon">{h.icon}</span>
                <span>{h.temp}°</span>
              </div>
            ))}
          </div>
        </Card>

        <div className="wx-pills">
          <Pill icon={symbol.clock} label="Hourly" size="big" grow onClick={() => go('hourly')} />
          <Pill icon={symbol.list} label="Days" size="big" grow onClick={() => go('days')} />
          <Pill icon={symbol.info} label="Details" size="big" grow onClick={() => go('details')} />
        </div>
      </div>
    </AppScreen>
  )
}


const Page = ({ title, onBack, children }: { title: string; onBack: () => void; children: ReactNode }) => (
  <AppScreen caption="WEATHER · ZAGREB" title={title} onBack={onBack}>{children}</AppScreen>
)

// On the device: an lv_chart line series (or lv_line) with labels on top;
// the SVG here only stands in for it
function TempChart() {
  const shown = hours.filter((_, i) => i % 3 === 0)  // 8 points, 3 hours apart
  const width = 412
  const height = 90
  const temps = shown.map((h) => h.temp)
  const lo = Math.min(...temps)
  const hi = Math.max(...temps)
  const x = (i: number) => ((i + 0.5) * width) / shown.length
  const y = (t: number) => height - 14 - ((t - lo) / (hi - lo || 1)) * (height - 44)

  return (
    <div className="wx-chart">
      <div className="wx-chart-row">
        {shown.map((h) => <span key={h.hour} className="weather-icon">{h.icon}</span>)}
      </div>
      <svg width={width} height={height} viewBox={`0 0 ${width} ${height}`}>
        <polyline points={shown.map((h, i) => `${x(i)},${y(h.temp)}`).join(' ')}
                  fill="none" stroke="var(--accent)" strokeWidth="3" strokeLinejoin="round" />
        {shown.map((h, i) => (
          <g key={h.hour}>
            <circle cx={x(i)} cy={y(h.temp)} r="5" fill="var(--accent)" />
            <text x={x(i)} y={y(h.temp) - 12} textAnchor="middle" fill="var(--text)" fontSize="22">{h.temp}°</text>
          </g>
        ))}
      </svg>
      <div className="wx-chart-row">
        {shown.map((h) => (
          <span key={h.hour} className={`small ${h.rain >= 10 ? 'accent' : 'faint'}`}>
            {h.rain >= 10 ? `${h.rain}%` : '–'}
          </span>
        ))}
      </div>
      <div className="wx-chart-row">
        {shown.map((h, i) => (
          <span key={h.hour} className="small dim">{columnHour(h.hour, i === 0)}</span>
        ))}
      </div>
    </div>
  )
}

function Hourly({ onBack }: { onBack: () => void }) {
  return (
    <Page title="Next 24 hours" onBack={onBack}>
      <Card vertical><TempChart /></Card>

      <Section>HOUR BY HOUR</Section>
      <div className="wx-list">
        {hours.map((h, i) => (
          <div key={h.hour} className="wx-hour-row">
            <span className="wx-hour-time">{rowHour(h.hour, i === 0)}</span>
            <span className="weather-icon wx-row-icon">{h.icon}</span>
            <span className="wx-rain">
              {h.rain >= 10 && <><span className="weather-icon">{wi.raindrop}</span> {h.rain}%</>}
            </span>
            <span className="wx-hour-temp">{h.temp}°</span>
          </div>
        ))}
      </div>
    </Page>
  )
}

function Days({ onBack }: { onBack: () => void }) {
  // One scale for every bar, so the days compare at a glance
  const lo = Math.min(...days.map((d) => d.min))
  const hi = Math.max(...days.map((d) => d.max))
  const at = (t: number) => ((t - lo) / (hi - lo)) * 100

  return (
    <Page title="Next days" onBack={onBack}>
      <div className="wx-list">
        {days.map((d, i) => (
          <div key={d.name} className="wx-day-row">
            <div className="wx-day-name">
              <div>{d.name}</div>
              <div className={`small ${d.rain >= 10 ? 'accent' : 'dim'}`}>
                {d.rain >= 10 ? `${d.rain}% rain` : d.text}
              </div>
            </div>
            <span className="weather-icon wx-row-icon">{d.icon}</span>
            <span className="wx-day-min">{d.min}°</span>
            <div className="wx-range">
              <div className="wx-range-fill" style={{ left: `${at(d.min)}%`, right: `${100 - at(d.max)}%` }} />
              {i === 0 && <div className="wx-range-now" style={{ left: `${at(now.temp)}%` }} />}
            </div>
            <span className="wx-day-max">{d.max}°</span>
          </div>
        ))}
      </div>
    </Page>
  )
}

function Details({ onBack }: { onBack: () => void }) {
  const tiles: [string, string, string][] = [
    [wi.thermometer, `${now.feels}°`, 'Feels like'],
    [wi.humidity, `${now.humidity}%`, 'Humidity'],
    [wi.wind, `${now.wind} km/h`, 'Wind'],
    [wi.raindrop, `${days[0].rain}%`, 'Rain today'],
    [wi.sunrise, sunrise, 'Sunrise'],
    [wi.sunset, sunset, 'Sunset'],
  ]
  return (
    <Page title="Details" onBack={onBack}>
      <div className="wx-tiles">
        {tiles.map(([icon, value, label]) => (
          <div key={label} className="wx-tile">
            <span className="weather-icon accent wx-tile-icon">{icon}</span>
            <span className="wx-tile-text">
              <span className="wx-tile-value">{value}</span>
              <span className="small dim">{label}</span>
            </span>
          </div>
        ))}
      </div>
      {/* Open-Meteo data is CC BY 4.0, which asks for attribution */}
      <Hint>Updated {updated} · Weather data by Open-Meteo.com</Hint>
    </Page>
  )
}

function Waiting({ offline }: { offline: boolean }) {
  return (
    <AppScreen title="Weather">
      <div className="wx-waiting">
        {offline ? (
          <>
            <span className="icon wx-waiting-icon">{symbol.wifi}</span>
            <div>No Wi-Fi, so no forecast.</div>
            <div className="small dim">Connect to Wi-Fi in Settings.</div>
          </>
        ) : (
          <>
            <span className="icon wx-waiting-icon wx-spin">{symbol.refresh}</span>
            <div>Getting the forecast…</div>
          </>
        )}
      </div>
    </AppScreen>
  )
}
