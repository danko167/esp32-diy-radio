// The page the radio serves at diy-radio.local while Settings > Display >
// Your picture is on (see UploadPage in Settings.tsx). It runs on the
// computer, not the radio: the browser crops the picture square and turns it
// into the radio's 480 x 480 RGB565, so the radio only has to store it. It
// also edits the favourite stations. On the device: web/upload.html, plain
// HTML and JS (no React) to stay small, with a simpler Home preview.
import { useRef, useState } from 'react'
import { demoPhoto } from '../demoPhoto'
import { symbol } from '../icons'
import Home from '../screens/Home'
import ClockFace from '../screens/ClockFaces'
import CustomFace from '../screens/CustomFace'
import { faces as builtInFaces } from '../screens/faces'
import { exampleFaces } from '../faceFile'
import type { FaceFile } from '../faceFile'
import FaceEditor from './FaceEditor'
import { duration, sampleRecordings, size } from '../recordings'
import { wallpaperBackground, wallpapers } from '../wallpapers'
import './upload-site.css'

const crop = 360       // the crop square on this page, px
const radio = 480      // the radio's screen, px
const fileKb = 450     // 480 x 480 x 2 bytes
const internalKb = 15200 * 1024  // the microSD card, where pictures go

type Photo = { name: string; src: string }
type Station = { name: string; url: string }
type Picked = { src: string; w: number; h: number; name: string }

export default function UploadSite({ start = 'unlocked' }: { start?: 'locked' | 'unlocked' | 'cropping' | 'faceEditor' }) {
  const [unlocked, setUnlocked] = useState(start !== 'locked')
  // The Recorder's files on the card: download (a link to the WAV) or delete
  const [recordings, setRecordings] = useState(sampleRecordings)
  // Faces sent from the editor, on the card; the built-in ones can't be changed
  const [customFaces, setCustomFaces] = useState<FaceFile[]>(exampleFaces)
  const [editing, setEditing] = useState<{ file: FaceFile; index: number } | null>(
    start === 'faceEditor' ? { file: exampleFaces[0], index: 0 } : null)
  const [code, setCode] = useState('')
  const [photos, setPhotos] = useState<Photo[]>([{ name: 'Lake', src: demoPhoto }])
  // The radio's favourites: the ★ list in Internet Radio
  const [stations, setStations] = useState<Station[]>([
    { name: 'Rock Radio', url: 'https://playerservices.streamtheworld.com/api/livestream-redirect/ROCK_RADIO_128.mp3' },
    { name: 'Radio Istra', url: 'http://de4.streamingpulse.com:7117/stream' },
  ])
  const [stName, setStName] = useState('')
  const [stUrl, setStUrl] = useState('')
  const [picked, setPicked] = useState<Picked | null>(
    start === 'cropping' ? { src: demoPhoto, w: 1200, h: 800, name: 'lake.jpg' } : null)

  const used = photos.length * fileKb
  const room = Math.floor((internalKb - used) / fileKb)

  const pick = (file: File) => {
    const src = URL.createObjectURL(file)
    const img = new Image()
    img.onload = () => setPicked({ src, w: img.naturalWidth, h: img.naturalHeight, name: file.name })
    img.src = src
  }

  if (!unlocked) {
    return (
      <div className="web">
        <Header />
        <div className="web-lock">
          <h2>Type the code from the radio's screen</h2>
          <input className="web-code" inputMode="numeric" maxLength={4} value={code} placeholder="····"
                 onChange={(e) => setCode(e.target.value.replace(/\D/g, ''))} />
          <button className="web-primary" disabled={code.length < 4} onClick={() => setUnlocked(true)}>
            Connect
          </button>
          <p className="web-dim">It's on the radio under Settings › Display › Your picture.</p>
        </div>
      </div>
    )
  }

  return (
    <div className="web">
      <Header connected />

      <section>
        <div className="web-row">
          <h2>Wallpapers on the radio</h2>
          <span className="web-dim">
            {(used / 1024).toFixed(1)} of {(internalKb / 1024).toFixed(1)} MB used · room for {room} more
          </span>
        </div>
        <div className="web-storage"><span style={{ width: `${(used / internalKb) * 100}%` }} /></div>

        <div className="web-grid">
          {wallpapers.map((w) => (
            <figure key={w.name} className="web-thumb">
              <span style={{ background: wallpaperBackground(w.blobs) }} />
              <figcaption>{w.name} <span className="web-dim">· built in</span></figcaption>
            </figure>
          ))}
          {photos.map((p) => (
            <figure key={p.name} className="web-thumb">
              <span style={{ background: `url("${p.src}") center / cover` }}>
                <button className="web-delete icon" title="Delete from the radio"
                        onClick={() => setPhotos(photos.filter((x) => x !== p))}>{symbol.close}</button>
              </span>
              <figcaption>{p.name}</figcaption>
            </figure>
          ))}
          <label className="web-thumb web-add">
            <span><span className="icon">{symbol.plus}</span></span>
            <figcaption>Add a picture</figcaption>
            <input type="file" accept="image/*" hidden
                   onChange={(e) => e.target.files?.[0] && pick(e.target.files[0])} />
          </label>
        </div>
      </section>

      {picked && (
        <Cropper key={picked.src} picked={picked} room={room > 0}
                 onCancel={() => setPicked(null)}
                 onSaved={(name) => {
                   setPhotos([...photos, { name, src: picked.src }])
                   setPicked(null)
                 }} />
      )}

      <section>
        <div className="web-row">
          <h2>Clock faces</h2>
          <span className="web-dim">Pick one on the radio in Settings › Clock screen › Face</span>
        </div>
        <div className="web-grid">
          {builtInFaces.map((f) => (
            <figure key={f.face} className="web-thumb">
              <span className="web-face"><span><ClockFace face={f.face} /></span></span>
              <figcaption>{f.name} <span className="web-dim">· built in</span></figcaption>
            </figure>
          ))}
          {customFaces.map((f, i) => (
            <figure key={f.name + i} className="web-thumb">
              <span className="web-face web-editable" onClick={() => setEditing({ file: f, index: i })}
                    title="Edit">
                <span><CustomFace file={f} /></span>
                <button className="web-delete icon" title="Delete from the radio"
                        onClick={(e) => {
                          e.stopPropagation()
                          setCustomFaces(customFaces.filter((_, j) => j !== i))
                        }}>{symbol.close}</button>
              </span>
              <figcaption>{f.name}</figcaption>
            </figure>
          ))}
          <button className="web-thumb web-add" onClick={() => setEditing({
            file: { format: 1, name: '', background: { colour: '#000000' }, elements: [] }, index: -1,
          })}>
            <span><span className="icon">{symbol.plus}</span></span>
            <figcaption>New face</figcaption>
          </button>
        </div>
      </section>

      {editing && (
        <FaceEditor key={editing.index} start={editing.file} onCancel={() => setEditing(null)}
                    onSave={(file) => {
                      setCustomFaces(editing.index < 0 ? [...customFaces, file]
                        : customFaces.map((f, i) => (i === editing.index ? file : f)))
                      setEditing(null)
                    }} />
      )}

      <section>
        <div className="web-row">
          <h2>Recordings</h2>
          <span className="web-dim">From the radio's Recorder, as WAV files</span>
        </div>
        {recordings.length ? (
          <div className="web-recordings">
            {recordings.map((r) => (
              <div key={r.id} className="web-recording">
                <span>{r.when}</span>
                <span className="web-dim">{duration(r.seconds)} · {size(r.bytes)}</span>
                <a className="web-secondary web-download" href="#" download={`recording-${r.id}.wav`}
                   onClick={(e) => e.preventDefault()}>
                  <span className="icon">{symbol.download}</span> Download
                </a>
                <button className="web-delete icon" title="Delete from the radio"
                        onClick={() => setRecordings(recordings.filter((x) => x !== r))}>{symbol.close}</button>
              </div>
            ))}
          </div>
        ) : (
          <p className="web-dim">Nothing recorded yet.</p>
        )}
      </section>

      <section>
        <div className="web-row">
          <h2>Favourite stations</h2>
          <span className="web-dim">They're the ★ list in Internet Radio</span>
        </div>
        <div className="web-stations">
          {stations.map((st, i) => (
            <div key={st.url} className="web-station">
              <span className="web-dim web-station-n">{i + 1}</span>
              <div className="web-station-text">
                <div>{st.name}</div>
                <div className="web-dim">{st.url}</div>
              </div>
              <button className="web-delete icon" title="Remove"
                      onClick={() => setStations(stations.filter((x) => x !== st))}>{symbol.close}</button>
            </div>
          ))}
        </div>
        <div className="web-add-station">
          <input className="web-input" placeholder="Name" value={stName} onChange={(e) => setStName(e.target.value)} />
          <input className="web-input web-url" placeholder="Stream address: http://…" value={stUrl}
                 onChange={(e) => setStUrl(e.target.value)} />
          <button className="web-primary" disabled={!stName || !/^https?:\/\//.test(stUrl)}
                  onClick={() => { setStations([...stations, { name: stName, url: stUrl }]); setStName(''); setStUrl('') }}>
            Add
          </button>
        </div>
      </section>
    </div>
  )
}

function Header({ connected = false }: { connected?: boolean }) {
  return (
    <header className="web-header">
      <span className="web-logo">DIY Radio</span>
      <span className="web-dim">
        {connected ? <><span className="web-dot" /> Connected to diy-radio.local</> : 'diy-radio.local'}
      </span>
    </header>
  )
}

// Drag to move, slider to zoom; the picture always covers the square
function Cropper({ picked, room, onCancel, onSaved }: {
  picked: Picked
  room: boolean
  onCancel: () => void
  onSaved: (name: string) => void
}) {
  const cover = crop / Math.min(picked.w, picked.h)
  const [zoom, setZoom] = useState(1)
  const w = picked.w * cover * zoom
  const h = picked.h * cover * zoom
  const clamp = (v: number, size: number) => Math.min(0, Math.max(crop - size, v))
  const [pos, setPos] = useState({ x: (crop - w) / 2, y: (crop - h) / 2 })
  const x = clamp(pos.x, w)
  const y = clamp(pos.y, h)
  const drag = useRef<{ px: number; py: number; x: number; y: number } | null>(null)
  const [progress, setProgress] = useState<number | null>(null)
  const [name, setName] = useState(picked.name.replace(/\.[^.]+$/, ''))

  // The same framing at the radio's size, for the preview
  const k = radio / crop
  const background = `url("${picked.src}") ${x * k}px ${y * k}px / ${w * k}px ${h * k}px no-repeat, #000`

  const upload = () => {
    // Stand-in for the real upload: 4 KB chunks, each acknowledged by the radio
    setProgress(0)
    const timer = setInterval(() => setProgress((p) => {
      if (p === null || p >= 100) {
        clearInterval(timer)
        onSaved(name || 'Picture')
        return p
      }
      return p + 5
    }), 60)
  }

  return (
    <section className="web-crop">
      <div className="web-row">
        <h2>Frame it</h2>
        <span className="web-dim">{picked.name} · drag to move, slide to zoom</span>
      </div>
      <div className="web-crop-body">
        <div>
          <div className="web-frame"
               onPointerDown={(e) => {
                 e.currentTarget.setPointerCapture(e.pointerId)
                 drag.current = { px: e.clientX, py: e.clientY, x, y }
               }}
               onPointerMove={(e) => {
                 const d = drag.current
                 if (d) setPos({ x: d.x + e.clientX - d.px, y: d.y + e.clientY - d.py })
               }}
               onPointerUp={() => { drag.current = null }}>
            <img src={picked.src} alt="" draggable={false}
                 style={{ width: w, height: h, transform: `translate(${x}px, ${y}px)` }} />
          </div>
          <label className="web-zoom">
            <span className="icon">{symbol.minus}</span>
            <input type="range" min={1} max={3} step={0.01} value={zoom}
                   onChange={(e) => setZoom(Number(e.target.value))} />
            <span className="icon">{symbol.plus}</span>
          </label>
        </div>

        <div className="web-preview">
          <div className="web-preview-screen"><Home background={background} /></div>
          <span className="web-dim">How Home will look</span>
        </div>
      </div>

      <div className="web-actions">
        <label className="web-name">
          Name <input value={name} maxLength={20} onChange={(e) => setName(e.target.value)} />
        </label>
        {progress === null ? (
          <>
            <button className="web-secondary" onClick={onCancel}>Cancel</button>
            <button className="web-primary" disabled={!room} onClick={upload}>
              <span className="icon">{symbol.upload}</span> Send to the radio
            </button>
          </>
        ) : (
          <div className="web-progress"><span style={{ width: `${progress}%` }} /> <em>{progress}%</em></div>
        )}
      </div>
      {!room && <p className="web-warn">The radio is full: delete a picture first.</p>}
    </section>
  )
}
