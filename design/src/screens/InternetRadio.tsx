// Internet radio, as built on the device in src/ui/screen_radio.cpp:
// - Now Playing: big station and song, prev/next favourite, volume
// - Stations: favourites as preset rows, popular, recent and search
// - Recently heard: songs from the stream titles, kept in RAM
// Separate from FM radio, which will get its own screens.
import { useRef, useState } from 'react'
import type { ReactNode } from 'react'
import { AppScreen, Card, Hint, Pill, RoundButton, Section } from '../components'
import { symbol } from '../icons'
import './internet-radio.css'

type Station = { name: string; country: string; codec: string; bitrate: number }

const station = (name: string, country = 'HR', codec = 'MP3', bitrate = 128): Station =>
  ({ name, country, codec, bitrate })

const popular = [
  station('Narodni Radio'),
  station('Otvoreni Radio'),
  station('HR 1 Prvi program', 'HR', 'AAC', 96),
  station('Antena Zagreb'),
  station('Radio 101'),
  station('Rock Radio'),
  station('Extra FM'),
  station('HR 2 Drugi program', 'HR', 'AAC', 96),
  station('Laganini FM'),
  station('Radio Dalmacija'),
]
const radioParadise = station('Radio Paradise', 'US')
const fip = station('FIP', 'FR', 'AAC', 192)
const jazz24 = station('Jazz24', 'US', 'AAC', 64)
const recent = [radioParadise, fip, popular[0], jazz24]
const jazzResults = [jazz24, station('KCSM Jazz 91', 'US'), station('Jazz Radio', 'FR'),
  station('Radio Swiss Jazz', 'CH', 'AAC', 96)]
const startFavourites = [popular[5], popular[2], popular[1], radioParadise, fip, popular[3]]

// What the streams send as the song title (empty: the station sends none)
const titles: Record<string, string> = {
  'Rock Radio': 'R.E.M. – Sweetness Follows',
  'Radio Paradise': 'Khruangbin – Maria También',
  FIP: 'Nina Simone – Feeling Good',
  'Otvoreni Radio': 'Dua Lipa – Houdini',
  'Narodni Radio': 'Oliver Dragojević – Cesarica',
  Jazz24: 'Miles Davis – So What',
}

const songs = [
  { title: 'R.E.M. – Sweetness Follows', station: 'Rock Radio', time: '17:41' },
  { title: 'Pearl Jam – Black', station: 'Rock Radio', time: '17:36' },
  { title: 'Nina Simone – Feeling Good', station: 'FIP', time: '17:22' },
  { title: 'Air – La femme d’argent', station: 'FIP', time: '17:15' },
  { title: 'Khruangbin – Maria También', station: 'Radio Paradise', time: '16:58' },
  { title: 'Bonobo – Kerala', station: 'Radio Paradise', time: '16:52' },
]

const sleepSteps = [0, 15, 30, 60, 90]  // minutes, tap to go round

type View = 'now' | 'stations' | 'songs'
type Tab = 'Favourites' | 'Popular' | 'Recent' | 'Search'
// wifi::status() as offlineText() sees it: on, a saved network still coming
// up (or back), or none to use (not set up, wrong password, not found)
type WiFi = 'connected' | 'connecting' | 'off'

const offlineText: Record<WiFi, string | null> = {
  connected: null,
  connecting: 'Connecting to Wi-Fi…',
  off: 'Connect to Wi-Fi in Settings to find stations.',
}

function useRadio(startTab: Tab, startPlaying: boolean) {
  const [current, setCurrent] = useState(startFavourites[0])
  const [playing, setPlaying] = useState(startPlaying)
  const [volume, setVolume] = useState(40)
  const [favourites, setFavourites] = useState(startFavourites)
  const [sleep, setSleep] = useState(0)
  const [tab, setTab] = useState<Tab>(startTab)
  const [searched, setSearched] = useState(false)

  const isFavourite = (s: Station) => favourites.some((f) => f.name === s.name)
  const play = (s: Station) => { setCurrent(s); setPlaying(true) }
  return {
    current, playing, volume, setVolume, favourites, sleep, tab, setTab, searched, setSearched,
    isFavourite, play,
    title: playing ? titles[current.name] ?? '' : '',
    togglePlay: () => setPlaying((p) => !p),
    toggleFavourite: (s: Station) =>
      setFavourites((f) => (isFavourite(s) ? f.filter((x) => x.name !== s.name) : [...f, s])),
    // Like radio::step(): next (+1) or previous (-1) favourite
    step: (direction: number) => {
      const n = favourites.length
      if (!n) return
      const i = favourites.findIndex((f) => f.name === current.name)
      play(favourites[i < 0 ? (direction > 0 ? 0 : n - 1) : (i + direction + n) % n])
    },
    cycleSleep: () => setSleep((m) => sleepSteps[(sleepSteps.indexOf(m) + 1) % sleepSteps.length]),
  }
}

type Radio = ReturnType<typeof useRadio>

// `tab`: the one Stations opens on; `wifi`: what Popular and Search can reach
export default function InternetRadio({ start = 'now', tab = 'Favourites', wifi = 'connected' }: {
  start?: View
  tab?: Tab
  wifi?: WiFi
}) {
  // No stream without Wi-Fi: the mini player says Stopped
  const radio = useRadio(tab, wifi === 'connected')
  const [view, setView] = useState<View>(start)
  if (view === 'stations') return <Stations r={radio} go={setView} wifi={wifi} />
  if (view === 'songs') return <Songs go={setView} />
  return <NowPlaying r={radio} go={setView} />
}

function NowPlaying({ r, go }: { r: Radio; go: (v: View) => void }) {
  const s = r.current
  const status = r.playing ? `Playing · ${s.codec} · ${s.bitrate} kbps` : 'Stopped · tap play to listen'
  const volumeIcon = r.volume === 0 ? symbol.mute : r.volume < 50 ? symbol.volumeMid : symbol.volumeMax

  return (
    <AppScreen title="Internet Radio"
               action={<RoundButton icon={symbol.star} size={54} className={r.isFavourite(s) ? 'ir-starred' : 'dim'}
                                    onClick={() => r.toggleFavourite(s)} />}>
      <div className="np">
        <div className="np-info">
          <div className="np-name">{s.name}</div>
          <div className="np-title">{r.title}</div>
          <div className="np-status">{status}</div>
        </div>

        <div className="np-controls">
          <RoundButton icon={symbol.prev} size={66} onClick={() => r.step(-1)} />
          <RoundButton icon={r.playing ? symbol.stop : symbol.play} size={98} primary className="np-play"
                       onClick={r.togglePlay} />
          <RoundButton icon={symbol.next} size={66} onClick={() => r.step(1)} />
        </div>

        <div className="np-volume">
          <span className="icon dim">{volumeIcon}</span>
          <input type="range" min={0} max={100} value={r.volume}
                 onChange={(e) => r.setVolume(Number(e.target.value))} />
          <span className="np-percent">{r.volume}%</span>
        </div>

        <div className="np-actions">
          <Pill icon={symbol.list} label="Stations" size="big" grow onClick={() => go('stations')} />
          <Pill icon={symbol.history} label="Songs" size="big" grow onClick={() => go('songs')} />
          <Pill icon={symbol.moon} label={r.sleep ? `${r.sleep} min` : 'Sleep'} on={r.sleep > 0}
                size="big" grow onClick={r.cycleSleep} />
        </div>
      </div>
    </AppScreen>
  )
}

function Stations({ r, go, wifi }: { r: Radio; go: (v: View) => void; wifi: WiFi }) {
  // directoryResults(): only the reason, no section or hint, until Wi-Fi is on
  const offline = offlineText[wifi]
  const tabs: { tab: Tab; label: ReactNode; narrow?: boolean }[] = [
    { tab: 'Favourites', label: <span className="icon">{symbol.star}</span>, narrow: true },
    { tab: 'Popular', label: 'Popular' },
    { tab: 'Recent', label: 'Recent' },
    { tab: 'Search', label: <span className="icon">{symbol.search}</span>, narrow: true },
  ]
  const rows = (list: Station[]) => list.map((s) => <StationRow key={s.name} s={s} r={r} />)

  return (
    <AppScreen caption="INTERNET RADIO" title="Stations" onBack={() => go('now')}
               footer={<MiniPlayer r={r} onOpen={() => go('now')} />}>
      <div className="st-tabs">
        {tabs.map((t) => (
          <button key={t.tab} className={`${t.tab === r.tab ? 'selected' : ''}${t.narrow ? ' narrow' : ''}`}
                  onClick={() => r.setTab(t.tab)}>
            {t.label}
          </button>
        ))}
      </div>

      {r.tab === 'Favourites' && (
        <div className="presets">
          {r.favourites.map((s, i) => (
            <button key={s.name} className={`preset${s.name === r.current.name ? ' current' : ''}`}
                    onClick={() => r.play(s)}>
              <span className="preset-number">{i + 1}</span>
              <Badge name={s.name} />
              <span className="preset-text">
                <span className="preset-name">{s.name}</span>
                <span className="preset-detail">{s.country} · {s.codec} · {s.bitrate} kbps</span>
              </span>
            </button>
          ))}
          <button className="preset add" onClick={() => r.setTab('Popular')}>
            <span className="icon">{symbol.plus}</span>
            <span>Add</span>
          </button>
        </div>
      )}

      {r.tab === 'Popular' && (offline
        ? <Hint>{offline}</Hint>
        : <><Section>MOST LISTENED IN CROATIA</Section>{rows(popular)}<HoldHint /></>)}
      {r.tab === 'Recent' && <><Section>PLAYED LATELY</Section>{rows(recent)}<HoldHint /></>}
      {r.tab === 'Search' && (
        <>
          <Card title="Search for a station…" onClick={() => r.setSearched(true)}>
            <span className="icon accent">{symbol.search}</span>
          </Card>
          {offline
            ? <Hint>{offline}</Hint>
            : r.searched && <><Section>RESULTS FOR “JAZZ”</Section>{rows(jazzResults)}<HoldHint /></>}
        </>
      )}
    </AppScreen>
  )
}

const HoldHint = () => <Hint>Hold a station to add it to favourites.</Hint>

// Tap plays the station; holding it stars it (or unstars it), so there's no
// star button to hit by accident next to the row. The star shows (or goes) at
// once, in the list as it is (markFavourites), without scrolling it to the top.
function StationRow({ s, r }: { s: Station; r: Radio }) {
  const timer = useRef(0)
  const held = useRef(false)
  const release = () => clearTimeout(timer.current)

  return (
    <div className="card tappable st-row"
         onPointerDown={() => {
           held.current = false
           timer.current = window.setTimeout(() => { held.current = true; r.toggleFavourite(s) }, 500)
         }}
         onPointerUp={release} onPointerLeave={release} onPointerCancel={release}
         onContextMenu={(e) => e.preventDefault()}
         onClick={() => { if (!held.current) r.play(s) }}>
      <Badge name={s.name} />
      <div className="st-text">
        <div className={`st-name${s.name === r.current.name ? ' accent' : ''}`}>{s.name}</div>
        <div className="st-detail">{s.country} · {s.codec} · {s.bitrate} kbps</div>
      </div>
      {r.isFavourite(s) && <span className="icon ir-starred">{symbol.star}</span>}
    </div>
  )
}

// Stand-in for a station logo: initials on a colour picked from the name
const badgeColours = ['#ff9f0a', '#30d158', '#bf5af2', '#ff375f', '#64d2ff', '#ffd60a']

function Badge({ name }: { name: string }) {
  const initials = name.split(' ').slice(0, 2).map((w) => w[0]).join('').toUpperCase()
  const colour = badgeColours[[...name].reduce((h, c) => h + c.charCodeAt(0), 0) % badgeColours.length]
  return <span className="badge" style={{ color: colour, background: `${colour}33` }}>{initials}</span>
}

function MiniPlayer({ r, onOpen }: { r: Radio; onOpen: () => void }) {
  return (
    <div className="mini" onClick={onOpen}>
      <RoundButton icon={r.playing ? symbol.stop : symbol.play} primary
                   onClick={(e) => { e.stopPropagation(); r.togglePlay() }} />
      <div className="mini-text">
        <div className="mini-name">{r.current.name}</div>
        <div className="mini-title">{r.playing ? r.title || 'Playing' : 'Stopped'}</div>
      </div>
      <span className="icon dim">{symbol.right}</span>
    </div>
  )
}

function Songs({ go }: { go: (v: View) => void }) {
  return (
    <AppScreen caption="INTERNET RADIO" title="Recently heard" onBack={() => go('now')}>
      {songs.map((s, i) => (
        <Card key={s.time} vertical>
          <div>
            <div className={`song-title${i === 0 ? ' accent' : ''}`}>{s.title}</div>
            <div className="song-detail">{i === 0 ? 'Now' : s.time} · {s.station}</div>
          </div>
        </Card>
      ))}
      <Hint>Songs since the radio was switched on.</Hint>
    </AppScreen>
  )
}
