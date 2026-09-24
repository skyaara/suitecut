import { type NativeAudioPacket } from './native-browser.js'

/** Reblocks arbitrary CEF callback sizes into the publisher's 20 ms PCM packets. */
export class NativeAudioAssembler {
  private pending: Buffer = Buffer.alloc(0)
  private at = 0
  constructor(private readonly emit: (timestampMs: number, pcm: Buffer) => void) {}

  clear(): void {
    this.pending = Buffer.alloc(0)
    this.at = 0
  }

  push(packet: NativeAudioPacket): void {
    const expected = this.at + this.pending.length / 192
    if (!this.pending.length || Math.abs(packet.timestampMs - expected) > 5) {
      this.pending = Buffer.alloc(0)
      this.at = packet.timestampMs
    }
    this.pending = Buffer.concat([this.pending, packet.data])
    while (this.pending.length >= 3840) {
      this.emit(this.at, this.pending.subarray(0, 3840))
      this.pending = this.pending.subarray(3840)
      this.at += 20
    }
  }
}
