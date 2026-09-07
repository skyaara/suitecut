declare module 'sherpa-onnx' {
  interface GeneratedAudio {
    samples: Float32Array
    sampleRate: number
  }

  interface OfflineTts {
    readonly sampleRate: number
    readonly numSpeakers: number
    generate(config: { text: string; sid: number; speed: number }): GeneratedAudio
    save(filename: string, audio: GeneratedAudio): void
    free(): void
  }

  interface SherpaOnnxModule {
    createOfflineTts(config: object): OfflineTts
    readonly version: string
    readonly onnxruntimeVersion: string
  }

  const sherpaOnnx: SherpaOnnxModule
  export default sherpaOnnx
}
