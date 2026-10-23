// SPDX-License-Identifier: MIT
import { type UntrustedInput } from './untrusted.js'

/** A self-contained codec shared by the SDK and the browser execution context. */
export function createNativeValueCodec() {
  // The tagged transport adds nesting of its own. Bound source graph depth
  // before Chromium's return-by-value serializer reaches its nesting limit.
  const maxDepth = 100
  const maxNodes = 100_000
  const maxUTF16Units = 48 * 1024 * 1024 - 1024
  // WindowProxy values from a different realm do not inherit this realm's
  // Window.prototype. The platform getter checks the actual native receiver;
  // an author-provided Symbol.toStringTag or `window` property cannot pass it.
  const windowGetter =
    typeof Window === 'function'
      ? Reflect.getOwnPropertyDescriptor(globalThis, 'window')?.get
      : undefined
  function isWindow(value: object): boolean {
    if (!windowGetter) return false
    try {
      return Reflect.apply(windowGetter, value, []) === value
    } catch {
      return false
    }
  }
  // Two-byte lookup avoids per-code-unit radix conversion and padding for
  // isolated-surrogate strings, which must use the ASCII-safe wire form.
  function encodeText(value: string): string | object {
    if (typeof value !== 'string') throw new Error('Invalid native string metadata')
    let isolatedUTF16 = false
    for (let index = 0; index < value.length; ++index) {
      const unit = value.charCodeAt(index)
      if (unit < 0xd800 || unit > 0xdfff) continue
      if (unit <= 0xdbff && index + 1 < value.length) {
        const next = value.charCodeAt(index + 1)
        if (next >= 0xdc00 && next <= 0xdfff) {
          ++index
          continue
        }
      }
      isolatedUTF16 = true
      break
    }
    if (!isolatedUTF16) return value
    if (value.length > maxUTF16Units)
      throw new Error('Native UTF-16 string exceeds its transport byte limit')
    // Most evaluations create a codec for a few short values. Build the lookup
    // only when at least four code units amortize each of its 256 entries.
    const hexBytes =
      value.length >= 1024
        ? Array.from({ length: 256 }, (_, byte) => byte.toString(16).padStart(2, '0'))
        : undefined
    const chunks: string[] = []
    for (let start = 0; start < value.length; start += 8192) {
      let chunk = ''
      const end = Math.min(start + 8192, value.length)
      if (hexBytes) {
        for (let index = start; index < end; ++index) {
          const unit = value.charCodeAt(index)
          chunk += (hexBytes[unit >>> 8] ?? '') + (hexBytes[unit & 255] ?? '')
        }
      } else {
        for (let index = start; index < end; ++index)
          chunk += value.charCodeAt(index).toString(16).padStart(4, '0')
      }
      chunks.push(chunk)
    }
    return ['utf16', chunks.join('')]
  }
  const typedArrays: Record<string, (buffer: ArrayBuffer) => ArrayBufferView> = {
    Int8Array: (buffer) => new Int8Array(buffer),
    Uint8Array: (buffer) => new Uint8Array(buffer),
    Uint8ClampedArray: (buffer) => new Uint8ClampedArray(buffer),
    Int16Array: (buffer) => new Int16Array(buffer),
    Uint16Array: (buffer) => new Uint16Array(buffer),
    Int32Array: (buffer) => new Int32Array(buffer),
    Uint32Array: (buffer) => new Uint32Array(buffer),
    Float32Array: (buffer) => new Float32Array(buffer),
    Float64Array: (buffer) => new Float64Array(buffer),
    BigInt64Array: (buffer) => new BigInt64Array(buffer),
    BigUint64Array: (buffer) => new BigUint64Array(buffer),
    DataView: (buffer) => new DataView(buffer),
  }
  function encode(
    input: UntrustedInput,
    mode: 'argument' | 'result' = 'result',
    handleIndex?: (value: UntrustedInput) => number | undefined,
  ): object {
    const seen = new Map<object, number>()
    let visited = 0
    const visit = (value: UntrustedInput, depth: number, nested: boolean, path: string): object => {
      if (depth > maxDepth)
        throw new Error('Cannot serialize result: object reference chain is too long.')
      if (++visited > maxNodes) throw new Error('Native value exceeds its node limit')
      const handle = handleIndex?.(value)
      if (handle !== undefined) return ['handle', handle]
      if (value === undefined || typeof value === 'symbol') return ['undefined']
      if (value === null) return ['null']
      if (typeof value === 'string') {
        const encoded = encodeText(value)
        return typeof encoded === 'string' ? ['primitive', encoded] : encoded
      }
      if (typeof value === 'boolean') return ['primitive', value]
      if (typeof value === 'number') {
        if (Number.isNaN(value)) return ['number', 'NaN']
        if (Object.is(value, -0)) return ['number', '-0']
        if (value === Infinity) return ['number', 'Infinity']
        if (value === -Infinity) return ['number', '-Infinity']
        return ['primitive', value]
      }
      if (typeof value === 'bigint') return ['bigint', String(value)]
      if (typeof value === 'function') {
        if (mode === 'argument')
          throw new Error(
            `Attempting to serialize unexpected value${path ? ` at position "${path}"` : ''}: ${String(value)}`,
          )
        return nested ? ['empty'] : ['undefined']
      }
      if (typeof Date === 'function' && value instanceof Date) return ['date', value.toISOString()]
      if (typeof RegExp === 'function' && value instanceof RegExp)
        return ['regexp', encodeText(value.source), value.flags]
      if (typeof URL === 'function' && value instanceof URL) return ['url', value.href]
      if (value instanceof Error)
        return [
          'error',
          encodeText(value.name),
          encodeText(value.message),
          encodeText(value.stack ?? ''),
        ]
      if (ArrayBuffer.isView(value) || value instanceof ArrayBuffer) {
        const bytes =
          value instanceof ArrayBuffer
            ? new Uint8Array(value)
            : new Uint8Array(value.buffer, value.byteOffset, value.byteLength)
        if (bytes.length > maxNodes) throw new Error('Native byte value exceeds its length limit')
        return [
          'bytes',
          value instanceof ArrayBuffer ? 'ArrayBuffer' : value.constructor.name,
          Array.from(bytes),
        ]
      }
      if (isWindow(value)) return ['primitive', 'ref: <Window>']
      if (typeof Document === 'function' && value instanceof Document)
        return ['primitive', 'ref: <Document>']
      if (typeof Node === 'function' && value instanceof Node) return ['primitive', 'ref: <Node>']
      // These browser objects expose their public data through platform
      // toJSON methods rather than own enumerable properties. Do not invoke
      // arbitrary user-defined toJSON methods on ordinary objects.
      const tag = Object.prototype.toString.call(value)
      if (
        tag === '[object Performance]' ||
        tag === '[object PerformanceTiming]' ||
        tag === '[object PerformanceNavigation]' ||
        tag === '[object PerformanceMeasure]' ||
        tag === '[object PerformanceMark]' ||
        tag === '[object PerformanceResourceTiming]' ||
        tag === '[object PerformanceNavigationTiming]'
      ) {
        const constructor = Reflect.get(globalThis, tag.slice(8, -1)) as UntrustedInput
        const toJSON = Reflect.get(value, 'toJSON') as UntrustedInput
        if (
          typeof constructor === 'function' &&
          value instanceof constructor &&
          typeof toJSON === 'function'
        )
          return visit(Reflect.apply(toJSON, value, []) as UntrustedInput, depth + 1, nested, path)
      }
      const previous = seen.get(value)
      if (previous !== undefined) return ['reference', previous]
      const id = seen.size + 1
      seen.set(value, id)
      if (Array.isArray(value)) {
        const items: object[] = []
        for (let i = 0; i < value.length; i++)
          items[i] = visit(value[i] as UntrustedInput, depth + 1, true, `${path}[${i}]`)
        return ['array', id, items]
      }
      const fields: object[] = []
      for (const key of Object.keys(value)) {
        const item = Reflect.get(value, key) as UntrustedInput
        fields[fields.length] = [
          encodeText(key),
          visit(item, depth + 1, true, path ? `${path}.${key}` : key),
        ]
      }
      return ['object', id, fields]
    }
    return visit(input, 0, false, '')
  }
  function decode(input: UntrustedInput, handles: UntrustedInput[] = []): UntrustedInput {
    const objects = new Map<number, object>()
    let visited = 0
    const fail = (): never => {
      throw new Error('Invalid native serialized value')
    }
    const utf16 = (value: UntrustedInput): string => {
      if (typeof value !== 'string' || value.length % 4 || value.length > maxUTF16Units * 4)
        return fail()
      const digit = (index: number): number => {
        const code = value.charCodeAt(index)
        if (code >= 0x30 && code <= 0x39) return code - 0x30
        if (code >= 0x61 && code <= 0x66) return code - 0x61 + 10
        return fail()
      }
      const chunks: string[] = []
      for (let start = 0; start < value.length; start += 32768) {
        let chunk = ''
        const end = Math.min(start + 32768, value.length)
        for (let index = start; index < end; index += 4)
          chunk += String.fromCharCode(
            (digit(index) << 12) |
              (digit(index + 1) << 8) |
              (digit(index + 2) << 4) |
              digit(index + 3),
          )
        chunks.push(chunk)
      }
      return chunks.join('')
    }
    const text = (value: UntrustedInput): string => {
      if (typeof value === 'string') return value
      if (Array.isArray(value) && value.length === 2 && value[0] === 'utf16')
        return utf16(value[1] as UntrustedInput)
      return fail()
    }
    const visit = (value: UntrustedInput, depth: number): UntrustedInput => {
      if (depth > maxDepth || ++visited > maxNodes || !Array.isArray(value)) return fail()
      const node = value as UntrustedInput[]
      const tag = typeof node[0] === 'string' ? node[0] : fail()
      switch (tag) {
        case 'handle': {
          const index = node[1]
          if (
            typeof index !== 'number' ||
            !Number.isInteger(index) ||
            index < 0 ||
            index >= handles.length
          )
            return fail()
          return handles[index]
        }
        case 'empty':
          return {}
        case 'undefined':
          return undefined
        case 'null':
          return null
        case 'utf16':
          return node.length === 2 ? utf16(node[1]) : fail()
        case 'primitive': {
          const result = node[1]
          if (
            typeof result === 'string' ||
            typeof result === 'boolean' ||
            (typeof result === 'number' && Number.isFinite(result))
          )
            return result
          return fail()
        }
        case 'number': {
          switch (text(node[1])) {
            case 'NaN':
              return NaN
            case '-0':
              return -0
            case 'Infinity':
              return Infinity
            case '-Infinity':
              return -Infinity
            default:
              return fail()
          }
        }
        case 'bigint':
          return BigInt(text(node[1]))
        case 'date':
          return new Date(text(node[1]))
        case 'regexp':
          return new RegExp(text(node[1]), text(node[2]))
        case 'url':
          return new URL(text(node[1]))
        case 'error': {
          const result = new Error(text(node[2]))
          result.name = text(node[1])
          result.stack = text(node[3])
          return result
        }
        case 'bytes': {
          const name = text(node[1])
          if (!Array.isArray(node[2]) || node[2].length > maxNodes) return fail()
          const bytes = new Uint8Array(node[2].length)
          for (let i = 0; i < bytes.length; i++) {
            const byte = node[2][i] as UntrustedInput
            if (typeof byte !== 'number' || !Number.isInteger(byte) || byte < 0 || byte > 255)
              return fail()
            bytes[i] = byte
          }
          if (name === 'ArrayBuffer') return bytes.buffer
          const create = Object.hasOwn(typedArrays, name) ? typedArrays[name] : undefined
          if (!create) return fail()
          return create(bytes.buffer)
        }
        case 'reference': {
          const id = node[1]
          if (typeof id !== 'number' || !objects.has(id)) return fail()
          return objects.get(id)
        }
        case 'array':
        case 'object': {
          const id = node[1]
          const fields = node[2]
          if (
            typeof id !== 'number' ||
            !Number.isInteger(id) ||
            id < 1 ||
            objects.has(id) ||
            !Array.isArray(fields)
          )
            return fail()
          const result: object = tag === 'array' ? [] : {}
          objects.set(id, result)
          for (let i = 0; i < fields.length; i++) {
            const field = fields[i] as UntrustedInput
            if (tag === 'array') {
              if (
                !Reflect.defineProperty(result, i, {
                  value: visit(field, depth + 1),
                  writable: true,
                  enumerable: true,
                  configurable: true,
                })
              )
                return fail()
            } else {
              if (!Array.isArray(field)) return fail()
              const key = text(field[0] as UntrustedInput)
              if (
                !Reflect.defineProperty(result, key, {
                  value: visit(field[1] as UntrustedInput, depth + 1),
                  writable: true,
                  enumerable: true,
                  configurable: true,
                })
              )
                return fail()
            }
          }
          return result
        }
        default:
          return fail()
      }
    }
    return visit(input, 0)
  }
  return { encode, decode }
}
