import * as z from 'zod'

import { type UntrustedInput } from './untrusted.js'

/** Validate the public coordinate contract before native IPC; C++ validates it again. */
export const NativeGeolocationSchema = z.preprocess(
  (input: UntrustedInput) => {
    if (input === null || typeof input !== 'object' || Array.isArray(input)) return input
    const value = input as Record<string, UntrustedInput>
    for (const name of ['longitude', 'latitude', 'accuracy'] as const) {
      const coordinate = value[name]
      if (name === 'accuracy' && coordinate === undefined) continue
      if (typeof coordinate !== 'number' || !Number.isFinite(coordinate)) {
        const received =
          coordinate === undefined ? 'undefined' : coordinate === null ? 'null' : typeof coordinate
        throw new Error(`geolocation.${name}: expected float, got ${received}`)
      }
      if (name === 'longitude' && (coordinate < -180 || coordinate > 180))
        throw new Error('geolocation.longitude: precondition -180 <= LONGITUDE <= 180 failed.')
      if (name === 'latitude' && (coordinate < -90 || coordinate > 90))
        throw new Error('geolocation.latitude: precondition -90 <= LATITUDE <= 90 failed.')
      if (name === 'accuracy' && coordinate < 0)
        throw new Error('geolocation.accuracy: precondition 0 <= ACCURACY failed.')
    }
    return input
  },
  z
    .strictObject({
      latitude: z.number().finite().min(-90).max(90),
      longitude: z.number().finite().min(-180).max(180),
      accuracy: z.number().finite().min(0).optional(),
    })
    .nullable(),
)
