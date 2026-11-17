// A clock face as a file: what the upload page's face editor saves to the
// microSD card (as JSON, plus its picture and font files next to it), and what
// the radio's face renderer draws. Data only, no code: a face can place and
// style the clock's pieces, not add new behaviour (that takes firmware).
//
// Coordinates are the radio's 480 x 480 pixels. x is where the element's
// alignment point sits (its left edge, centre or right edge), y its top.

import { demoPhoto } from './demoPhoto'

// Pictures by name: on the radio, files on the card next to the face (the
// playground keeps uploaded ones here as data URLs)
export const facePictures: Record<string, string> = { lake: demoPhoto }

export type FontRef =
  | 'clock96' | 'clock140'                 // digits, colon, degree sign
  | 'ui20' | 'ui22' | 'ui30' | 'ui50'     // text
  | { file: string; size: number }        // uploaded: the editor converts the glyphs it needs

// A colour, or one of the radio's: its accent (Settings > Display) and text colours
export type ColourRef = string | 'accent' | 'text' | 'dim'

export type Align = 'left' | 'centre' | 'right'

type Placed = { x: number; y: number; align: Align; font: FontRef; colour: ColourRef }

export type FaceElement =
  | ({ kind: 'time'; seconds: boolean } & Placed)
  // long: "Sunday, 1 November", short: "Sun 1 Nov", day: "Sunday", date: "1 November", numeric: "1. 11."
  | ({ kind: 'date'; format: 'long' | 'short' | 'day' | 'date' | 'numeric' } & Placed)
  | ({ kind: 'alarm' } & Placed)                                   // bell and time; hidden with no alarm
  | ({ kind: 'weather'; words: boolean } & Placed)                 // icon, temperature, and maybe "Partly cloudy"
  | ({ kind: 'text'; text: string } & Placed)                      // fixed text
  | {
      kind: 'analog'
      cx: number
      cy: number
      radius: number
      ticks: 'none' | 'quarters' | 'hours' | 'minutes'
      tickColour: ColourRef
      hour: Hand
      minute: Hand
      second?: Hand                                                // only drawn with "Show seconds" on
      dot?: ColourRef                                              // in the centre
    }

export type Hand = { length: number; width: number; colour: ColourRef }

export type FaceFile = {
  format: 1
  name: string
  // A picture is a 480 x 480 RGB565 file on the card, like a wallpaper
  background: { colour: string } | { picture: string }
  elements: FaceElement[]
}

export const elementName: Record<FaceElement['kind'], string> = {
  time: 'Time', date: 'Date', alarm: 'Alarm', weather: 'Weather', text: 'Text', analog: 'Analog clock',
}

// Two examples, as an uploaded face might look
export const exampleFaces: FaceFile[] = [
  {
    format: 1,
    name: 'Corner',
    background: { colour: '#000000' },
    elements: [
      { kind: 'date', format: 'day', x: 36, y: 238, align: 'left', font: 'ui30', colour: 'accent' },
      { kind: 'time', seconds: false, x: 28, y: 270, align: 'left', font: 'clock140', colour: 'text' },
      { kind: 'date', format: 'date', x: 36, y: 412, align: 'left', font: 'ui22', colour: 'dim' },
      { kind: 'alarm', x: 444, y: 40, align: 'right', font: 'ui22', colour: 'dim' },
      { kind: 'weather', words: false, x: 36, y: 34, align: 'left', font: 'ui30', colour: 'accent' },
    ],
  },
  {
    format: 1,
    name: 'Lake dial',
    background: { picture: 'lake' },
    elements: [
      {
        kind: 'analog', cx: 240, cy: 210, radius: 150, ticks: 'quarters', tickColour: 'text',
        hour: { length: 80, width: 10, colour: 'text' },
        minute: { length: 125, width: 6, colour: 'text' },
        dot: 'accent',
      },
      { kind: 'date', format: 'short', x: 240, y: 404, align: 'centre', font: 'ui30', colour: 'text' },
    ],
  },
]
