import { beforeEach, describe, expect, it, vi } from 'vitest'

import vitsPlugin from '../plugins/vits/src/index.js'

const sherpa = vi.hoisted(() => {
  const audio = {
    samples: new Float32Array([0, 0.25, -0.25]),
    sampleRate: 22_050,
  }
  const engine = {
    sampleRate: 22_050,
    numSpeakers: 3,
    generate: vi.fn(() => audio),
    save: vi.fn(),
    free: vi.fn(),
  }
  return {
    audio,
    engine,
    createOfflineTts: vi.fn(() => engine),
  }
})

vi.mock('sherpa-onnx', () => ({
  default: {
    createOfflineTts: sherpa.createOfflineTts,
    version: 'test',
    onnxruntimeVersion: 'test',
  },
}))

beforeEach(() => {
  vi.clearAllMocks()
})

describe('Sherpa audio plugin validation', () => {
  it('rejects another family configuration before creating an engine', async () => {
    await expect(
      vitsPlugin.synthesize({
        text: 'The report is ready.',
        voice: 'default',
        speed: 1,
        outputPath: '/tmp/suitecut-unused-sherpa.wav',
        options: {
          acousticModel: '/models/matcha.onnx',
          vocoder: '/models/vocos.onnx',
          tokens: '/models/tokens.txt',
        },
      }),
    ).rejects.toThrow()
    expect(sherpa.createOfflineTts).not.toHaveBeenCalled()
  })

  it('rejects a speaker ID outside the model range', async () => {
    await expect(
      vitsPlugin.synthesize({
        text: 'The report is ready.',
        voice: '9',
        speed: 1,
        outputPath: '/tmp/suitecut-unused-sherpa.wav',
        options: {
          model: '/models/other.onnx',
          tokens: '/models/other-tokens.txt',
        },
      }),
    ).rejects.toThrow("outside the model's 3 speakers")
    expect(sherpa.engine.generate).not.toHaveBeenCalled()
  })
})
