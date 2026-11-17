// Alarms (src/ui/screen_alarms.cpp, and the ringing panel in src/ui/ui.cpp):
// - List: the next alarm up top, days drawn as M T W T F S S, + in the header
// - Editor: time and days first, Save pinned at the bottom; snooze as chips
// - Sound: volume, fade in and the sound list together on their own page
// - Ringing: a huge Snooze for the half-asleep, Stop below it
import { useState } from 'react'
import { AppScreen, BigSlider, Card, Hint, Pill, RoundButton, Roller, Section, SectionRow, Switch } from '../components'
import { symbol } from '../icons'
import './alarms.css'

type Alarm = {
  id: number
  enabled: boolean
  hour: number
  minute: number
  days: number  // bit 0 = Monday ... bit 6 = Sunday, none = rings once
  label: string
  sound: string
  volume: number
  gentle: boolean
  snooze: number
}

const weekdays = 0x1f
const weekend = 0x60
const everyDay = 0x7f
const dayLetters = ['M', 'T', 'W', 'T', 'F', 'S', 'S']
const dayShort = ['Mo', 'Tu', 'We', 'Th', 'Fr', 'Sa', 'Su']
const dayNames = ['Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday', 'Sunday']
const snoozeOptions = [5, 9, 10, 15, 20]

// What the alarm can ring with (tones::list() plus favourite stations)
const sounds = [
  { group: 'TONES', icon: symbol.bell, names: ['Chime', 'Beeps', 'Sunrise'] },
  { group: 'FROM THE SD CARD', icon: symbol.sdCard, names: ['Morning birds', 'Ocean waves'] },
  { group: 'RADIO, YOUR FAVOURITES', icon: symbol.wifi, names: ['Rock Radio', 'HR 1 Prvi program', 'Radio Paradise'] },
]

const startAlarms: Alarm[] = [
  { id: 1, enabled: true, hour: 6, minute: 45, days: weekdays, label: 'Work', sound: 'Rock Radio', volume: 60, gentle: true, snooze: 9 },
  { id: 2, enabled: true, hour: 9, minute: 0, days: weekend, label: '', sound: 'Sunrise', volume: 40, gentle: true, snooze: 10 },
  { id: 3, enabled: false, hour: 14, minute: 30, days: 0, label: 'Dentist', sound: 'Beeps', volume: 80, gentle: false, snooze: 5 },
]

const newAlarm = (): Alarm => ({
  id: 0, enabled: true, hour: 7, minute: 0, days: 0, label: '', sound: 'Chime', volume: 60, gentle: true, snooze: 9,
})

// The mockup's "now": Sunday 17:42
const nowDay = 6
const nowMinutes = 17 * 60 + 42

const two = (n: number) => String(n).padStart(2, '0')
// formatTime(): "07:30"
const timeText = (h: number, m: number) => `${two(h)}:${two(m)}`

// alarms::describeDays()
function describeDays(days: number) {
  if (days === 0) return 'Once'
  if (days === everyDay) return 'Every day'
  if (days === weekdays) return 'Weekdays'
  if (days === weekend) return 'Weekends'
  return dayShort.filter((_, i) => days & (1 << i)).join(', ')
}

// When it rings next: days from today and minutes from now
function nextRing(a: Alarm) {
  const at = a.hour * 60 + a.minute
  for (let d = 0; d <= 7; d++) {
    if (d === 0 && at <= nowMinutes) continue
    if (a.days && !(a.days & (1 << ((nowDay + d) % 7)))) continue
    return { day: d, minutes: d * 24 * 60 + at - nowMinutes }
  }
  return null
}

// "in 13 h 3 min", like untilText()
function untilText(minutes: number) {
  if (minutes < 60) return `in ${minutes} min`
  if (minutes < 24 * 60) return `in ${Math.floor(minutes / 60)} h ${minutes % 60} min`
  return `in ${Math.floor(minutes / (24 * 60))} days`
}

const dayText = (d: number) => (d === 0 ? 'Today' : d === 1 ? 'Tomorrow' : dayNames[(nowDay + d) % 7])

type View = 'list' | 'edit' | 'sound' | 'ringing'

export default function Alarms({ start = 'list' }: { start?: View }) {
  const [alarms, setAlarms] = useState(startAlarms)
  const [draft, setDraft] = useState<Alarm>(start === 'list' ? newAlarm() : startAlarms[0])
  const [snoozedUntil, setSnoozedUntil] = useState<string | null>(null)
  const [view, setView] = useState<View>(start)

  const edit = (a: Alarm) => { setDraft(a); setView('edit') }
  const save = () => {
    setAlarms((list) => {
      const saved = draft.id ? draft : { ...draft, id: Math.max(0, ...list.map((a) => a.id)) + 1 }
      const rest = list.filter((a) => a.id !== saved.id)
      return [...rest, saved].sort((x, y) => x.hour * 60 + x.minute - (y.hour * 60 + y.minute))
    })
    setView('list')
  }
  const remove = () => { setAlarms((list) => list.filter((a) => a.id !== draft.id)); setView('list') }
  const toggle = (id: number, on: boolean) =>
    setAlarms((list) => list.map((a) => (a.id === id ? { ...a, enabled: on } : a)))

  if (view === 'ringing') {
    return <Ringing a={draft}
                    onSnooze={() => {
                      const at = draft.hour * 60 + draft.minute + draft.snooze
                      setSnoozedUntil(timeText(Math.floor(at / 60) % 24, at % 60))
                      setView('list')
                    }}
                    onStop={() => {
                      setSnoozedUntil(null)
                      if (!draft.days) toggle(draft.id, false)  // a one-off is done once it has rung
                      setView('list')
                    }} />
  }
  if (view === 'sound') return <SoundPage draft={draft} setDraft={setDraft} onBack={() => setView('edit')} />
  if (view === 'edit') {
    return <Editor draft={draft} setDraft={setDraft} onSave={save} onDelete={remove} onBack={() => setView('list')}
                   onSound={() => setView('sound')} />
  }
  return <List alarms={alarms} snoozedUntil={snoozedUntil} onCancelSnooze={() => setSnoozedUntil(null)}
               onEdit={edit} onAdd={() => edit(newAlarm())} onToggle={toggle} />
}

function List({ alarms, snoozedUntil, onCancelSnooze, onEdit, onAdd, onToggle }: {
  alarms: Alarm[]
  snoozedUntil: string | null
  onCancelSnooze: () => void
  onEdit: (a: Alarm) => void
  onAdd: () => void
  onToggle: (id: number, on: boolean) => void
}) {
  const next = alarms
    .filter((a) => a.enabled)
    .map((a) => ({ a, ring: nextRing(a)! }))
    .sort((x, y) => x.ring.minutes - y.ring.minutes)[0]

  return (
    <AppScreen title="Alarms" action={<RoundButton icon={symbol.plus} size={54} onClick={onAdd} />}>
      {snoozedUntil ? (
        <div className="next-alarm snoozed">
          <span className="next-icon icon">{symbol.moon}</span>
          <div className="next-text">
            <div className="next-caption">SNOOZED</div>
            <div>Rings again at {snoozedUntil}</div>
          </div>
          <Pill label="Cancel" size="small" onClick={onCancelSnooze} />
        </div>
      ) : next ? (
        <div className="next-alarm">
          <span className="next-icon icon">{symbol.bell}</span>
          <div className="next-text">
            <div className="next-caption">NEXT ALARM</div>
            <div>{dayText(next.ring.day)} {timeText(next.a.hour, next.a.minute)}
              <span className="dim"> · {untilText(next.ring.minutes)}</span></div>
          </div>
        </div>
      ) : (
        <div className="next-alarm off">
          <span className="next-icon icon">{symbol.bell}</span>
          <div className="next-text">No alarms on</div>
        </div>
      )}

      {alarms.map((a) => (
        <Card key={a.id} onClick={() => onEdit(a)}>
          <div className="alarm-text">
            <div className={`alarm-time${a.enabled ? '' : ' off'}`}>{timeText(a.hour, a.minute)}</div>
            <div className="alarm-detail">
              {a.days ? (
                <span className="day-letters">
                  {dayLetters.map((l, i) => <span key={i} className={a.days & (1 << i) ? 'on' : ''}>{l}</span>)}
                </span>
              ) : <span>Once</span>}
              {a.label && <span className="alarm-label">{a.label}</span>}
            </div>
          </div>
          <Switch on={a.enabled} onChange={(on) => onToggle(a.id, on)} />
        </Card>
      ))}

      {alarms.length === 0 && (
        <div className="al-empty">
          <div>No alarms yet.</div>
          <Pill icon={symbol.plus} label="Add an alarm" size="big" onClick={onAdd} />
        </div>
      )}
    </AppScreen>
  )
}

type DraftProps = { draft: Alarm; setDraft: (a: Alarm) => void }

function Editor({ draft, setDraft, onSave, onDelete, onBack, onSound }: DraftProps & {
  onSave: () => void
  onDelete: () => void
  onBack: () => void
  onSound: () => void
}) {
  const set = (change: Partial<Alarm>) => setDraft({ ...draft, ...change })
  const ring = nextRing(draft)!
  const presets: [string, number][] = [['Weekdays', weekdays], ['Weekends', weekend], ['Every day', everyDay]]

  return (
    <AppScreen caption="ALARMS" title={draft.id ? 'Edit alarm' : 'New alarm'} onBack={onBack}
               footer={
                 <div className="editor-buttons">
                   {draft.id > 0 && <RoundButton icon={symbol.trash} size={58} onClick={onDelete} />}
                   <Pill icon={symbol.ok} label="Save" size="big" primary grow onClick={onSave} />
                 </div>
               }>
      <div className="time-pick">
        <Roller value={draft.hour} count={24} onChange={(hour) => set({ hour })} />
        <span className="time-colon">:</span>
        <Roller value={draft.minute} count={60} onChange={(minute) => set({ minute })} />
      </div>
      <div className="al-ring-hint">Rings {dayText(ring.day).toLowerCase()}, {untilText(ring.minutes)}</div>

      <SectionRow title="REPEAT">
        <span className="accent">{describeDays(draft.days)}</span>
      </SectionRow>
      <div className="day-buttons">
        {dayShort.map((d, i) => (
          <button key={d} className={draft.days & (1 << i) ? 'on' : ''} onClick={() => set({ days: draft.days ^ (1 << i) })}>
            {d}
          </button>
        ))}
      </div>
      <div className="al-chips">
        {presets.map(([name, days]) => (
          <button key={name} className={draft.days === days ? 'on' : ''}
                  onClick={() => set({ days: draft.days === days ? 0 : days })}>{name}</button>
        ))}
      </div>

      <Card title="Sound" onClick={onSound}>
        <span className="al-value">{draft.sound} · {draft.volume}%</span>
      </Card>
      {/* Opens the keyboard on the device */}
      <Card title="Name" onClick={() => set({ label: draft.label ? '' : 'Wake up' })}>
        <span className={draft.label ? 'al-value' : 'al-value dim'}>{draft.label || 'None'}</span>
      </Card>

      <SectionRow title="SNOOZE">
        <span className="dim small">minutes</span>
      </SectionRow>
      <div className="al-chips five">
        {snoozeOptions.map((m) => (
          <button key={m} className={draft.snooze === m ? 'on' : ''} onClick={() => set({ snooze: m })}>{m}</button>
        ))}
      </div>
    </AppScreen>
  )
}

function SoundPage({ draft, setDraft, onBack }: DraftProps & { onBack: () => void }) {
  const set = (change: Partial<Alarm>) => setDraft({ ...draft, ...change })
  return (
    <AppScreen caption={draft.id ? 'EDIT ALARM' : 'NEW ALARM'} title="Sound" onBack={onBack}>
      <Section>VOLUME</Section>
      <BigSlider value={draft.volume} min={10} icon={symbol.volumeMax} onChange={(volume) => set({ volume })} />
      <Card title="Fade in over a minute"><Switch on={draft.gentle} onChange={(gentle) => set({ gentle })} /></Card>

      {sounds.map((g) => (
        <div key={g.group} className="sound-group">
          <Section>{g.group}</Section>
          {g.names.map((name) => (
            <Card key={name} onClick={() => set({ sound: name })}>
              <span className="icon dim">{g.icon}</span>
              <span className={`sound-name${draft.sound === name ? ' accent' : ''}`}>{name}</span>
              {draft.sound === name && <span className="icon accent">{symbol.ok}</span>}
            </Card>
          ))}
        </div>
      ))}
      <Hint>Tap a sound to hear it. If a station can't play, the chime rings instead.</Hint>
    </AppScreen>
  )
}

// Full screen over everything, like showRinging()
function Ringing({ a, onSnooze, onStop }: {
  a: Alarm
  onSnooze: () => void
  onStop: () => void
}) {
  return (
    <div className="ringing">
      <div className="ring-label"><span className="icon">{symbol.bell}</span> {a.label || 'Alarm'}</div>
      <div className="ring-clock">{timeText(a.hour, a.minute)}</div>
      <div className="ring-sound">{a.sound}{a.gentle ? ' · fading in' : ''}</div>
      <button className="ring-snooze" onClick={onSnooze}>Snooze {a.snooze} min</button>
      <button className="ring-stop" onClick={onStop}>Stop</button>
      <div className="ring-hint">Side buttons snooze · middle button stops</div>
    </div>
  )
}
