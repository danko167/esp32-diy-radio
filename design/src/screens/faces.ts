// The clock screen faces (ClockFaces.tsx draws them)
export type Face = 'digital' | 'analog' | 'split' | 'night' | 'segment'

export const faces: { face: Face; name: string }[] = [
  { face: 'digital', name: 'Digital' },
  { face: 'analog', name: 'Analog' },
  { face: 'split', name: 'Split' },
  { face: 'night', name: 'Night' },
  { face: 'segment', name: 'Segment' },
]

// Night and Segment come in red or green (Settings > Clock face)
export type NightColour = 'red' | 'green'
export const isNightFace = (face: Face) => face === 'night' || face === 'segment'
