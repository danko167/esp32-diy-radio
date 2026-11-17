// src/ui/wallpaper.cpp's designs: x, y and radius as fractions of the screen,
// colour, strength. The device renders its own; these are close.
type Blob = [number, number, number, string, number];
export const wallpapers: { name: string; blobs: Blob[] }[] = [
  { name: "Black", blobs: [] },
  {
    name: "Aurora",
    blobs: [
      [0.15, 0.05, 0.6, "#00c8a0", 0.85],
      [0.85, 0.15, 0.55, "#6a3de8", 0.9],
      [0.5, -0.1, 0.45, "#0a7aff", 0.6],
    ],
  },
  {
    name: "Sunset",
    blobs: [
      [0.2, 1, 0.6, "#ff5e3a", 0.85],
      [0.85, 0.95, 0.55, "#c21f7a", 0.9],
      [0.55, 1.15, 0.5, "#ffa030", 0.7],
    ],
  },
  {
    name: "Ocean",
    blobs: [
      [0, 0.65, 0.7, "#0040b0", 0.9],
      [1, 0.3, 0.6, "#0090c8", 0.7],
      [0.6, 1.05, 0.5, "#281488", 0.8],
    ],
  },
  {
    name: "Nebula",
    blobs: [
      [0.3, 0.35, 0.45, "#b030c0", 0.8],
      [0.72, 0.68, 0.5, "#3040d0", 0.8],
      [0.88, 0.12, 0.3, "#ff4080", 0.5],
    ],
  },
  {
    name: "Ember",
    blobs: [
      [0.5, 1.08, 0.7, "#d03010", 0.9],
      [0.12, 0.85, 0.35, "#ff8020", 0.5],
      [0.9, 0.9, 0.3, "#a01040", 0.5],
    ],
  },
];

export const wallpaperBackground = (blobs: Blob[]) =>
  [
    ...blobs.map(([x, y, r, colour, strength]) => {
      const alpha = Math.round(strength * 255)
        .toString(16)
        .padStart(2, "0");
      return `radial-gradient(ellipse ${r * 100}% ${r * 100}% at ${x * 100}% ${y * 100}%, ${colour}${alpha}, transparent)`;
    }),
    "#000",
  ].join(", ");
