// On-screen keyboard (src/ui/text_input.cpp):
// - Phone layout, QWERTZ: ten letters across the full width, so letter keys get
//   ~43 px instead of ~34 (the mode keys, Backspace and Enter no longer sit
//   in the letter rows), and 28 px letters instead of 20
// - Shift and Backspace beside the bottom letter row, 123 / space / OK below
// - Numbers and symbols on two more layers, for Wi-Fi passwords
// - The pressed key pops up big above the finger (already on the device)
// - Hold a letter for its accents: č ć for c, š for s, ž for z, đ for d
// - Gone: the ← → cursor keys and the keyboard-hide key (Cancel does that)
//
// "Now" is the current LVGL keyboard, for comparison.
import { useRef, useState } from "react";
import { symbol } from "../icons";
import "./keyboard.css";

type Layer = "letters" | "numbers" | "symbols";
type Shift = "off" | "once" | "lock";

// Special keys; anything else types itself
const SHIFT = "shift";
const BACK = "back";
const MODE = "mode"; // 123, #+=, 123 or ABC, see modeKey()
const MODE2 = "mode2";
const SPACE = "space";
const OK = "ok";

const layers: Record<Layer, string[][]> = {
  letters: [
    ["q", "w", "e", "r", "t", "z", "u", "i", "o", "p"],
    ["a", "s", "d", "f", "g", "h", "j", "k", "l"],
    [SHIFT, "y", "x", "c", "v", "b", "n", "m", BACK],
    [MODE, ",", SPACE, ".", OK],
  ],
  numbers: [
    ["1", "2", "3", "4", "5", "6", "7", "8", "9", "0"],
    ["-", "/", ":", ";", "(", ")", "&", "@", '"'],
    [MODE2, ".", ",", "?", "!", "'", "_", BACK],
    [MODE, SPACE, OK],
  ],
  symbols: [
    ["[", "]", "{", "}", "#", "%", "^", "*", "+", "="],
    ["\\", "|", "~", "<", ">", "€", "£", "$", "•"],
    [MODE2, ".", ",", "?", "!", "'", "_", BACK],
    [MODE, SPACE, OK],
  ],
};

// Held letter -> its accents (all in the device's Latin-1 + Extended-A fonts)
const accents: Record<string, string[]> = {
  a: ["á", "ä", "ą"],
  c: ["č", "ć"],
  d: ["đ", "ď"],
  e: ["é", "ě", "ę", "ë"],
  i: ["í"],
  l: ["ł", "ľ"],
  n: ["ň", "ń", "ñ"],
  o: ["ó", "ö", "ô"],
  r: ["ř"],
  s: ["š", "ś", "ß"],
  t: ["ť"],
  u: ["ú", "ů", "ü"],
  y: ["ý"],
  z: ["ž", "ź", "ż"],
};

const kHoldMs = 450;

function modeLabel(key: string, layer: Layer) {
  if (key === MODE) return layer === "letters" ? "123" : "ABC";
  return layer === "numbers" ? "#+=" : "123";
}

type Bubble = {
  left: number;
  top: number;
  width: number;
  height: number;
  text: string;
};
type AccentStrip = {
  left: number;
  top: number;
  letter: string;
  options: string[];
};

export default function Keyboard({
  start = "letters",
  title = "Alarm name",
  password = false,
}: {
  start?: Layer | "accents" | "pressed";
  title?: string;
  password?: boolean;
}) {
  const [text, setText] = useState(
    start === "accents"
      ? "Hrvatski radio Spli"
      : start === "pressed"
        ? "Mornin"
        : "",
  );
  const [layer, setLayer] = useState<Layer>(
    start === "numbers" || start === "symbols" ? start : "letters",
  );
  // Names start with a capital, as on a phone; passwords don't
  const [shift, setShift] = useState<Shift>(
    password || start === "accents" || start === "pressed" ? "off" : "once",
  );
  const [bubble, setBubble] = useState<Bubble | null>(null);
  const [strip, setStrip] = useState<AccentStrip | null>(null);
  const [showText, setShowText] = useState(!password);
  const kbRef = useRef<HTMLDivElement>(null);
  const holdTimer = useRef<number>(0);
  const lastShiftTap = useRef(0);
  const opened = useRef(false);

  const upper = shift !== "off";
  const label = (key: string) =>
    layer === "letters" && upper ? key.toUpperCase() : key;

  const type = (s: string) => {
    setText((t) => t + s);
    if (shift === "once") setShift("off");
  };

  const press = (key: string, el: HTMLElement) => {
    if (key.length > 2) return; // special keys: no bubble
    const h = el.offsetHeight;
    const w = Math.max(el.offsetWidth + 28, 64);
    const kbWidth = kbRef.current?.offsetWidth ?? 480;
    const left = Math.min(
      Math.max(el.offsetLeft + el.offsetWidth / 2 - w / 2, 4),
      kbWidth - w - 4,
    );
    setBubble({
      left,
      top: el.offsetTop - h,
      width: w,
      height: h * 2,
      text: label(key),
    });
    const options = layer === "letters" ? accents[key] : undefined;
    if (options) {
      holdTimer.current = window.setTimeout(() => {
        setBubble(null);
        const stripW = options.length * 52 + 8;
        setStrip({
          left: Math.min(
            Math.max(el.offsetLeft + el.offsetWidth / 2 - stripW / 2, 4),
            kbWidth - stripW - 4,
          ),
          top: el.offsetTop - h - 8,
          letter: key,
          options: options.map((o) => (upper ? o.toUpperCase() : o)),
        });
      }, kHoldMs);
    }
  };

  const release = (key: string, at: number) => {
    window.clearTimeout(holdTimer.current);
    setBubble(null);
    if (strip) return; // the accent strip stays open for a pick
    switch (key) {
      case SHIFT: {
        // Tap: next letter upper case. Double tap: caps lock.
        if (at - lastShiftTap.current < 350) setShift("lock");
        else setShift(shift === "off" ? "once" : "off");
        lastShiftTap.current = at;
        break;
      }
      case BACK:
        setText((t) => t.slice(0, -1));
        break;
      case MODE:
        setLayer(layer === "letters" ? "numbers" : "letters");
        break;
      case MODE2:
        setLayer(layer === "numbers" ? "symbols" : "numbers");
        break;
      case SPACE:
        type(" ");
        break;
      case OK:
        break;
      default:
        type(label(key));
    }
  };

  // "Key pressed" opens with g held, "Accents" with c held long enough for its strip
  const heldKey = start === "pressed" ? "g" : start === "accents" ? "c" : null;
  const holdAtStart = (el: HTMLElement | null) => {
    if (!heldKey || opened.current || !el) return;
    opened.current = true;
    setTimeout(() => press(heldKey, el));
  };

  const keyContent = (key: string) => {
    switch (key) {
      case SHIFT:
        return (
          <span className={`icon${shift === "lock" ? " kb-lock" : ""}`}>
            {symbol.up}
          </span>
        );
      case BACK:
        return <span className="icon">{symbol.backspace}</span>;
      case OK:
        return <span className="icon">{symbol.ok}</span>;
      case SPACE:
        return <span className="kb-space-label">space</span>;
      case MODE:
      case MODE2:
        return modeLabel(key, layer);
      default:
        return label(key);
    }
  };

  return (
    <div className="kb-screen">
      <div className="kb-top">
        <h2>{title}</h2>
        <button className="kb-cancel">Cancel</button>
      </div>
      <div className="kb-fieldrow">
        <div className="kb-field">
          {showText ? text : "•".repeat(text.length)}
          <span className="kb-cursor" />
        </div>
        {password && (
          <button
            className="kb-eye icon"
            onClick={() => setShowText(!showText)}
          >
            {showText ? symbol.eyeClose : symbol.eyeOpen}
          </button>
        )}
      </div>

      <div
        className="kb"
        ref={kbRef}
        onPointerLeave={() => {
          setBubble(null);
          window.clearTimeout(holdTimer.current);
        }}
      >
        {layers[layer].map((row, r) => (
          <div
            key={r}
            className={`kb-row kb-row-${r}${layer === "letters" && r === 1 ? " kb-indent" : ""}`}
          >
            {row.map((key, i) => {
              const special = [SHIFT, BACK, MODE, MODE2, OK].includes(key);
              return (
                <button
                  key={`${key}${i}`}
                  ref={key === heldKey ? holdAtStart : undefined}
                  className={
                    `kb-key${special ? " kb-special" : ""}${key === OK ? " kb-ok" : ""}` +
                    `${key === SPACE ? " kb-spacebar" : ""}${key === SHIFT && shift !== "off" ? " kb-on" : ""}` +
                    `${[SHIFT, BACK, MODE2].includes(key) ? " kb-wide" : ""}${key === MODE ? " kb-mode" : ""}`
                  }
                  onPointerDown={(e) => press(key, e.currentTarget)}
                  onPointerUp={(e) => release(key, e.timeStamp)}
                >
                  {keyContent(key)}
                </button>
              );
            })}
          </div>
        ))}

        {bubble && (
          <div
            className="kb-bubble"
            style={{
              left: bubble.left,
              top: bubble.top,
              width: bubble.width,
              height: bubble.height,
            }}
          >
            {bubble.text}
          </div>
        )}
        {strip && (
          <div
            className="kb-strip"
            style={{ left: strip.left, top: strip.top }}
          >
            {strip.options.map((o) => (
              <button
                key={o}
                onClick={() => {
                  type(o);
                  setStrip(null);
                }}
              >
                {o}
              </button>
            ))}
          </div>
        )}
      </div>
    </div>
  );
}
