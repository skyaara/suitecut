import { describe, expect, it } from 'vitest'

import { createSuiteCutReportRedactor } from '../src/report-redaction.js'

describe('render report redaction', () => {
  it('replaces specific files before their parent directories', () => {
    const redact = createSuiteCutReportRedactor([
      { value: '/Users/person/project', replacement: '<project>' },
      { value: '/Users/person', replacement: '<home>' },
      { value: '/Users/person/project/results/source.webm', replacement: '<input-1>' },
    ])

    const result = redact(
      'ffmpeg -i /Users/person/project/results/source.webm /Users/person/project/out.mp4',
    )
    expect(result).toBe('ffmpeg -i <input-1> <project>/out.mp4')
    expect(result).not.toContain('/Users/person')
  })

  it('redacts portable forms of Windows paths', () => {
    const redact = createSuiteCutReportRedactor([
      { value: String.raw`C:\Users\person\project`, replacement: '<project>' },
    ])

    expect(redact('C:/Users/person/project/video.webm')).toBe('<project>/video.webm')
  })
})
