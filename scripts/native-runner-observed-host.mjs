// SPDX-License-Identifier: MIT
// Test-only byte observation on the ACTUAL native-owned host fd3/PID. No ACK,
// callback settlement, file result, process exit or protocol field is synthesized.
import { Buffer } from 'node:buffer'
import { syncBuiltinESMExports } from 'node:module'
import net from 'node:net'
import { pathToFileURL } from 'node:url'

/** @typedef {{toHost:(line:string)=>boolean,toNative:(line:string)=>boolean}} Wire */
/** @typedef {{fromNative:(line:string)=>void,fromHost:(line:string)=>void,close?:()=>void}} Gate */
/**
 * Observe genuine control bytes without adding a host process.
 * @param {string} host Actual authored SDK host module.
 * @param {(wire:Wire)=>Gate} createGate Real byte gate callbacks.
 * @returns {Promise<void>} Actual host module completion.
 */
export async function observeNativeRunnerHost(host, createGate) {
  // Older socket-shaped gates forward an already framed line; newer gates
  // forward the observed line without its LF. Preserve exactly one delimiter.
  const framed = (/** @type {string} */ line) => {
    const packet = line.endsWith('\n') ? line.slice(0, -1) : line
    if (!packet || packet.includes('\n'))
      throw Error('Proof control forwarding requires exactly one genuine line')
    return Buffer.from(packet + '\n')
  }
  const OriginalSocket = net.Socket
  /** @type {ObservedSocket[]} */
  const channels = []
  const gate = createGate({
    toHost: (line) => {
      const channel = channels[0]
      if (!channel) throw Error('Actual fd3 host socket is not open')
      return channel.deliver(line)
    },
    toNative: (line) => {
      const channel = channels[0]
      if (!channel) throw Error('Actual fd3 host socket is not open')
      return channel.forward(line)
    },
  })
  class ObservedSocket extends OriginalSocket {
    actual = false
    fromNative = ''
    fromHost = ''
    writableResult = true
    /**
     * Open the real Node socket and register only fd3.
     * @param {import('node:net').SocketConstructorOpts} [options] Actual socket options.
     */
    constructor(options) {
      super(options)
      if (options?.fd === 3) {
        if (channels.length) throw Error('Actual fd3 observer socket is already owned')
        channels.push(this)
        this.actual = true
      }
    }
    /**
     * Deliver an observed native line to the actual host reader.
     * @param {string} line Genuine native control line.
     * @returns {boolean} Actual event delivery result.
     */
    deliver(line) {
      return super.emit('data', framed(line))
    }
    /**
     * Forward a genuine host line onto the actual native socket.
     * @param {string} line Genuine host control line.
     * @returns {boolean} Actual write backpressure state.
     */
    forward(line) {
      const writable = super.write(framed(line))
      this.writableResult &&= writable
      return writable
    }
    /**
     * Observe only incoming real fd3 bytes.
     * @param {string|symbol} event Actual socket event.
     * @param {...unknown} args Actual event arguments.
     * @returns {boolean} Actual or consumed event status.
     */
    emit(event, ...args) {
      if (!this.actual || event !== 'data') return super.emit(event, ...args)
      const chunk = args[0]
      if (!Buffer.isBuffer(chunk)) throw Error('Actual fd3 emitted non-byte data')
      this.fromNative += chunk.toString('utf8')
      let at
      while ((at = this.fromNative.indexOf('\n')) >= 0) {
        const line = this.fromNative.slice(0, at)
        this.fromNative = this.fromNative.slice(at + 1)
        gate.fromNative(line)
      }
      return true
    }
    /**
     * Observe outgoing control bytes while preserving real socket writes.
     * @param {string|Uint8Array} chunk Actual outgoing bytes.
     * @param {BufferEncoding|((error?:Error|null)=>void)} [encoding] Actual encoding or callback.
     * @param {(error?:Error|null)=>void} [callback] Actual write callback.
     * @returns {boolean} Actual write backpressure state.
     */
    write(chunk, encoding, callback) {
      if (!this.actual) {
        if (typeof encoding === 'function') return super.write(chunk, encoding)
        return encoding === undefined
          ? super.write(chunk, callback)
          : super.write(chunk, encoding, callback)
      }
      if (typeof encoding === 'function' || callback)
        throw Error('Proof fd3 callback write is unsupported')
      this.fromHost += typeof chunk === 'string' ? chunk : Buffer.from(chunk).toString('utf8')
      this.writableResult = true
      let at
      while ((at = this.fromHost.indexOf('\n')) >= 0) {
        const line = this.fromHost.slice(0, at)
        this.fromHost = this.fromHost.slice(at + 1)
        gate.fromHost(line)
      }
      return this.writableResult
    }
  }
  // syncBuiltinESMExports is Node's public bridge for builtin module replacement.
  // Only the test host's fd3 socket is intercepted; stdout/stderr stay real.
  net.Socket = ObservedSocket
  syncBuiltinESMExports()
  try {
    await import(pathToFileURL(host).href)
  } finally {
    gate.close?.()
    net.Socket = OriginalSocket
    syncBuiltinESMExports()
  }
}
