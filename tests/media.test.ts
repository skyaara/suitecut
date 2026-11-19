// SPDX-License-Identifier: MIT
import { describe, expect, it, vi } from 'vitest'

import { decodeProbeDocument, probeMediaBatch } from '../src/media.js'
import { runProcess } from '../src/process.js'
import { type SuiteCutArtifact } from '../src/types.js'

vi.mock('../src/process.js', () => ({
  resolveFfprobe: vi.fn(() => Promise.resolve('/ffprobe')),
  runProcess: vi.fn(),
}))

describe('FFprobe metadata decoder', () => {
  it('accepts finite numeric strings and structured streams', () => {
    const document = decodeProbeDocument({
      streams: [
        {
          codec_type: 'video',
          codec_name: 'h264',
          pix_fmt: 'yuvj420p',
          color_range: 'pc',
          color_space: 'bt709',
          color_transfer: 'bt709',
          color_primaries: 'bt709',
          width: 1280,
          height: 720,
          duration: '1.25',
          avg_frame_rate: '30/1',
          r_frame_rate: '30/1',
        },
      ],
      format: { format_name: 'mov,mp4', duration: '1.25' },
    })
    expect(document.format.duration).toBe(1.25)
    expect(document.streams).toHaveLength(1)
    expect(document.streams[0]).toMatchObject({
      pix_fmt: 'yuvj420p',
      color_range: 'pc',
      color_space: 'bt709',
    })
  })

  it.each([
    { streams: [], format: { format_name: 'mp4', duration: '1' } },
    { streams: [{}], format: { format_name: 'mp4', duration: 'N/A' } },
    { streams: 'video', format: { format_name: 'mp4', duration: '1' } },
    { streams: [{}], format: { format_name: '', duration: '1' } },
  ])('rejects malformed probe documents', (document) => {
    expect(() => decodeProbeDocument(document)).toThrow()
  })
})

describe('FFprobe batch', () => {
  it('starts every probe at once and keeps input order and settle-before-reject', async () => {
    const pending: { path: string; settle: (fail: boolean) => void }[] = []
    vi.mocked(runProcess).mockImplementation((_executable, args) => {
      const path = args.at(-1) ?? ''
      return new Promise((resolve) => {
        pending.push({
          path,
          settle: (fail) =>
            resolve({
              exitCode: fail ? 1 : 0,
              stderr: fail ? 'broken' : '',
              stdout: JSON.stringify({
                streams: [
                  {
                    codec_type: 'audio',
                    codec_name: 'aac',
                    sample_rate: '48000',
                    channels: 2,
                    duration: '1',
                  },
                ],
                format: { format_name: path, duration: '1' },
              }),
            } as Awaited<ReturnType<typeof runProcess>>),
        })
      })
    })
    const inputs = Array.from({ length: 9 }, (_, index) => ({
      artifact: { id: `artifact-${index}` } as SuiteCutArtifact,
      path: `/media/${index}.mp4`,
    }))
    const batch = probeMediaBatch(inputs)
    await vi.waitFor(() => expect(pending).toHaveLength(inputs.length))
    for (const probe of pending.toReversed()) probe.settle(false)
    expect((await batch).map((media) => [media.artifactId, media.formatName])).toEqual(
      inputs.map((input) => [input.artifact.id, input.path]),
    )

    pending.length = 0
    let settled = false
    const failing = probeMediaBatch(inputs.slice(0, 3)).finally(() => (settled = true))
    await vi.waitFor(() => expect(pending).toHaveLength(3))
    pending[0]?.settle(true)
    pending[1]?.settle(false)
    await Promise.resolve()
    expect(settled).toBe(false)
    pending[2]?.settle(false)
    await expect(failing).rejects.toThrow('FFprobe failed for /media/0.mp4')
  })
})
