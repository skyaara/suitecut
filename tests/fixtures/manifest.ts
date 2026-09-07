import { type SuiteCutManifest } from '../../src/types.js'

export function createManifest(): SuiteCutManifest {
  return {
    schemaVersion: 1,
    startedAt: '2026-08-22T10:00:00.000Z',
    endedAt: '2026-08-22T10:00:05.000Z',
    status: 'passed',
    rootDirectory: '/workspace/app',
    tests: [
      {
        id: 'test-1',
        order: 0,
        title: 'creates a project',
        titlePath: ['projects', 'creates a project'],
        projectName: 'chromium',
        location: { file: '/workspace/app/tests/projects.spec.ts', line: 10, column: 1 },
        attempts: [
          {
            id: 'attempt-1',
            retry: 0,
            status: 'passed',
            durationMs: 5_000,
            clock: {
              originEpochMs: 10_000,
              originMonotonicMs: 500,
              startedAt: '2026-08-22T10:00:00.000Z',
            },
            pages: [
              {
                id: 'page-main',
                kind: 'main',
                initialUrl: 'https://example.test/projects',
                createdAtMs: 0,
                closedAtMs: 5_000,
              },
            ],
            events: [
              {
                id: 'event-1',
                type: 'narration',
                atMs: 200,
                pageId: 'page-main',
                text: 'The projects page is open.',
              },
              {
                id: 'event-2',
                type: 'checkpoint',
                atMs: 600,
                pageId: 'page-main',
                label: 'Project form opened',
                artifactId: 'artifact-checkpoint',
                durationMs: 500,
                viewport: {
                  width: 1280,
                  height: 720,
                  scrollX: 0,
                  scrollY: 0,
                },
              },
            ],
            steps: [
              {
                id: 'step-1',
                title: 'page.goto',
                category: 'pw:api',
                atMs: 100,
                durationMs: 80,
                outcome: 'passed',
              },
            ],
            artifacts: [
              {
                id: 'artifact-checkpoint',
                name: 'project-form.webm',
                role: 'checkpoint',
                contentType: 'video/webm',
                path: '/workspace/app/test-results/project-form.webm',
                pathKind: 'absolute',
                sizeBytes: 4_096,
                pageId: 'page-main',
                createdAtMs: 600,
              },
              {
                id: 'artifact-video',
                name: 'page-main.webm',
                role: 'source-video',
                contentType: 'video/webm',
                path: '/workspace/app/test-results/page-main.webm',
                pathKind: 'absolute',
                sizeBytes: 32_768,
                pageId: 'page-main',
                createdAtMs: 180,
              },
            ],
            media: [
              {
                id: 'media-video',
                artifactId: 'artifact-video',
                formatName: 'webm',
                durationMs: 4_820,
                streams: [
                  {
                    kind: 'video',
                    codec: 'vp8',
                    pixelFormat: 'yuv420p',
                    colorRange: 'limited',
                    colorSpace: 'bt709',
                    colorTransfer: 'bt709',
                    colorPrimaries: 'bt709',
                    width: 1280,
                    height: 720,
                    durationMs: 4_820,
                    frameRate: 30,
                    hasVariableFrameRate: false,
                  },
                ],
              },
              {
                id: 'media-checkpoint',
                artifactId: 'artifact-checkpoint',
                formatName: 'webm',
                durationMs: 500,
                streams: [
                  {
                    kind: 'video',
                    codec: 'vp9',
                    pixelFormat: 'yuv420p',
                    width: 1280,
                    height: 720,
                    durationMs: 500,
                    frameRate: 30,
                    hasVariableFrameRate: false,
                  },
                ],
              },
            ],
            videoTiming: [
              {
                pageId: 'page-main',
                mediaId: 'media-video',
                firstFrameEpochMs: 10_180,
                sourceStartedAtMs: 180,
              },
            ],
            errors: [],
            diagnostics: [],
          },
        ],
      },
    ],
  }
}

export function cloneManifest(): SuiteCutManifest {
  return structuredClone(createManifest())
}
