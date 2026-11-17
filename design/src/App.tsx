import { useEffect, useState } from "react";
import "./app.css";
import Alarms from "./screens/Alarms";
import Battery from "./screens/Battery";
import ClockFace from "./screens/ClockFaces";
import Home from "./screens/Home";
import Recorder from "./screens/Recorder";
import Level from "./screens/Level";
import ControlCenter from "./screens/ControlCenter";
import LockScreen from "./screens/LockScreen";
import InternetRadio from "./screens/InternetRadio";
import Keyboard from "./screens/Keyboard";
import Settings from "./screens/Settings";
import Weather from "./screens/Weather";
import UploadSite from "./web/UploadSite";

// The picker, in the order the radio has them: "Group / Screen" entries are
// listed under the group's heading, and " — " names a state of a screen.
// The address picks one, e.g. #Home / First tiles.
const screens = {
  "Home / First tiles": () => <Home />,
  "Home / More tiles": () => <Home startPage={1} />,
  "Home / Upload on": () => <Home uploading />,
  "Home / On USB, full": () => <Home power="full" />,
  "Home / Battery low": () => <Home power="low" />,
  "Home / Recording": () => <Home startPage={2} recording />,
  "Home / Radio playing": () => <Home playing />,
  "Control centre / Open": () => <ControlCenter start="open" />,
  "Control centre / Wi-Fi off": () => <ControlCenter start="open" wifiOff />,
  "Control centre / Pull down (try it)": () => <ControlCenter />,

  "Internet Radio / Now Playing": () => <InternetRadio start="now" />,
  "Internet Radio / Stations": () => <InternetRadio start="stations" />,
  "Internet Radio / Recently heard": () => <InternetRadio start="songs" />,
  "Internet Radio / Popular — connecting": () => <InternetRadio start="stations" tab="Popular" wifi="connecting" />,
  "Internet Radio / Popular — no Wi-Fi": () => <InternetRadio start="stations" tab="Popular" wifi="off" />,
  "Internet Radio / Search — connecting": () => <InternetRadio start="stations" tab="Search" wifi="connecting" />,
  "Internet Radio / Search — no Wi-Fi": () => <InternetRadio start="stations" tab="Search" wifi="off" />,

  "Alarms / Alarms": () => <Alarms />,
  "Alarms / Edit alarm": () => <Alarms start="edit" />,
  "Alarms / Sound": () => <Alarms start="sound" />,
  "Alarms / Ringing": () => <Alarms start="ringing" />,

  "Weather / Now": () => <Weather />,
  "Weather / Next 24 hours": () => <Weather start="hourly" />,
  "Weather / Next days": () => <Weather start="days" />,
  "Weather / Details": () => <Weather start="details" />,
  "Weather / Now — no Wi-Fi": () => <Weather start="offline" />,
  "Weather / Now — loading": () => <Weather start="loading" />,

  // The tiles' order, each page's own pages after it
  "Settings / Settings": () => <Settings />,
  "Settings / Wi-Fi": () => <Settings start="wifi" />,
  "Settings / Display": () => <Settings start="display" />,
  "Settings / Home tiles": () => <Settings start="homeTiles" />,
  "Settings / Upload pictures": () => <Settings start="upload" />,
  "Settings / Upload pictures — waiting": () => <Settings start="uploadWaiting" />,
  "Settings / Upload pictures — receiving": () => <Settings start="uploadReceiving" />,
  "Settings / Upload pictures — connecting": () => <Settings start="upload" wifi="connecting" />,
  "Settings / Upload pictures — no Wi-Fi": () => <Settings start="upload" wifi="off" />,
  "Settings / Clock screen": () => <Settings start="clockScreen" />,
  "Settings / Clock face": () => <Settings start="clockFace" />,
  "Settings / Date & time": () => <Settings start="dateTime" />,
  "Settings / Date & time — set by hand": () => <Settings start="setTime" />,
  "Settings / Weather location": () => <Settings start="location" />,
  "Settings / About": () => <Settings start="about" />,
  "Recorder / Ready": () => <Recorder />,
  "Recorder / Recording": () => <Recorder start="recording" />,
  "Recorder / Recordings": () => <Recorder start="list" />,
  "Recorder / Playing one": () => <Recorder start="playing" />,
  "Level / Level": () => <Level />,
  "Level / Tilted": () => <Level tilt={{ x: -4.2, y: 1.6 }} />,
  "Level / Moving (demo)": () => <Level moving />,

  "Clock screen / Digital": () => <ClockFace face="digital" />,
  "Clock screen / Analog — with seconds": () => <ClockFace face="analog" showSeconds />,
  "Clock screen / Split": () => <ClockFace face="split" />,
  "Clock screen / Night": () => <ClockFace face="night" />,
  "Clock screen / Segment": () => <ClockFace face="segment" />,
  "Clock screen / Segment — green": () => <ClockFace face="segment" nightColour="green" />,
  "Clock screen / Locked — tapped": () => <LockScreen start="shown" />,

  // On battery only, over whatever is open
  "Battery / Low — over the clock screen": () => <Battery />,
  "Battery / Empty — switching off": () => <Battery start="empty" />,

  "Keyboard / Letters": () => <Keyboard />,
  "Keyboard / Letters — key pressed": () => <Keyboard start="pressed" />,
  "Keyboard / Letters — accents (hold c)": () => <Keyboard start="accents" title="Search stations" />,
  "Keyboard / Numbers": () => <Keyboard start="numbers" title="Password for HomeNet" password />,
  "Keyboard / Symbols": () => <Keyboard start="symbols" title="Password for HomeNet" password />,

  // Served by the radio to a computer's browser: full width, no radio frame
  "On a computer / Upload page — code": () => <UploadSite start="locked" />,
  "On a computer / Upload page": () => <UploadSite />,
  "On a computer / Upload page — framing": () => <UploadSite start="cropping" />,
  "On a computer / Face editor": () => <UploadSite start="faceEditor" />,
} as const;

type ScreenName = keyof typeof screens;

const names = Object.keys(screens) as ScreenName[];
const groupOf = (s: string) => (s.includes(" / ") ? s.split(" / ")[0] : null);
const labelOf = (s: string) => s.split(" / ").pop();

const fromHash = (): ScreenName => {
  const h = decodeURIComponent(location.hash.slice(1));
  return h in screens ? (h as ScreenName) : "Home / First tiles";
};

export default function App() {
  const [name, setNameState] = useState<ScreenName>(fromHash);
  const [zoom, setZoom] = useState(1.25);
  const setName = (s: ScreenName) => {
    location.hash = s;
    setNameState(s);
  };
  // Follow links between screens (e.g. Home's upload icon)
  useEffect(() => {
    const follow = () => setNameState(fromHash());
    window.addEventListener("hashchange", follow);
    return () => window.removeEventListener("hashchange", follow);
  }, []);
  const Screen = screens[name];
  const onComputer = name.startsWith("On a computer / ");

  return (
    <div className="playground">
      <aside className="picker">
        <h1>DIY Radio</h1>
        {names.map((s, i) => {
          const group = groupOf(s);
          return (
            <div key={s} className="picker-entry">
              {group && group !== groupOf(names[i - 1] ?? "") && (
                <h3>{group}</h3>
              )}
              <button
                className={`${s === name ? "selected" : ""}${group ? " grouped" : ""}`}
                onClick={() => setName(s)}
              >
                {labelOf(s)}
              </button>
            </div>
          );
        })}
        <label>
          Zoom {zoom}×
          <input
            type="range"
            min={1}
            max={2}
            step={0.25}
            value={zoom}
            onChange={(e) => setZoom(Number(e.target.value))}
          />
        </label>
        <p className="hint">
          The frame is the real 480 × 480 screen. At 1× it's the device's own
          pixels.
        </p>
      </aside>

      <main className={onComputer ? "computer" : ""}>
        {onComputer ? (
          <Screen />
        ) : (
          <div className="device" style={{ zoom }}>
            <Screen />
          </div>
        )}
      </main>
    </div>
  );
}
