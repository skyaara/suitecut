/** Resolves relative URL patterns while preserving syntax that URL parsing would encode. */
export function resolveNativeURLGlob(pattern: string, baseURL?: string): string {
  if (baseURL === undefined || pattern.startsWith('*')) return pattern
  if (pattern.length > 8192) throw new Error('Native URL glob exceeds 8192 characters')
  let prefix = 'suitecutglobtoken'
  const occupied = (pattern + baseURL).toLowerCase()
  while (occupied.includes(prefix)) prefix += 'x'
  const tokens: string[] = []
  const masked = pattern.replace(/\\[\s\S]?|[{}*]/gu, (token) => {
    tokens.push(token)
    return `${prefix}${tokens.length}end`
  })
  let resolved = new URL(masked, baseURL).href
  for (let index = 0; index < tokens.length; index++)
    resolved = resolved.replace(`${prefix}${index + 1}end`, tokens[index] ?? '')
  return resolved
}

/** Bounded, authored URL glob matching for synchronous native metadata lookup. */
export function nativeURLGlobMatcher(pattern: string): (url: string) => boolean {
  if (pattern.length > 8192) throw new Error('Native URL glob exceeds 8192 characters')
  const pending = [pattern]
  const alternatives: string[] = []
  let examined = 0
  while (pending.length) {
    const current = pending.pop()
    if (current === undefined) throw new Error('Native URL glob expansion state is unavailable')
    if (++examined > 1024) throw new Error('Native URL glob expansion capacity exceeded')
    let expanded = false
    for (let begin = 0; begin < current.length; begin++) {
      if (current.charAt(begin) === '\\') {
        begin++
        continue
      }
      if (current.charAt(begin) !== '{') continue
      let depth = 0,
        end = begin + 1,
        start = end
      const choices: string[] = []
      for (; end < current.length; end++) {
        const token = current.charAt(end)
        if (token === '\\') {
          end++
          continue
        }
        if (token === '{') depth++
        else if (token === '}' && depth) depth--
        else if (token === '}') break
        else if (token === ',' && !depth) {
          choices.push(current.slice(start, end))
          start = end + 1
        }
      }
      if (end === current.length || !choices.length) continue
      choices.push(current.slice(start, end))
      if (choices.length + pending.length + alternatives.length > 256)
        throw new Error('Native URL glob alternative capacity exceeded')
      for (const choice of choices)
        pending.push(current.slice(0, begin) + choice + current.slice(end + 1))
      expanded = true
      break
    }
    if (!expanded) alternatives.push(current)
  }
  // Compile literals once. Most browser route patterns have a long origin/path
  // prefix and extension suffix; neither needs a dynamic-programming row.
  const plans = alternatives.map((alternative) => {
    const tokens: (string | boolean)[] = []
    for (let position = 0; position < alternative.length; position++) {
      const token = alternative.charAt(position)
      if (token === '\\' && position + 1 < alternative.length)
        tokens.push(alternative.charAt(++position))
      else if (token === '*') {
        const deep = alternative.charAt(position + 1) === '*'
        while (alternative.charAt(position + 1) === '*') position++
        tokens.push(deep)
      } else tokens.push(token)
    }
    const first = tokens.findIndex((token) => typeof token === 'boolean')
    if (first === -1) return { exact: tokens.join(''), prefix: '', suffix: '', tokens: [] }
    let last = tokens.length - 1
    while (typeof tokens[last] !== 'boolean') last--
    return {
      exact: undefined,
      prefix: tokens.slice(0, first).join(''),
      suffix: tokens.slice(last + 1).join(''),
      tokens: tokens.slice(first, last + 1),
    }
  })
  return (url) => {
    if (url.length > 8192) throw new Error('Native frame URL exceeds lookup capacity')
    // A lookup budget belongs to this invocation, not to the lifetime of a
    // registration reused by pages, redirects and repeated navigation.
    let work = 0
    for (const plan of plans) {
      if (plan.exact !== undefined) {
        if (plan.exact === url) return true
        continue
      }
      if (
        url.length < plan.prefix.length + plan.suffix.length ||
        !url.startsWith(plan.prefix) ||
        !url.endsWith(plan.suffix)
      )
        continue
      const start = plan.prefix.length
      const length = url.length - start - plan.suffix.length
      let previous = new Uint8Array(length + 1)
      let next = new Uint8Array(length + 1)
      previous[0] = 1
      for (const token of plan.tokens) {
        work += length + 1
        if (work > 32 * 1024 * 1024)
          throw new Error('Native URL glob lookup work capacity exceeded')
        next.fill(0)
        if (typeof token === 'boolean') {
          next[0] = previous[0] ?? 0
          for (let index = 1; index <= length; index++)
            next[index] =
              previous[index] ||
              (next[index - 1] && (token || url.charAt(start + index - 1) !== '/'))
                ? 1
                : 0
        } else {
          for (let index = 1; index <= length; index++)
            next[index] = previous[index - 1] && token === url.charAt(start + index - 1) ? 1 : 0
        }
        const old = previous
        previous = next
        next = old
      }
      if (previous[length]) return true
    }
    return false
  }
}
