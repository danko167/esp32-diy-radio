// The face editor on the upload page (web/upload.html on the radio): the face
// at 3/4 size, drag its pieces into place, style them on the right, then
// send the file (and its picture and font) to the card. Changing a face
// later opens it here again.
import { useEffect, useRef, useState } from 'react'
import type { ReactNode } from 'react'
import { elementName, facePictures } from '../faceFile'
import type { ColourRef, FaceElement, FaceFile, FontRef } from '../faceFile'
import CustomFace from '../screens/CustomFace'
import './upload-site.css'
import './face-editor.css'

const scale = 0.75  // the face on this page: 360 px for the radio's 480
type Kind = FaceElement['kind']

const newElement: Record<Kind, () => FaceElement> = {
  time: () => ({ kind: 'time', seconds: false, x: 240, y: 170, align: 'centre', font: 'clock140', colour: 'text' }),
  date: () => ({ kind: 'date', format: 'long', x: 240, y: 300, align: 'centre', font: 'ui30', colour: 'dim' }),
  alarm: () => ({ kind: 'alarm', x: 240, y: 345, align: 'centre', font: 'ui30', colour: 'dim' }),
  weather: () => ({ kind: 'weather', words: true, x: 240, y: 390, align: 'centre', font: 'ui30', colour: 'accent' }),
  text: () => ({ kind: 'text', text: 'Good night', x: 240, y: 60, align: 'centre', font: 'ui22', colour: 'dim' }),
  analog: () => ({
    kind: 'analog', cx: 240, cy: 220, radius: 150, ticks: 'hours', tickColour: 'dim',
    hour: { length: 82, width: 10, colour: 'text' }, minute: { length: 124, width: 6, colour: 'text' }, dot: 'accent',
  }),
}

const builtInFonts: { font: FontRef; name: string; digitsOnly?: boolean }[] = [
  { font: 'clock140', name: 'Clock 140', digitsOnly: true },
  { font: 'clock96', name: 'Clock 96', digitsOnly: true },
  { font: 'ui50', name: 'Text 50' },
  { font: 'ui30', name: 'Text 30' },
  { font: 'ui22', name: 'Text 22' },
  { font: 'ui20', name: 'Text 20' },
]

// Average brightness of the background, 0..1: a bright face lit all night uses
// more battery and wears the AMOLED, so the editor says so
function useBrightness(file: FaceFile) {
  const bg = file.background
  const src = 'picture' in bg ? facePictures[bg.picture] : null
  // A picture is measured once it has loaded, from a 32 x 32 copy
  const [measured, setMeasured] = useState<{ src: string; level: number } | null>(null)
  useEffect(() => {
    if (!src) return
    const img = new Image()
    img.onload = () => {
      const c = document.createElement('canvas')
      c.width = c.height = 32
      const g = c.getContext('2d')!
      g.drawImage(img, 0, 0, 32, 32)
      const d = g.getImageData(0, 0, 32, 32).data
      let sum = 0
      for (let i = 0; i < d.length; i += 4) sum += 0.2126 * d[i] + 0.7152 * d[i + 1] + 0.0722 * d[i + 2]
      setMeasured({ src, level: sum / (32 * 32) / 255 })
    }
    img.src = src
  }, [src])
  if ('colour' in bg) {
    const n = parseInt(bg.colour.slice(1), 16)
    return (0.2126 * (n >> 16) + 0.7152 * ((n >> 8) & 255) + 0.0722 * (n & 255)) / 255
  }
  return measured?.src === src ? measured.level : 0
}

export default function FaceEditor({ start, onSave, onCancel }: {
  start: FaceFile
  onSave: (file: FaceFile) => void
  onCancel: () => void
}) {
  const [file, setFile] = useState<FaceFile>(start)
  const [selected, setSelected] = useState(0)
  const [fonts, setFonts] = useState<string[]>([])
  const [adding, setAdding] = useState(false)
  const drag = useRef<{ px: number; py: number; x: number; y: number } | null>(null)
  const brightness = useBrightness(file)
  const element = file.elements[selected] as FaceElement | undefined

  const update = (changes: Partial<FaceElement>) =>
    setFile({ ...file, elements: file.elements.map((e, i) => (i === selected ? { ...e, ...changes } as FaceElement : e)) })

  const startDrag = (index: number, ev: React.PointerEvent) => {
    setSelected(index)
    const e = file.elements[index]
    ;(ev.currentTarget as HTMLElement).closest('.fe-canvas')!.setPointerCapture(ev.pointerId)
    drag.current = e.kind === 'analog' ? { px: ev.clientX, py: ev.clientY, x: e.cx, y: e.cy }
      : { px: ev.clientX, py: ev.clientY, x: e.x, y: e.y }
  }
  const moveDrag = (ev: React.PointerEvent) => {
    const d = drag.current
    if (!d || !element) return
    const x = Math.round(d.x + (ev.clientX - d.px) / scale)
    const y = Math.round(d.y + (ev.clientY - d.py) / scale)
    update(element.kind === 'analog' ? { cx: x, cy: y } : { x, y })
  }

  const addFont = async (f: File) => {
    const name = f.name.replace(/\.[^.]+$/, '')
    await new FontFace(name, await f.arrayBuffer()).load().then((face) => document.fonts.add(face))
    setFonts([...fonts, name])
    // The radio gets only the glyphs this face uses, converted to LVGL's binary font format
    if (element && element.kind !== 'analog') update({ font: { file: name, size: 120 } })
  }
  const addPicture = (f: File) => {
    const reader = new FileReader()
    reader.onload = () => {
      const name = f.name.replace(/\.[^.]+$/, '')
      facePictures[name] = String(reader.result)
      setFile({ ...file, background: { picture: name } })
    }
    reader.readAsDataURL(f)
  }

  return (
    <section className="web-crop fe">
      <div className="web-row">
        <h2>{start.name ? `Edit “${start.name}”` : 'New clock face'}</h2>
        <span className="web-dim">Drag the pieces into place; pick one to style it</span>
      </div>

      <div className="fe-body">
        <div className="fe-canvas" onPointerMove={moveDrag} onPointerUp={() => { drag.current = null }}
             onPointerDown={(ev) => { if (ev.target === ev.currentTarget) setSelected(-1) }}>
          <div className="fe-scaled"><CustomFace file={file} selected={selected} onPointerDown={startDrag} /></div>
        </div>

        <div className="fe-panel">
          <Group title="Background">
            <div className="fe-choices">
              <button className={'colour' in file.background && file.background.colour === '#000000' ? 'on' : ''}
                      onClick={() => setFile({ ...file, background: { colour: '#000000' } })}>Black</button>
              <label className={'colour' in file.background && file.background.colour !== '#000000' ? 'on' : ''}>
                Colour
                <input type="color" value={'colour' in file.background ? file.background.colour : '#000000'}
                       onChange={(e) => setFile({ ...file, background: { colour: e.target.value } })} />
              </label>
              <label className={'picture' in file.background ? 'on' : ''}>
                Picture…
                <input type="file" accept="image/*" hidden onChange={(e) => e.target.files?.[0] && addPicture(e.target.files[0])} />
              </label>
            </div>
            {brightness > 0.25 && (
              <p className="web-warn">
                A bright face: lit all night it uses more battery and wears the screen. A dark background
                is kinder; the radio also dims it with the clock screen brightness.
              </p>
            )}
          </Group>

          <Group title="Pieces">
            <div className="fe-list">
              {file.elements.map((e, i) => (
                <button key={i} className={i === selected ? 'on' : ''} onClick={() => setSelected(i)}>
                  {elementName[e.kind]}{e.kind === 'date' && <span className="web-dim"> · {e.format}</span>}
                </button>
              ))}
              {adding ? (
                (Object.keys(newElement) as Kind[]).map((k) => (
                  <button key={k} className="fe-add" onClick={() => {
                    setFile({ ...file, elements: [...file.elements, newElement[k]()] })
                    setSelected(file.elements.length)
                    setAdding(false)
                  }}>+ {elementName[k]}</button>
                ))
              ) : (
                <button className="fe-add" onClick={() => setAdding(true)}>+ Add</button>
              )}
            </div>
          </Group>

          {element && (
            <Group title={elementName[element.kind]} action={
              <button className="fe-remove" onClick={() => {
                setFile({ ...file, elements: file.elements.filter((_, i) => i !== selected) })
                setSelected(-1)
              }}>Remove</button>
            }>
              {element.kind === 'analog' ? (
                <>
                  <Slider label="Size" value={element.radius} min={60} max={230}
                          onChange={(radius) => update({ radius })} />
                  <Choice label="Marks" value={element.ticks}
                          options={[['none', 'None'], ['quarters', '12, 3, 6, 9'], ['hours', 'Hours'], ['minutes', 'Minutes']]}
                          onChange={(ticks) => update({ ticks: ticks as typeof element.ticks })} />
                  <Colour label="Marks" value={element.tickColour} onChange={(tickColour) => update({ tickColour })} />
                  <Colour label="Hands" value={element.hour.colour}
                          onChange={(c) => update({ hour: { ...element.hour, colour: c }, minute: { ...element.minute, colour: c } })} />
                  <Slider label="Hand width" value={element.minute.width} min={2} max={16}
                          onChange={(w) => update({ hour: { ...element.hour, width: Math.round(w * 1.6) }, minute: { ...element.minute, width: w } })} />
                  <Colour label="Centre" value={element.dot ?? 'text'} onChange={(dot) => update({ dot })} />
                  <Toggle label="Seconds hand (with Show seconds on)" on={!!element.second}
                          onChange={(on) => update({ second: on ? { length: 135, width: 2, colour: 'accent' } : undefined })} />
                </>
              ) : (
                <>
                  {element.kind === 'date' && (
                    <Choice label="Shows" value={element.format}
                            options={[['long', 'Sunday, 1 November'], ['short', 'Sun 1 Nov'], ['day', 'Sunday'], ['date', '1 November'], ['numeric', '1. 11.']]}
                            onChange={(format) => update({ format: format as typeof element.format })} />
                  )}
                  {element.kind === 'text' && (
                    <label className="fe-field">Text <input className="fe-input" value={element.text} maxLength={30}
                                                            onChange={(e) => update({ text: e.target.value })} /></label>
                  )}
                  {element.kind === 'time' && <Toggle label="Seconds (with Show seconds on)" on={element.seconds}
                                                      onChange={(seconds) => update({ seconds })} />}
                  {element.kind === 'weather' && <Toggle label="Words (“Partly cloudy”)" on={element.words}
                                                         onChange={(words) => update({ words })} />}
                  <FontPick element={element} fonts={fonts} onChange={(font) => update({ font })} onUpload={addFont} />
                  <Colour label="Colour" value={element.colour} onChange={(colour) => update({ colour })} />
                  <Choice label="Line up" value={element.align}
                          options={[['left', 'Left'], ['centre', 'Centre'], ['right', 'Right']]}
                          onChange={(align) => update({ align: align as 'left' })} />
                </>
              )}
            </Group>
          )}
        </div>
      </div>

      <div className="web-actions">
        <label className="web-name">
          Name <input value={file.name} maxLength={20} onChange={(e) => setFile({ ...file, name: e.target.value })} />
        </label>
        <button className="web-secondary" onClick={onCancel}>Cancel</button>
        <button className="web-primary" disabled={!file.name} onClick={() => onSave(file)}>Send to the radio</button>
      </div>
    </section>
  )
}

function Group({ title, action, children }: { title: string; action?: ReactNode; children: ReactNode }) {
  return (
    <div className="fe-group">
      <div className="fe-group-title"><span>{title}</span>{action}</div>
      {children}
    </div>
  )
}

function Slider({ label, value, min, max, onChange }: {
  label: string; value: number; min: number; max: number; onChange: (v: number) => void
}) {
  return (
    <label className="fe-field">{label}
      <input type="range" min={min} max={max} value={value} onChange={(e) => onChange(Number(e.target.value))} />
    </label>
  )
}

function Choice({ label, value, options, onChange }: {
  label: string; value: string; options: [string, string][]; onChange: (v: string) => void
}) {
  return (
    <label className="fe-field">{label}
      <select className="fe-input" value={value} onChange={(e) => onChange(e.target.value)}>
        {options.map(([v, name]) => <option key={v} value={v}>{name}</option>)}
      </select>
    </label>
  )
}

function Toggle({ label, on, onChange }: { label: string; on: boolean; onChange: (on: boolean) => void }) {
  return (
    <label className="fe-toggle"><input type="checkbox" checked={on} onChange={(e) => onChange(e.target.checked)} />{label}</label>
  )
}

// The radio's own colours first (the accent follows Settings > Display), then any
function Colour({ label, value, onChange }: { label: string; value: ColourRef; onChange: (c: ColourRef) => void }) {
  const named: [ColourRef, string][] = [['accent', 'var(--accent)'], ['text', 'var(--text)'], ['dim', 'var(--text-dim)']]
  const custom = !named.some(([c]) => c === value)
  return (
    <div className="fe-field">{label}
      <span className="fe-swatches">
        {named.map(([c, css]) => (
          <button key={c} title={c} className={value === c ? 'on' : ''} style={{ background: css }} onClick={() => onChange(c)} />
        ))}
        <input type="color" className={custom ? 'on' : ''} value={custom ? value : '#ff9f0a'}
               onChange={(e) => onChange(e.target.value)} title="Any colour" />
      </span>
    </div>
  )
}

function FontPick({ element, fonts, onChange, onUpload }: {
  element: Extract<FaceElement, { font: FontRef }>
  fonts: string[]
  onChange: (f: FontRef) => void
  onUpload: (f: File) => void
}) {
  const current = typeof element.font === 'string' ? element.font : `file:${element.font.file}`
  // The clock fonts have digits only: fine for the time, not for words
  const usable = builtInFonts.filter((f) => element.kind === 'time' || !f.digitsOnly)
  return (
    <>
      <label className="fe-field">Font
        <select className="fe-input" value={current} onChange={(e) => {
          const v = e.target.value
          onChange(v.startsWith('file:') ? { file: v.slice(5), size: typeof element.font === 'string' ? 60 : element.font.size } : v as FontRef)
        }}>
          {usable.map((f) => <option key={String(f.font)} value={String(f.font)}>{f.name}</option>)}
          {fonts.map((f) => <option key={f} value={`file:${f}`}>{f}</option>)}
        </select>
      </label>
      {typeof element.font !== 'string' && (
        <Slider label="Size" value={element.font.size} min={16} max={200}
                onChange={(size) => onChange({ file: (element.font as { file: string }).file, size })} />
      )}
      <label className="fe-upload">
        Use your own font (.ttf, .otf)…
        <input type="file" accept=".ttf,.otf,.woff" hidden onChange={(e) => e.target.files?.[0] && onUpload(e.target.files[0])} />
      </label>
    </>
  )
}
