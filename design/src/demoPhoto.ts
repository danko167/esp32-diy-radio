// A stand-in for a photo someone uploads: a lake at dusk, drawn as SVG so
// the playground needs no image files
const svg = `<svg xmlns="http://www.w3.org/2000/svg" width="1200" height="800" viewBox="0 0 1200 800">
<defs>
<linearGradient id="sky" x1="0" y1="0" x2="0" y2="1">
<stop offset="0" stop-color="#1b2a5a"/><stop offset="0.55" stop-color="#e0785a"/><stop offset="0.7" stop-color="#f4b26a"/>
</linearGradient>
<linearGradient id="lake" x1="0" y1="0" x2="0" y2="1">
<stop offset="0" stop-color="#f0a466"/><stop offset="1" stop-color="#1a2440"/>
</linearGradient>
</defs>
<rect width="1200" height="560" fill="url(#sky)"/>
<circle cx="760" cy="470" r="60" fill="#ffd39a"/>
<path d="M0 520 L180 380 L330 470 L520 300 L700 470 L860 360 L1040 480 L1200 400 L1200 560 L0 560 Z" fill="#2a2240"/>
<rect y="560" width="1200" height="240" fill="url(#lake)"/>
<rect x="700" y="575" width="120" height="6" fill="#ffd39a" opacity="0.7"/>
<rect x="680" y="600" width="160" height="5" fill="#ffd39a" opacity="0.5"/>
</svg>`

export const demoPhoto = `data:image/svg+xml;utf8,${encodeURIComponent(svg)}`
