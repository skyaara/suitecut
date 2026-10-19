import * as z from 'zod'

export interface NativeCookieFilter {
  name?: string | RegExp | undefined
  domain?: string | RegExp | undefined
  path?: string | RegExp | undefined
}

const value = z.union([z.string().max(8192), z.instanceof(RegExp)])
const schema = z.strictObject({
  name: value.optional(),
  domain: value.optional(),
  path: value.optional(),
})

/** Marshal only selected filter fields; native V8 owns matching and native deletes own the result. */
export function nativeCookieFilter(
  options: NativeCookieFilter,
): Record<string, string | { sourceJSON: string; flags: string }> {
  const input = schema.parse(options)
  const result: Record<string, string | { sourceJSON: string; flags: string }> = {}
  for (const [field, filter] of Object.entries(input)) {
    if (filter === undefined) continue
    if (typeof filter === 'string') result[field] = filter
    else {
      const sourceJSON = JSON.stringify(filter.source)
      if (new TextEncoder().encode(sourceJSON).length > 32768)
        throw new Error('Native cookie regular expression capacity exceeded')
      result[field] = { sourceJSON, flags: filter.flags }
    }
  }
  return result
}
