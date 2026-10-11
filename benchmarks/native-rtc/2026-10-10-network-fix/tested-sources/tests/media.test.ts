import { describe, expect, it, vi } from 'vitest'

import { decodeProbeDocument, probeMedia } from '../src/media.js'

const { runProcess } = vi.hoisted(() => ({ runProcess: vi.fn() }))
vi.mock('../src/process.js', () => ({
  resolveFfprobe: () => Promise.resolve('ffprobe'),
  runProcess,
}))

describe('FFprobe metadata decoder', () => {
  it('accepts an unknown average rate for sparse variable-rate video', async () => {
    runProcess.mockResolvedValue({
      exitCode: 0,
      stdout: JSON.stringify({
        streams: [
          {
            codec_type: 'video',
            codec_name: 'vp9',
            width: 960,
            height: 540,
            avg_frame_rate: '0/0',
            r_frame_rate: '10/1',
          },
        ],
        format: { format_name: 'matroska,webm', duration: '0.3' },
      }),
    })
    const media = await probeMedia(
      {
        id: 'test',
        name: 'Sparse clip',
        role: 'checkpoint',
        contentType: 'video/webm',
        path: 'test.webm',
        pathKind: 'manifest-relative',
      },
      '/test.webm',
    )
    expect(media.streams[0]).toMatchObject({ frameRate: 10, hasVariableFrameRate: true })
  })

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
