// Recordings on the card (the Recorder app, the upload page): samples and formatting

export type Recording = { id: number; when: string; seconds: number; bytes: number }

export const sampleRecordings: Recording[] = [
  { id: 3, when: 'Tue 3 Nov, 15:42', seconds: 134, bytes: 134 * 32000 },
  { id: 2, when: 'Mon 2 Nov, 22:10', seconds: 2412, bytes: 2412 * 32000 },
  { id: 1, when: 'Sun 1 Nov, 09:03', seconds: 47, bytes: 47 * 192000 },
]

export const duration = (s: number) =>
  s >= 3600 ? `${Math.floor(s / 3600)}:${String(Math.floor(s / 60) % 60).padStart(2, '0')}:${String(s % 60).padStart(2, '0')}`
    : `${Math.floor(s / 60)}:${String(s % 60).padStart(2, '0')}`

export const size = (bytes: number) =>
  bytes >= 1024 * 1024 * 1024 ? `${(bytes / 1024 ** 3).toFixed(1)} GB`
    : bytes >= 1024 * 1024 ? `${(bytes / 1024 ** 2).toFixed(1)} MB` : `${Math.round(bytes / 1024)} KB`
