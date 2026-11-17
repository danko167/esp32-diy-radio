// Recorder, a new app (Home's seventh tile): the board's two microphones
// (ES7210) to WAV files on the microSD card.
// - Ready: a big record button, Voice or Music, the time left on the card,
//   the latest recordings
// - Recording: a red dot that never hides, the running time, the level, Stop;
//   the radio stops for it (the speaker would be in the recording)
// - Recordings: all of them, play on the speaker, delete; the upload page
//   downloads them to a computer
import { useEffect, useState } from 'react'
import { AppScreen, Card, Hint, Pill, RoundButton, Section } from '../components'
import { symbol } from '../icons'
import { duration, sampleRecordings, size } from '../recordings'
import type { Recording } from '../recordings'
import './recorder.css'

export type RecorderView = 'ready' | 'recording' | 'list' | 'playing'

// Voice: 16 kHz mono, 32 KB/s. Music: 48 kHz stereo, 192 KB/s. Plain WAV.
const qualities = [
  { id: 'voice', label: 'Voice', bytesPerSecond: 32000, detail: '16 kHz mono', what: 'small files' },
  { id: 'music', label: 'Music', bytesPerSecond: 192000, detail: '48 kHz stereo', what: 'full sound' },
] as const
type Quality = (typeof qualities)[number]['id']

const cardFree = 14_905 * 1024 * 1024  // as the radio says in `h`

export default function Recorder({ start = 'ready' }: { start?: RecorderView }) {
  const [view, setView] = useState<RecorderView>(start === 'playing' ? 'list' : start)
  const [quality, setQuality] = useState<Quality>('voice')
  const [seconds, setSeconds] = useState(start === 'recording' ? 134 : 0)
  const [level, setLevel] = useState(0.4)
  const [recordings, setRecordings] = useState(sampleRecordings)
  const [playing, setPlaying] = useState<number | null>(start === 'playing' ? 2 : null)
  const [played, setPlayed] = useState(start === 'playing' ? 0.35 : 0)
  const q = qualities.find((x) => x.id === quality)!

  // While recording: the clock runs and the level moves (on the device: the
  // loudest sample of the last 100 ms)
  useEffect(() => {
    if (view !== 'recording') return
    const tick = setInterval(() => setSeconds((s) => s + 1), 1000)
    const meter = setInterval(() => setLevel(0.15 + Math.random() * 0.7), 120)
    return () => { clearInterval(tick); clearInterval(meter) }
  }, [view])

  // While playing: the progress moves on
  useEffect(() => {
    if (playing === null) return
    const length = recordings.find((r) => r.id === playing)?.seconds ?? 1
    const t = setInterval(() => setPlayed((p) => {
      if (p >= 1) { setPlaying(null); return 0 }
      return p + 1 / length
    }), 1000)
    return () => clearInterval(t)
  }, [playing, recordings])

  const stop = () => {
    setRecordings([{ id: Date.now(), when: 'Tue 3 Nov, 15:44', seconds, bytes: seconds * q.bytesPerSecond }, ...recordings])
    setSeconds(0)
    setView('ready')
  }

  if (view === 'recording') {
    return (
      <AppScreen title="Recorder">
        <div className="rec-live">
          <span className="rec-status"><span className="rec-dot" /> Recording</span>
          <span className="rec-time">{duration(seconds)}</span>
          <span className="rec-meter"><span style={{ width: `${level * 100}%` }} /></span>
          <span className="rec-detail">{q.label} · {q.detail} · {size(seconds * q.bytesPerSecond)}</span>
          <button className="rec-stop" onClick={stop} aria-label="Stop"><span /></button>
        </div>
        <Hint>The radio stopped for the recording; it doesn't start again by itself.</Hint>
      </AppScreen>
    )
  }

  if (view === 'list') {
    return (
      <AppScreen caption="RECORDER" title="Recordings" onBack={() => setView('ready')}>
        <List recordings={recordings} playing={playing} played={played}
              onPlay={(id) => { setPlaying(playing === id ? null : id); setPlayed(0) }}
              onDelete={(id) => setRecordings(recordings.filter((r) => r.id !== id))} />
        <Hint>Download them on a computer: the upload page (Settings › Display › Your picture).</Hint>
      </AppScreen>
    )
  }

  const hoursLeft = Math.floor(cardFree / q.bytesPerSecond / 3600)
  return (
    <AppScreen title="Recorder">
      <div className="rec-ready">
        <button className="rec-button" onClick={() => setView('recording')} aria-label="Record">
          <span className="icon">{symbol.mic}</span>
        </button>
      </div>
      <Section>QUALITY</Section>
      <div className="rec-quality">
        {qualities.map((x) => (
          <button key={x.id} className={`rec-q${x.id === quality ? ' on' : ''}`} onClick={() => setQuality(x.id)}>
            <span>{x.label}</span>
            <span className="rec-q-what">{x.what}</span>
          </button>
        ))}
      </div>
      <Hint>{q.detail} · room for about {hoursLeft} hours on the card</Hint>
      <Section>LATEST</Section>
      <List recordings={recordings.slice(0, 2)} playing={playing} played={played}
            onPlay={(id) => { setPlaying(playing === id ? null : id); setPlayed(0) }} />
      {/* Under the latest ones, scrolling with them: as wide and big as the alarm editor's Save */}
      <div className="rec-all">
        <Pill icon={symbol.list} label={`All recordings (${recordings.length})`} size="big" grow
              onClick={() => setView('list')} />
      </div>
    </AppScreen>
  )
}

function List({ recordings, playing, played, onPlay, onDelete }: {
  recordings: Recording[]
  playing: number | null
  played: number
  onPlay: (id: number) => void
  onDelete?: (id: number) => void
}) {
  if (!recordings.length) return <Section>Nothing recorded yet</Section>
  return (
    <>
      {recordings.map((r) => (
        <Card key={r.id} vertical>
          <div className="rec-row">
            <RoundButton icon={playing === r.id ? symbol.stop : symbol.play} size={48}
                         primary={playing === r.id} onClick={() => onPlay(r.id)} />
            <div className="rec-text">
              <span>{r.when}</span>
              <span className="rec-sub">{duration(r.seconds)} · {size(r.bytes)}</span>
            </div>
            {onDelete && <RoundButton icon={symbol.trash} size={44} onClick={() => onDelete(r.id)} />}
          </div>
          {playing === r.id && (
            <span className="rec-progress"><span style={{ width: `${played * 100}%` }} /></span>
          )}
        </Card>
      ))}
    </>
  )
}
