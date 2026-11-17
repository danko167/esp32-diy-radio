// Home (src/ui/screen_home.cpp):
// - Battery top left (as on the device since 303469b), Wi-Fi top right
// - The clock lower, with date and weather under it, and the next alarm below those
// - One row of tiles that scrolls sideways, three at a time, with dots for
//   the page; only that row moves, the clock stays put
import { useEffect, useRef, useState } from 'react'
import { symbol, weather } from '../icons'
import './home.css'
import './recorder.css'

const tiles = [
  { icon: symbol.tower, label: 'FM Radio' },
  { icon: symbol.globe, label: 'Web Radio' },  // Internet Radio: the globe, as Wi-Fi has the Wi-Fi icon
  { icon: symbol.bluetooth, label: 'Bluetooth' },
  { icon: symbol.tint, label: 'Weather' },
  { icon: symbol.bell, label: 'Alarm' },
  { icon: symbol.mic, label: 'Recorder' },  // the microphones: a third page
  { icon: symbol.ruler, label: 'Level' },  // the motion sensor as a spirit level
  { icon: symbol.settings, label: 'Settings' },
]
const perPage = 3
const pages = Math.ceil(tiles.length / perPage)

// The battery: on USB charging (the bolt), on USB and full (the plug), or on
// battery and low (red; never red on USB)
const batteries = {
  charging: { source: symbol.charge, icon: symbol.battery3, level: '82%' },
  full: { source: symbol.usb, icon: symbol.battery, level: '100%' },
  low: { source: null, icon: symbol.batteryEmpty, level: '8%' },
}

// `background`: CSS for the wallpaper, e.g. an uploaded photo (default: Sunset)
// `uploading`: the upload server is on (Settings > Display > Your picture)
// `recording`: the Recorder is recording (a red dot that never hides; taps open it)
// `playing`: the radio plays (the same dot in the accent; taps open Now Playing).
//   Never with the other two: the radio is held off while recording or uploading
export default function Home({ startPage = 0, background, uploading = false, recording = false, power = 'charging',
                               playing = false }: {
  startPage?: number
  background?: string
  uploading?: boolean
  recording?: boolean
  playing?: boolean
  power?: keyof typeof batteries
}) {
  const battery = batteries[power]
  const row = useRef<HTMLDivElement>(null)
  const [page, setPage] = useState(startPage)
  useEffect(() => {
    if (row.current) row.current.scrollLeft = startPage * row.current.clientWidth
  }, [startPage])

  // Pages are the row's width apart (scroll-snap stops on each)
  const onScroll = () => {
    const el = row.current
    if (el) setPage(Math.round(el.scrollLeft / el.clientWidth))
  }
  const goTo = (p: number) => row.current?.scrollTo({ left: p * row.current.clientWidth, behavior: 'smooth' })

  return (
    <div className="home home2">
      <div className="wallpaper sunset" style={background ? { background } : undefined} />
      <span className={`home2-battery${power === 'low' ? ' low' : ''}`}>
        {battery.source && <><span className="icon">{battery.source}</span> </>}
        <span className="icon">{battery.icon}</span> {battery.level}
      </span>
      <span className="home2-status icon">
        {/* Tapping it opens the upload page, to see the code or stop it (on the
            device: a bigger tap area than the icon) */}
        {uploading && (
          <a className="home2-upload" href="#Settings / Upload pictures — waiting" title="Upload pictures">
            {symbol.upload}
          </a>
        )}
        {recording && (
          <a className="home2-upload home-rec" href="#Recorder / Recording" title="Recording">●</a>
        )}
        {playing && (
          <a className="home2-upload" href="#Internet Radio / Now Playing" title="Now playing">●</a>
        )}
        {symbol.wifi}
      </span>

      <div className="home2-clock">17:42</div>
      <div className="home2-info">
        <span className="date">Sunday, 1 November</span>
        <span className="weather-icon">{weather.partlyCloudyDay}</span>
        <span>12°</span>
      </div>
      <div className="home2-alarm"><span className="icon">{symbol.bell}</span> 07:30</div>

      <div className="home2-row" ref={row} onScroll={onScroll}>
        {tiles.map((t) => (
          <button key={t.label} className="tile">
            <span className="icon">{t.icon}</span>
            <span>{t.label}</span>
          </button>
        ))}
        {/* A short last page still fills the row, so it scrolls a whole page and shows only its own */}
        {Array.from({ length: pages * perPage - tiles.length }, (_, i) => <span key={`gap${i}`} className="tile home2-filler" />)}
      </div>
      <div className="home2-dots">
        {Array.from({ length: pages }, (_, p) => (
          <button key={p} className={p === page ? 'on' : ''} onClick={() => goTo(p)} aria-label={`Page ${p + 1}`} />
        ))}
      </div>
    </div>
  )
}
