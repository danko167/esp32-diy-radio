// The accent colour (theme::kAccent on the device), picked in Settings >
// Display. Only this colour changes; the rest of the palette stays.
// Bright ones only: accent buttons (Play, Save) put black text on it.

export const accents = [
  { name: 'Sky', colour: '#5ac8fa' },  // the default
  { name: 'Mint', colour: '#63e6e2' },
  { name: 'Green', colour: '#30d158' },
  { name: 'Yellow', colour: '#ffd60a' },
  { name: 'Orange', colour: '#ff9f0a' },
  { name: 'Red', colour: '#ff453a' },
  { name: 'Pink', colour: '#ff375f' },
  { name: 'Purple', colour: '#bf5af2' },
] as const

const key = 'accent'

// Sets --accent for every screen, and remembers it across reloads
export function applyAccent(colour: string) {
  document.documentElement.style.setProperty('--accent', colour)
  localStorage.setItem(key, colour)
}

export function savedAccent(): string {
  return localStorage.getItem(key) ?? accents[0].colour
}
