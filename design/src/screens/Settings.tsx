// Settings (src/ui/screen_settings.cpp, and screen_wifi, screen_place and
// screen_upload for their pages): one tile per row, each opening its own page.
// Choices you can see instead of dropdowns, and everything about the clock
// screen (ambient) in one place.
import { useState } from "react";
import type { ReactNode } from "react";
import { AppScreen, BigSlider, Card, Hint, Pill, Roller, RoundButton, Section, SectionRow, Switch } from "../components";
import { symbol, weather } from "../icons";
import ClockFace from "./ClockFaces";
import CustomFace from "./CustomFace";
import { exampleFaces } from "../faceFile";
import { faces, isNightFace } from "./faces";
import { accents, applyAccent, savedAccent } from "../accent";
import { demoPhoto } from "../demoPhoto";
import { wallpaperBackground, wallpapers } from "../wallpapers";
import type { Face, NightColour } from "./faces";
import "./settings.css";

export type SettingsPage =
  | "main"
  | "wifi"
  | "display"
  | "clockScreen"
  | "dateTime"
  | "location"
  | "about"
  | "homeTiles"
  | "clockFace"
  | "upload"
  | "uploadWaiting"
  | "uploadReceiving"
  | "setTime"; // Date & time with its "Set by hand" dialog open

// wifi::status(), for what needs the internet: on, a saved network still
// coming up (or back), or none to use (not set up, wrong password, not found)
export type WiFiState = "connected" | "connecting" | "off";


type AmbientMode = "clock" | "weather" | "off";
const ambientModes: { mode: AmbientMode; label: string; summary: string }[] = [
  { mode: "clock", label: "Clock", summary: "Clock" },
  { mode: "weather", label: "+ Weather", summary: "Clock + weather" },
  { mode: "off", label: "Off", summary: "Screen off" },
];

// Seconds of inactivity before the clock screen, 0 = never
const timeouts: [number, string][] = [
  [30, "30 s"],
  [60, "1 min"],
  [120, "2 min"],
  [300, "5 min"],
  [600, "10 min"],
  [0, "Never"],
];

type Network = { ssid: string; secured: boolean; rssi: number };
const networks: Network[] = [
  { ssid: "HomeNet", secured: true, rssi: -48 },
  { ssid: "TP-Link_3F2A", secured: true, rssi: -67 },
  { ssid: "Neighbours", secured: true, rssi: -74 },
  { ssid: "Caffe Bar Free", secured: false, rssi: -83 },
];

// Like the Wi-Fi page: signal strength shown by dimming the symbol
const signalOpacity = (rssi: number) =>
  rssi > -60 ? 1 : rssi > -70 ? 0.8 : rssi > -80 ? 0.5 : 0.3;
const signalWord = (rssi: number) =>
  rssi > -60
    ? "strong"
    : rssi > -70
      ? "good"
      : rssi > -80
        ? "weak"
        : "very weak";

function useSettings(wifiState: WiFiState, autoStart: boolean, lightSensor: boolean) {
  const [wifi, setWifi] = useState<string | null>(wifiState === "off" ? null : "HomeNet");
  // Wi-Fi off: the saved networks stay, and it joins one again when it's back on
  const [wifiOn, setWifiOn] = useState(true);
  // Up to 5, most recently used first, like a phone: the strongest one in range joins
  const [saved, setSaved] = useState(["HomeNet", "My phone", "Office"]);
  const [brightness, setBrightness] = useState(70);
  // Follows the room (the BH1750 light sensor): both brightness settings are
  // then for a normally lit room, dimmer in the dark, brighter in daylight
  const [autoBrightness, setAutoBrightness] = useState(autoStart && lightSensor);
  const [wallpaper, setWallpaper] = useState(2);
  const [ambientMode, setAmbientMode] = useState<AmbientMode>("weather");
  const [ambientTimeout, setAmbientTimeout] = useState(60);
  const [ambientBrightness, setAmbientBrightness] = useState(15);
  const [ambientSeconds, setAmbientSeconds] = useState(false);
  // Slide to unlock on the clock screen; the buttons and a ringing alarm bypass it
  const [screenLock, setScreenLock] = useState(false);
  const [place, setPlace] = useState({
    name: "Zagreb",
    region: "Grad Zagreb, Croatia",
    automatic: true,
  });
  const [perfOverlay, setPerfOverlay] = useState(false);
  const [homeTiles, setHomeTiles] = useState(homeTileList.map((t) => t.label));
  const [clockFace, setClockFace] = useState<Face>("digital");
  // A face from the card (made on the upload page) instead, by name
  const [cardFace, setCardFace] = useState<string | null>(null);
  // Night and Segment: red or green
  const [nightColour, setNightColour] = useState<NightColour>("red");
  const [accent, setAccentState] = useState(savedAccent);
  const setAccent = (colour: string) => {
    applyAccent(colour);
    setAccentState(colour);
  };
  return {
    wifi,
    setWifi,
    wifiOn,
    setWifiOn,
    saved,
    setSaved,
    wifiConnecting: wifiState === "connecting",
    brightness,
    setBrightness,
    autoBrightness,
    setAutoBrightness,
    lightSensor,
    wallpaper,
    setWallpaper,
    ambientMode,
    setAmbientMode,
    ambientTimeout,
    setAmbientTimeout,
    ambientBrightness,
    setAmbientBrightness,
    ambientSeconds,
    setAmbientSeconds,
    screenLock,
    setScreenLock,
    place,
    setPlace,
    perfOverlay,
    setPerfOverlay,
    homeTiles,
    setHomeTiles,
    clockFace,
    cardFace,
    setCardFace,
    setClockFace,
    nightColour,
    setNightColour,
    accent,
    setAccent,
    time: "17:42",
  };
}

type S = ReturnType<typeof useSettings>;
type Go = (page: SettingsPage) => void;

// `wifi`: how Wi-Fi is doing (the upload page needs it)
// `autoBrightness`: starts with it on; `lightSensor`: false for a radio
// without the BH1750 (the switch can't go on)
export default function Settings({
  start = "main",
  wifi = "connected",
  autoBrightness = false,
  lightSensor = true,
}: {
  start?: SettingsPage;
  wifi?: WiFiState;
  autoBrightness?: boolean;
  lightSensor?: boolean;
}) {
  const s = useSettings(wifi, autoBrightness, lightSensor);
  const [page, setPage] = useState<SettingsPage>(start);
  switch (page) {
    case "wifi":
      return <WiFi s={s} go={setPage} />;
    case "display":
      return <Display s={s} go={setPage} />;
    case "clockScreen":
      return <ClockScreen s={s} go={setPage} />;
    case "dateTime":
    case "setTime":
      return <DateTime s={s} go={setPage} setting={page === "setTime"} />;
    case "location":
      return <Location s={s} go={setPage} />;
    case "about":
      return <About s={s} go={setPage} />;
    case "homeTiles":
      return <HomeTiles s={s} go={setPage} />;
    case "clockFace":
      return <ClockFacePage s={s} go={setPage} />;
    case "upload":
    case "uploadWaiting":
    case "uploadReceiving":
      return (
        <UploadPage
          s={s}
          go={setPage}
          start={page === "upload" ? "off" : page === "uploadWaiting" ? "waiting" : "receiving"}
        />
      );
    default:
      return <Main s={s} go={setPage} />;
  }
}

function Main({ s, go }: { s: S; go: Go }) {
  const faceName = s.cardFace ?? faces.find((f) => f.face === s.clockFace)!.name;
  const mode =
    s.ambientMode === "off"
      ? "Screen off"
      : s.ambientMode === "weather"
        ? `${faceName} + weather`
        : faceName;
  const after = timeouts.find(([t]) => t === s.ambientTimeout)![1];
  const list: {
    page: SettingsPage;
    icon: string;
    colour: string;
    name: string;
    value: string;
  }[] = [
    {
      page: "wifi",
      icon: symbol.wifi,
      colour: "#64d2ff",
      name: "Wi-Fi",
      value: !s.wifiOn ? "Off" : s.wifi ?? "Not connected",
    },
    {
      page: "display",
      icon: symbol.sun,
      colour: "#ffd60a",
      name: "Display",
      value: `${s.brightness}% · ${wallpapers[s.wallpaper].name}`,
    },
    {
      page: "clockScreen",
      icon: symbol.moon,
      colour: "#bf5af2",
      name: "Clock screen",
      // A non-breaking space keeps "1 min" together when it wraps
      value: s.ambientTimeout ? `${mode}, ${after.replace(" ", " ")}` : "Never",
    },
    {
      page: "dateTime",
      icon: symbol.clock,
      colour: "#ff9f0a",
      name: "Date & time",
      value: s.time,
    },
    {
      page: "location",
      icon: symbol.gps,
      colour: "#30d158",
      name: "Weather location",
      value: s.place.name,
    },
    {
      page: "about",
      icon: symbol.info,
      colour: "#8a8a90",
      name: "About",
      value: "Memory, developer",
    },
  ];

  return (
    <AppScreen title="Settings">
      <div className="set-rows">
        {list.map((t) => (
          <button key={t.page} className="set-tile" onClick={() => go(t.page)}>
            <span
              className="set-icon icon"
              style={{ color: t.colour, background: `${t.colour}33` }}
            >
              {t.icon}
            </span>
            <span className="set-text">
              <span className="set-name">{t.name}</span>
              <span className="set-value">{t.value}</span>
            </span>
            <span className="icon dim set-chevron">{symbol.right}</span>
          </button>
        ))}
      </div>
    </AppScreen>
  );
}

// openSubpage(): every page says it belongs to Settings and goes back there
function Page({
  title,
  go,
  back = "main",
  children,
}: {
  title: string;
  go: Go;
  back?: SettingsPage;
  children: ReactNode;
}) {
  return (
    <AppScreen caption="SETTINGS" title={title} onBack={() => go(back)}>
      {children}
    </AppScreen>
  );
}


function WiFi({ s, go }: { s: S; go: Go }) {
  const current = networks.find((n) => n.ssid === s.wifi);
  return (
    <Page title="Wi-Fi" go={go}>
      <Card title="Wi-Fi">
        <Switch on={s.wifiOn} onChange={s.setWifiOn} />
      </Card>
      {!s.wifiOn && (
        <Hint>
          Off: no internet radio, weather or time from the internet. {s.wifi ?? "The saved network"} stays
          saved and joins again when Wi-Fi is back on. Also in the control centre: swipe down from the top.
        </Hint>
      )}
      {s.wifiOn && <>
      <Card vertical>
        <div className="wifi-status">
          <span className={`set-icon big icon${s.wifi ? "" : " off"}`}>
            {symbol.wifi}
          </span>
          <div className="wifi-text">
            <div className={s.wifi ? "accent" : "dim"}>
              {s.wifi ? "Connected" : "Not connected"}
            </div>
            <div className="wifi-name">{s.wifi ?? "Pick a network below"}</div>
          </div>
        </div>
        {current && (
          <>
            <div className="set-detail">
              IP 192.168.1.42 · {signalWord(current.rssi)} signal (
              {current.rssi} dBm)
            </div>
          </>
        )}
      </Card>

      {s.saved.length > 0 && <>
        <Section>{`SAVED NETWORKS (${s.saved.length} OF 5)`}</Section>
        {s.saved.map((name) => (
          <Card key={name}>
            <div className="saved-net">
              <span className={name === s.wifi ? "accent" : undefined}>{name}</span>
              <span className="set-detail">
                {name === s.wifi ? "Connected" : networks.some((n) => n.ssid === name) ? "In range" : "Not in range"}
              </span>
            </div>
            <RoundButton icon={symbol.trash} size={44} onClick={() => {
              s.setSaved(s.saved.filter((n) => n !== name));
              if (name === s.wifi) s.setWifi(null);
            }} />
          </Card>
        ))}
        <Hint>
          The strongest saved network in range joins by itself. A sixth replaces the one used longest ago.
        </Hint>
      </>}

      <SectionRow title="NETWORKS NEARBY">
        <span className="icon dim">{symbol.refresh}</span>
      </SectionRow>
      {/* The device asks for the password of a locked network first */}
      {networks.map((n) => (
        <Card key={n.ssid} onClick={() => s.setWifi(n.ssid)}>
          <span
            className={`network-name${n.ssid === s.wifi ? " accent" : ""}`}
          >
            {n.ssid}
          </span>
          {n.secured && <span className="icon dim">{symbol.lock}</span>}
          <span className="icon" style={{ opacity: signalOpacity(n.rssi) }}>
            {symbol.wifi}
          </span>
        </Card>
      ))}
      </>}
    </Page>
  );
}

function Display({ s, go }: { s: S; go: Go }) {
  return (
    <Page title="Display" go={go}>
      <Section>BRIGHTNESS</Section>
      <BigSlider
        value={s.brightness}
        min={5}
        icon={symbol.sun}
        onChange={s.setBrightness}
      />
      <Card title="Automatic brightness">
        {s.lightSensor
          ? <Switch on={s.autoBrightness} onChange={s.setAutoBrightness} />
          : <span className="dim">No sensor</span>}
      </Card>
      <Hint>
        {!s.lightSensor
          ? "Needs a BH1750 light sensor on the radio's I2C bus."
          : s.autoBrightness
            ? "Follows the room: dimmer in the dark, brighter in daylight. The bar sets it for a normally lit room."
            : "The screen follows the room, dimmer in the dark and brighter in daylight."}
      </Hint>

      <Section>WALLPAPER</Section>
      <div className="wallpapers">
        {wallpapers.map((w, i) => (
          <button
            key={w.name}
            className={`wallpaper-pick${i === s.wallpaper ? " selected" : ""}`}
            onClick={() => s.setWallpaper(i)}
          >
            <span
              className="wallpaper-thumb"
              style={{ background: wallpaperBackground(w.blobs) }}
            >
              {i === s.wallpaper && (
                <span className="icon wallpaper-check">{symbol.ok}</span>
              )}
            </span>
            <span className="wallpaper-name">{w.name}</span>
          </button>
        ))}
        {/* Uploaded from a computer, see UploadPage */}
        <button
          className={`wallpaper-pick${s.wallpaper === wallpapers.length ? " selected" : ""}`}
          onClick={() => s.setWallpaper(wallpapers.length)}
        >
          <span
            className="wallpaper-thumb"
            style={{ background: `url("${demoPhoto}") center / cover` }}
          >
            {s.wallpaper === wallpapers.length && (
              <span className="icon wallpaper-check">{symbol.ok}</span>
            )}
          </span>
          <span className="wallpaper-name">Lake</span>
        </button>
        <button className="wallpaper-pick" onClick={() => go("upload")}>
          <span className="wallpaper-thumb wallpaper-add">
            <span className="icon">{symbol.plus}</span>
          </span>
          <span className="wallpaper-name">Your picture</span>
        </button>
      </div>

      <Section>ACCENT COLOUR</Section>
      <div className="accents">
        {accents.map((a) => (
          <button
            key={a.colour}
            className={`accent-pick${a.colour === s.accent ? " selected" : ""}`}
            style={{ background: a.colour }}
            onClick={() => s.setAccent(a.colour)}
            aria-label={a.name}
          >
            {a.colour === s.accent && <span className="icon">{symbol.ok}</span>}
          </button>
        ))}
      </div>
      <Hint>
        {accents.find((a) => a.colour === s.accent)?.name}: for highlights, buttons and
        what's selected, throughout the radio.
      </Hint>

      <Section>HOME SCREEN</Section>
      <Card title="Home tiles" onClick={() => go("homeTiles")}>
        <span className="set-value-right">
          {s.homeTiles.slice(0, 3).join(", ")} first
        </span>
      </Card>
    </Page>
  );
}

// The tiles on Home v2, in the order the user picks: the first three show
// when Home opens, the rest a swipe away
const homeTileList = [
  { icon: symbol.tower, label: "FM Radio" },
  { icon: symbol.globe, label: "Web Radio" },
  { icon: symbol.bluetooth, label: "Bluetooth" },
  { icon: symbol.tint, label: "Weather" },
  { icon: symbol.bell, label: "Alarm" },
  { icon: symbol.mic, label: "Recorder" },
  { icon: symbol.ruler, label: "Level" },
  { icon: symbol.settings, label: "Settings" },
];

// Up and down buttons rather than dragging, which is fiddly on a small screen
function HomeTiles({ s, go }: { s: S; go: Go }) {
  const move = (i: number, by: number) => {
    const order = [...s.homeTiles];
    [order[i], order[i + by]] = [order[i + by], order[i]];
    s.setHomeTiles(order);
  };
  const row = (label: string, i: number) => {
    const tile = homeTileList.find((t) => t.label === label)!;
    const first = i === 0;
    const last = i === s.homeTiles.length - 1;
    return (
      <Card key={label}>
        <span className="icon tile-order-icon">{tile.icon}</span>
        <span className="tile-order-name">{label}</span>
        <RoundButton icon={symbol.up} size={46}
          className={first ? "tile-order-off" : ""}
          onClick={() => !first && move(i, -1)} />
        <RoundButton icon={symbol.down} size={46}
          className={last ? "tile-order-off" : ""}
          onClick={() => !last && move(i, 1)} />
      </Card>
    );
  };
  return (
    <Page title="Home tiles" go={go} back="display">
      {/* Three to a page, as Home shows them */}
      {Array.from({ length: Math.ceil(s.homeTiles.length / 3) }, (_, page) => (
        <div key={page}>
          <Section>{["FIRST", "SECOND", "THIRD"][page] ?? `PAGE ${page + 1}`} PAGE</Section>
          {s.homeTiles.slice(page * 3, page * 3 + 3).map((label, i) => row(label, page * 3 + i))}
        </div>
      ))}
      <Hint>The first page shows when Home opens; swipe the tiles for the rest.</Hint>
    </Page>
  );
}

function ClockScreen({ s, go }: { s: S; go: Go }) {
  const clock = s.ambientSeconds ? "17:42:05" : "17:42";
  return (
    <Page title="Clock screen" go={go}>
      <Hint>
        Shows when the radio has been left alone for a while. Tap or press a
        button to wake it.
      </Hint>

      <Section>SHOW</Section>
      <div className="modes">
        {ambientModes.map((m) => (
          <button
            key={m.mode}
            className={`mode${m.mode === s.ambientMode ? " selected" : ""}`}
            onClick={() => s.setAmbientMode(m.mode)}
          >
            <span className="mode-preview">
              {m.mode !== "off" && <span className="mode-clock">{clock}</span>}
              {m.mode === "weather" && (
                <span className="mode-weather">
                  <span className="weather-icon">
                    {weather.partlyCloudyDay}
                  </span>{" "}
                  12°
                </span>
              )}
            </span>
            <span>{m.label}</span>
          </button>
        ))}
      </div>

      {s.ambientMode !== "off" && (
        <Card title="Face" onClick={() => go("clockFace")}>
          <span className="set-value-right">
            {s.cardFace ?? faces.find((f) => f.face === s.clockFace)!.name}
          </span>
        </Card>
      )}

      <Section>START AFTER</Section>
      <div className="chips">
        {timeouts.map(([t, label]) => (
          <button
            key={t}
            className={t === s.ambientTimeout ? "selected" : ""}
            onClick={() => s.setAmbientTimeout(t)}
          >
            {label}
          </button>
        ))}
      </div>

      <Section>BRIGHTNESS</Section>
      <BigSlider
        value={s.ambientBrightness}
        min={2}
        icon={symbol.moon}
        onChange={s.setAmbientBrightness}
      />
      <Hint>
        {s.autoBrightness
          ? "For a normally lit room: in the dark it goes dimmer, never off, so the time stays readable. The screen dims to this while you drag."
          : "The screen dims to this while you drag, so you see it for real."}
      </Hint>

      <Card title="Show seconds">
        <Switch on={s.ambientSeconds} onChange={s.setAmbientSeconds} />
      </Card>

      <Card title="Lock">
        <Switch on={s.screenLock} onChange={s.setScreenLock} />
      </Card>
      <Hint>
        A tap then only shows "slide to unlock": slide it to get to Home. The buttons and a ringing alarm
        work without it.
      </Hint>
    </Page>
  );
}

// A small web server, on only when asked: a computer on the same Wi-Fi opens
// the address, types the code, and sends pictures. The browser crops and
// converts them to the radio's 480 x 480 format, so the radio only stores
// them. It stops after the picked time without activity: each page load,
// picture or deletion from the computer starts that time again.
// A new code each time it starts, so an old one is no use
const randomCode = () => String(Math.floor(1000 + Math.random() * 9000));

// The radio remembers the last one picked
const idleOptions: [number, string][] = [
  [5, "5 min"],
  [15, "15 min"],
  [30, "30 min"],
  [60, "1 h"],
];

// offlineText() in screen_upload.cpp: why Start can't be used yet
const uploadOffline = (s: S) =>
  !s.wifi
    ? "Connect to Wi-Fi first, in Settings > Wi-Fi."
    : s.wifiConnecting
      ? "Connecting to Wi-Fi…"
      : null;

function UploadPage({
  s,
  go,
  start,
}: {
  s: S;
  go: Go;
  start: "off" | "waiting" | "receiving";
}) {
  const [on, setOn] = useState(start !== "off");
  const [idle, setIdle] = useState(15);
  const [code, setCode] = useState("4817");
  const [phase, setPhase] = useState<"waiting" | "receiving" | "cancelled">(
    start === "receiving" ? "receiving" : "waiting",
  );
  const idleName = idleOptions.find(([m]) => m === idle)![1];
  const offline = uploadOffline(s);

  if (!on) {
    return (
      <Page title="Upload pictures" go={go} back="display">
        <Hint big>
          Send your own pictures from a computer on the same Wi-Fi, to use as
          wallpaper, and edit the favourite stations there. The radio is
          reachable for that only while this is on.
        </Hint>
        <Section>STOP AFTER NO ACTIVITY FOR</Section>
        <div className="chips four">
          {idleOptions.map(([m, label]) => (
            <button
              key={m}
              className={m === idle ? "selected" : ""}
              onClick={() => setIdle(m)}
            >
              {label}
            </button>
          ))}
        </div>
        {/* Disabled (40% background) while there's no Wi-Fi, the reason below it */}
        <div className={`upload-start${offline ? " disabled" : ""}`}>
          <Pill
            icon={symbol.upload}
            label="Start"
            size="big"
            primary
            wide
            onClick={() => {
              if (offline) return;
              setCode(randomCode());
              setPhase("waiting");
              setOn(true);
            }}
          />
        </div>
        {offline && <Hint big>{offline}</Hint>}
      </Page>
    );
  }

  return (
    <Page title="Upload pictures" go={go} back="display">
      <Card vertical>
        <span className="dim">1 · On a computer on this Wi-Fi, open</span>
        <span className="upload-address">diy-radio.local</span>
        {/* Some computers don't find .local names; the address always works */}
        <span className="dim small">or http://192.168.1.42</span>
      </Card>
      <Card vertical>
        <span className="dim">2 · Type this code there</span>
        <span className="upload-code">{code}</span>
      </Card>
      {phase === "receiving" && (
        <Card vertical>
          <span>Receiving lake.jpg</span>
          <span className="upload-progress">
            <span className="upload-bar"><span style={{ width: "45%" }} /></span>
            {/* Cuts the transfer; the radio drops the half-received file */}
            <RoundButton icon={symbol.close} size={42} onClick={() => setPhase("cancelled")} />
          </span>
          <span className="dim small">45% · it shows up under Wallpaper when done</span>
        </Card>
      )}
      {phase === "waiting" && (
        <div className="upload-waiting">
          <span className="icon upload-spin">{symbol.refresh}</span>
          Waiting for your computer…
        </div>
      )}
      {phase === "cancelled" && (
        <div className="upload-waiting">
          <span className="icon">{symbol.close}</span>
          Cancelled. The computer can send it again.
        </div>
      )}
      <div className="upload-footer">
        <span className="upload-countdown">
          Stops in {idle - 1}:32
          <span className="dim small">{idleName} after the last activity</span>
        </span>
        <Pill label="Stop" onClick={() => setOn(false)} />
      </div>
    </Page>
  );
}

const nightColours: [NightColour, string][] = [
  ["red", "Red"],
  ["green", "Green"],
];

// Its own page, opened from Clock screen: the faces as live previews
function ClockFacePage({ s, go }: { s: S; go: Go }) {
  return (
    <Page title="Clock face" go={go} back="clockScreen">
      <div className="faces">
        {faces.map((f) => (
          <button
            key={f.face}
            className={`face-pick${!s.cardFace && f.face === s.clockFace ? " selected" : ""}`}
            onClick={() => {
              s.setClockFace(f.face);
              s.setCardFace(null);
            }}
          >
            <span className="face-thumb">
              <ClockFace face={f.face} showWeather={s.ambientMode === "weather"}
                         showSeconds={s.ambientSeconds} nightColour={s.nightColour} />
            </span>
            <span>{f.name}</span>
          </button>
        ))}
        {/* Made on the upload page and saved on the card */}
        {exampleFaces.map((f) => (
          <button
            key={f.name}
            className={`face-pick${s.cardFace === f.name ? " selected" : ""}`}
            onClick={() => s.setCardFace(f.name)}
          >
            <span className="face-thumb">
              <CustomFace file={f} />
            </span>
            <span>{f.name}</span>
          </button>
        ))}
      </div>
      {/* Night and Segment only; the previews change with it */}
      {!s.cardFace && isNightFace(s.clockFace) && (
        <>
          <Section>COLOUR</Section>
          <div className="chips two">
            {nightColours.map(([c, label]) => (
              <button
                key={c}
                className={c === s.nightColour ? "selected" : ""}
                onClick={() => s.setNightColour(c)}
              >
                {label}
              </button>
            ))}
          </div>
        </>
      )}
      <Hint>
        Every face moves a few pixels each minute, so the screen doesn't burn in.
        Make your own on the upload page (Settings › Display › Your picture).
      </Hint>
    </Page>
  );
}

function DateTime({ s, go, setting }: { s: S; go: Go; setting: boolean }) {
  const [dialog, setDialog] = useState(setting);
  return (
    <>
    <Page title="Date & time" go={go}>
      <Card vertical>
        <div>
          <div className="dt-time">{s.time}</div>
          <div>Sunday, 1 November 2026</div>
          <div className="set-detail dt-sync">
            <span className="icon accent">{symbol.ok}</span> From the
            internet · checked at 17:02
          </div>
        </div>
      </Card>
      {/* Not a setting yet: the firmware has Central European time built in */}
      <Card title="Time zone" onClick={() => {}}>
        <span className="set-value-right">Zagreb</span>
      </Card>
      <Card title="Set by hand" onClick={() => setDialog(true)}>
        <span className="icon dim">{symbol.right}</span>
      </Card>
      <Hint>
        Only needed without Wi-Fi. Otherwise the radio sets its clock from the
        internet.
      </Hint>
    </Page>
    {dialog && <SetByHand onClose={() => setDialog(false)} />}
    </>
  );
}

// Set by hand (src/ui/time_dialog.cpp): a page like the alarm editor, over
// Date & time. The time on top in the alarm editor's rollers, the date below
// in smaller ones, Set at the bottom; back cancels. Each roller's index is the
// value; they start at the time now.
const months = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"];
const firstYear = 2025;
const numberList = (first: number, count: number, pad: boolean) =>
  Array.from({ length: count }, (_, i) => String(first + i).padStart(pad ? 2 : 1, "0"));

export function SetByHand({ onClose }: { onClose: () => void }) {
  // Sunday, 1 November 2026, 17:42, like the page behind
  const [day, setDay] = useState(0);
  const [month, setMonth] = useState(10);
  const [year, setYear] = useState(2026 - firstYear);
  const [hour, setHour] = useState(17);
  const [minute, setMinute] = useState(42);

  return (
    <div className="sbh">
      <AppScreen caption="DATE & TIME" title="Set by hand" onBack={onClose}
                 footer={<Pill icon={symbol.ok} label="Set" size="big" primary wide onClick={onClose} />}>
        <div className="sbh-time">
          <Roller value={hour} count={24} onChange={setHour} />
          <span className="sbh-colon">:</span>
          <Roller value={minute} count={60} onChange={setMinute} />
        </div>
        <div className="sbh-date">
          <Roller small value={day} labels={numberList(1, 31, false)} width={96} onChange={setDay} />
          <Roller small value={month} labels={months} width={120} onChange={setMonth} />
          <Roller small value={year} labels={numberList(firstYear, 16, false)} width={140} stops onChange={setYear} />
        </div>
      </AppScreen>
    </div>
  );
}

function Location({ s, go }: { s: S; go: Go }) {
  return (
    <Page title="Weather location" go={go}>
      <Card vertical>
        <div>
          <div className="place-name">{s.place.name}</div>
          <div className="set-detail">
            {s.place.region}
            {s.place.automatic ? " · found automatically" : ""}
          </div>
        </div>
      </Card>
      <Card
        title="Search for a place"
        onClick={() =>
          s.setPlace({
            name: "Split",
            region: "Splitsko-dalmatinska, Croatia",
            automatic: false,
          })
        }
      >
        <span className="icon accent">{symbol.search}</span>
      </Card>
      <Card
        title="Find automatically"
        onClick={() =>
          s.setPlace({
            name: "Zagreb",
            region: "Grad Zagreb, Croatia",
            automatic: true,
          })
        }
      >
        <span className="icon accent">{symbol.gps}</span>
      </Card>
      <Hint>
        Automatically means from the internet connection's location, which can
        be a nearby city.
      </Hint>
    </Page>
  );
}

function About({ s, go }: { s: S; go: Go }) {
  const rows: [string, string][] = [
    ["Firmware", "1 Nov 2026"],
    ["IP address", s.wifi ? "192.168.1.42" : "–"],
    ["Running for", "3 h 12 min"],
    ["Memory free", "44 KB RAM · 6.1 MB PSRAM"],
    ["Battery", "92% · 4.13 V · charging"],
    // The power chip's own readings: what comes in over USB-C, and how warm it is
    ["USB power", "5.04 V"],
    ["Power chip", "36 °C"],
    ["microSD card", "14.6 GB · 14.6 GB free"],
  ];
  return (
    <Page title="About" go={go}>
      <Card vertical>
        <div>
          <div className="place-name">DIY Radio</div>
          <div className="set-detail">Waveshare ESP32-S3 AMOLED 2.16″</div>
        </div>
      </Card>
      {rows.map(([name, value]) => (
        <Card key={name} title={name}>
          <span className="set-detail">{value}</span>
        </Card>
      ))}
      {/* backup::save() and restore(): restore asks for a second tap, then restarts */}
      <Section>BACKUP</Section>
      <Card title="Back up now" onClick={() => {}}>
        <span className="set-value-right">2 November, 19:49</span>
      </Card>
      <Card title="Restore" onClick={() => {}}>
        <span className="set-value-right">Restarts the radio</span>
      </Card>
      <Hint>
        On the microSD card: settings, alarms, favourites and the weather location. Not the
        Wi-Fi password.
      </Hint>

      <Section>DEVELOPER</Section>
      <Card title="Charging">
        <span className="set-detail">to 4.20 V · 500 mA · stops at 125 mA</span>
      </Card>
      <Card title="Power">
        <span className="set-detail">off below 3.10 V · USB 1500 mA</span>
      </Card>
      <Card title="Performance overlay">
        <Switch on={s.perfOverlay} onChange={s.setPerfOverlay} />
      </Card>
    </Page>
  );
}
