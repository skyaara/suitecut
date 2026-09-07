# SuiteCut technical specification

Status: implemented local v1; public schema remains pre-release

This document defines the objects that move through SuiteCut, from a running Playwright browser flow or test to a rendered video. The local v1 described here is implemented in `src/`; the file format is still pre-release and may change before publication.

The shared recorder records page activity, pointer events, checkpoints, narration, holds,
highlights, and zooms on an attempt clock. It streams one undecorated WebM source per page. The
primary standalone API writes that capture directly to a one-attempt manifest. Playwright Test can
instead send the capture through attachments to the SuiteCut reporter. The CLI renders a selected
attempt to captioned, narrated MP4 or WebM output. SuiteCut has not published a stable file format,
so development can still change the schema directly.

## 1. Design boundaries

SuiteCut's primary package entry runs without a test framework and owns one browser, context, main
page, recording, and cleanup lifecycle per `record()` call. The secondary `suitecut/test` entry
extends Playwright Test without replacing its runner, actionability checks, retries, or reporter.
`suitecut/playwright` remains an alias for the primary recorder.

SuiteCut owns:

- its per-attempt recording session;
- starting and stopping one undecorated screencast per page;
- narration, checkpoints, pointer events, highlights, zooms, and pacing hints;
- the common attempt clock;
- normalized Playwright execution steps when Playwright Test supplies them;
- per-page video roles and first-frame timing;
- the edit map and render plan;
- cursor artwork, visual effects, captions, and narration audio;
- the manifest and render diagnostics.

Playwright always owns:

- `Browser`, `BrowserContext`, `Page`, and `Locator` behavior;
- action and assertion waiting;
- the public screencast implementation, screenshots, and browser processes.

Playwright Test additionally owns test execution, fixtures, hooks, retries, parallel workers,
`TestInfo`, attachments, statuses, execution steps, and reporter objects. The plain Playwright entry
point has no tests, steps, retries, reporter, or Playwright configuration.

Playwright objects exist only while the active test or raw recording callback is running. SuiteCut may accept them in setup and recording methods, but must resolve them into plain serializable data before writing an event attachment or manifest. A manifest must never contain a `Browser`, `BrowserContext`, `Page`, `Locator`, `TestInfo`, `TestCase`, `TestResult`, or reporter step instance.

## 2. Target object flow

```text
standalone record()             Playwright Test
  |                               |
  | shared recorder               | shared recorder
  v                               v
SuiteCutEventAttachment         SuiteCutEventAttachment
  |                               |
  | writer adds callback result    | reporter adds test data
  +---------------+---------------+
                  v
            SuiteCutManifest
  |
  | media probe validates timing; timeline compiler applies pacing
  v
SuiteCutRenderPlan
  |
  | FFmpeg compiler creates inputs, filter_complex, and output args
  v
Rendered video + SuiteCutRenderReport
```

The event attachment is internal transport between the fixture and reporter. The reporter decodes it into the manifest and does not retain it as an artifact. The manifest is the durable input contract. The render plan is deterministic build data and may be regenerated.

## 3. Type conventions

All persisted objects are JSON unless a field explicitly refers to a file. JSON files use UTF-8.

```ts
type Milliseconds = number
type EpochMilliseconds = number
type ISODateTime = string
type FilePath = string
type MimeType = string

type SuiteCutTestId = string
type SuiteCutAttemptId = string
type SuiteCutEventId = string
type SuiteCutStepId = string
type SuiteCutPageId = string
type SuiteCutArtifactId = string
type SuiteCutMediaId = string
```

These aliases explain meaning but serialize as ordinary strings and numbers. Durations and timeline positions use milliseconds. Epoch values use Unix time in milliseconds. Calendar timestamps use ISO 8601 strings.

Rules:

- `atMs` is relative to the attempt's `clock.originEpochMs`.
- `durationMs` is never negative.
- rectangles use CSS viewport pixels captured at the event time;
- normalized render coordinates are calculated later and are not written back over capture coordinates;
- optional properties are absent when unknown. They are not written as `null` unless the type explicitly permits `null`;
- IDs are unique within their containing run;
- file paths in the current manifest are absolute. The proposed schema stores the path policy explicitly so portable relative paths can be added safely.

## 4. Shared value objects

### Geometry

```ts
interface SuiteCutPoint {
  x: number
  y: number
}

interface SuiteCutSize {
  width: number
  height: number
}

interface SuiteCutRect extends SuiteCutPoint, SuiteCutSize {}

interface SuiteCutViewport extends SuiteCutSize {
  scrollX: number
  scrollY: number
}
```

`SuiteCutPoint` uses coordinates relative to the main browser viewport. `SuiteCutRect` describes an element in the same coordinate space. Width and height must be zero or greater.

### Source location and errors

```ts
interface SuiteCutSourceLocation {
  file: FilePath
  line: number
  column: number
}

interface SuiteCutError {
  message: string
  name?: string
  stack?: string
  location?: SuiteCutSourceLocation
}
```

Line and column numbers follow Playwright's one-based source locations. Error messages in the durable manifest should preserve useful text. Any separate display summary may truncate them.

### Page identity

```ts
type SuiteCutPageKind = 'main' | 'popup' | 'secondary'

interface SuiteCutPage {
  id: SuiteCutPageId
  kind: SuiteCutPageKind
  openerPageId?: SuiteCutPageId
  initialUrl: string
  createdAtMs: Milliseconds
  closedAtMs?: Milliseconds
}
```

Page IDs let SuiteCut match pointer events and videos when a test creates more than one page. The first fixture page has `kind: 'main'`. A later page with a Playwright opener has `kind: 'popup'` and records `openerPageId`. A later page without an opener, such as one created through `context.newPage()`, has `kind: 'secondary'`.

## 5. Attempt clock

```ts
interface SuiteCutAttemptClock {
  originEpochMs: EpochMilliseconds
  originMonotonicMs: number
  startedAt: ISODateTime
}
```

The fixture captures the wall-clock and monotonic origins together. It calculates each event's `atMs` from the monotonic clock. The epoch value lets the reporter translate Playwright `Date` timestamps onto the same attempt timeline.

The reporter uses this mapping:

```text
stepAtMs = step.startTime.getTime() - clock.originEpochMs
```

Negative values are valid when a Playwright fixture or hook starts before SuiteCut's fixture body establishes its origin.

## 6. Recorded event model

Every event has the same base fields.

```ts
interface SuiteCutEventBase {
  id: SuiteCutEventId
  atMs: Milliseconds
}

interface SuiteCutPageEventBase extends SuiteCutEventBase {
  pageId: SuiteCutPageId
}
```

### Page selection

```ts
type SuiteCutPageSelectionReason = 'opened' | 'closed' | 'author' | 'interaction'

interface SuiteCutPageSelectionEvent extends SuiteCutPageEventBase {
  type: 'page-selected'
  reason: SuiteCutPageSelectionReason
}
```

The main page starts selected. SuiteCut writes a selection event when a new page becomes active,
the author calls `selectPage()`, an interaction moves to another page, or a selected popup closes.
The renderer uses these events instead of reconstructing page state from narration or pointer events.
It keeps event-based reconstruction only for unversioned legacy manifests.

### Narration

```ts
type SuiteCutNarrationVoice = string
type SuiteCutNarrationProvider = string

interface SuiteCutNarrationEvent extends SuiteCutPageEventBase {
  type: 'narration'
  text: string
  provider?: SuiteCutNarrationProvider
  voice?: SuiteCutNarrationVoice
  speed?: number
  caption?: string
}
```

`text` is the speech input. `pageId` records the active page when `narrate()` is called, so narration and visual events such as zoom can begin together without reconstructing page selection from timestamps. `caption`, when present, overrides the displayed caption without changing the spoken text. Speech duration does not belong in this captured event because SuiteCut does not know it until the queued audio has been generated and probed.

The `kokoro` provider runs the quantized Kokoro 82M ONNX model through `onnxruntime-web/wasm` in SuiteCut's Node worker and writes 24 kHz mono PCM WAV. SuiteCut bundles the model, tokenizer, and default `af_heart` voice. Other supported voices download once and reuse the local cache.

Kokoro with `af_heart` is the default narration provider and voice. The optional `macos-say` provider uses the reserved voice ID `default` to select the current macOS system voice. Other provider IDs must match a configured audio plugin. Provider IDs use lowercase letters, numbers, dots, underscores, and hyphens. Manifests and narration artifacts record the selected voice and exact speech provider so output differences can be diagnosed across machines.

### Audio plugins

```ts
type SuiteCutJsonValue =
  null | boolean | number | string | SuiteCutJsonValue[] | { [key: string]: SuiteCutJsonValue }

interface SuiteCutAudioPluginReference {
  provider: SuiteCutNarrationProvider
  module: string
  options?: SuiteCutJsonValue
}

interface SuiteCutAudioSynthesisRequest {
  text: string
  voice: string
  speed: number
  outputPath: string
  options?: SuiteCutJsonValue
}

interface SuiteCutAudioPlugin {
  synthesize(request: SuiteCutAudioSynthesisRequest): Promise<void>
}
```

`suitecut/audio-plugin` exports the interfaces, `defineSuiteCutAudioPlugin()`, and a mono 16-bit PCM
WAV encoder. Plugin packages use a default ESM export and write WAV audio to `outputPath` before
resolving. SuiteCut resolves package names from the recording project, converts local paths to file
URLs, rejects duplicate provider IDs, and prevents plugins from replacing `kokoro` or `macos-say`.

The main thread sends the resolved module reference and JSON options to the narration worker. The
worker validates the request, imports one plugin per provider, and reuses it for later narration in
the same recording. Model loading and synthesis stay outside the browser and renderer processes.

Sherpa model families ship as separate packages. VITS, Matcha, Sherpa-compatible Kokoro,
KittenTTS, ZipVoice, Pocket TTS, and Supertonic each expose one strict family schema. They depend on
the shared `@suitecut/audio-sherpa-core` package, which owns the Sherpa Node WASM engine cache. npm
deduplicates that shared runtime when a project installs more than one family package.

### Checkpoint

```ts
interface SuiteCutCheckpointEvent extends SuiteCutPageEventBase {
  type: 'checkpoint'
  label: string
  artifactId: SuiteCutArtifactId
  durationMs: Milliseconds
  viewport: SuiteCutViewport
}
```

A checkpoint points to a WebM clip artifact by ID. SuiteCut cuts the clip from the same page
screencast used by the renderer. The event timestamp maps the checkpoint onto source-video time.

### Pointer movement and buttons

```ts
type SuiteCutPointerType = 'mouse' | 'pen' | 'touch'
type SuiteCutPointerButton = 'none' | 'left' | 'middle' | 'right' | 'back' | 'forward'

interface SuiteCutPointerMoveEvent extends SuiteCutPageEventBase {
  type: 'pointer-move'
  point: SuiteCutPoint
  pointerType: SuiteCutPointerType
  viewport: SuiteCutViewport
  targetRect?: SuiteCutRect
}

interface SuiteCutPointerButtonEvent extends SuiteCutPageEventBase {
  type: 'pointer-down' | 'pointer-up' | 'click'
  point: SuiteCutPoint
  pointerType: SuiteCutPointerType
  button: SuiteCutPointerButton
  viewport: SuiteCutViewport
  targetRect?: SuiteCutRect
}
```

SuiteCut does not store the continuous browser `pointermove` stream. It records one movement anchor for a click target and one for a hover target produced by an author-written Playwright hover action. The renderer interpolates motion between those anchors. `targetRect` records the browser's measured event target bounds when available. It is capture evidence, not a live locator.

### Highlight

```ts
type SuiteCutHighlightMode = 'outline' | 'spotlight' | 'fill'
type SuiteCutHighlightGeometry = 'element' | 'content'
type SuiteCutBorderStyle = 'solid' | 'dashed'
type SuiteCutEasing = 'linear' | 'ease-in' | 'ease-out' | 'ease-in-out'
type SuiteCutVisualAnimation = 'none' | 'fade' | 'scale' | 'fade-scale'

interface SuiteCutAnimationOptions {
  type?: SuiteCutVisualAnimation
  durationMs?: Milliseconds
  easing?: SuiteCutEasing
}

interface SuiteCutHighlightOptions {
  durationMs?: Milliseconds
  mode?: SuiteCutHighlightMode
  geometry?: SuiteCutHighlightGeometry
  paddingPx?: number
  borderWidthPx?: number
  borderStyle?: SuiteCutBorderStyle
  borderColor?: string
  borderRadiusPx?: number
  fillColor?: string
  fillOpacity?: number
  backdropColor?: string
  backdropOpacity?: number
  label?: string
  enter?: SuiteCutAnimationOptions
  exit?: SuiteCutAnimationOptions
}

interface SuiteCutHighlightEvent extends SuiteCutPageEventBase {
  type: 'highlight'
  rect: SuiteCutRect
  viewport: SuiteCutViewport
  options: SuiteCutHighlightOptions
}
```

The runtime API accepts a locator and measures it. `element` geometry records the locator box. `content` geometry records the union of rendered text fragments and embedded visual controls, clamped to the locator box. Padding, border, fill, backdrop, and animation choices live in `options`.

Colors use CSS color strings. Opacity values range from `0` through `1`. The renderer must validate both instead of passing arbitrary text into an FFmpeg expression or shell command.

### Zoom

```ts
interface SuiteCutZoomOptions {
  scale?: number
  geometry?: SuiteCutHighlightGeometry
  paddingPx?: number
  holdMs?: Milliseconds
  enter?: SuiteCutAnimationOptions
  exit?: SuiteCutAnimationOptions
}

interface SuiteCutZoomEvent extends SuiteCutPageEventBase {
  type: 'zoom'
  rect: SuiteCutRect
  viewport: SuiteCutViewport
  options: SuiteCutZoomOptions
}
```

The target rectangle contains the selected element or content geometry. `paddingPx` controls the space kept around it after zooming. `enter` controls the move into the crop, `holdMs` controls how long the camera stays there, and `exit` controls the return to the normal frame. A requested scale must be at least `1` and no greater than the configured maximum.

### Explicit pacing

```ts
type SuiteCutHoldReason = 'author' | 'narration' | 'checkpoint'

interface SuiteCutHoldEvent extends SuiteCutPageEventBase {
  type: 'hold'
  durationMs: Milliseconds
  reason: SuiteCutHoldReason
}
```

An author hold is an explicit presentation instruction. Narration and checkpoint holds are normally generated by the timeline compiler and can appear in a derived timeline rather than the captured attachment.

### Event union

```ts
type SuiteCutEvent =
  | SuiteCutPageSelectionEvent
  | SuiteCutNarrationEvent
  | SuiteCutCheckpointEvent
  | SuiteCutPointerMoveEvent
  | SuiteCutPointerButtonEvent
  | SuiteCutHighlightEvent
  | SuiteCutZoomEvent
  | SuiteCutHoldEvent
```

The `type` field is the discriminator. Decoders must check every field used by the selected member.
Unknown event types fail validation for the selected manifest schema version.

## 7. Fixture attachment

The fixture writes one event attachment per test attempt.

```ts
const SUITECUT_EVENT_ATTACHMENT = 'suitecut-events.json' as const

interface SuiteCutEventAttachment {
  attemptId: SuiteCutAttemptId
  clock: SuiteCutAttemptClock
  endedAtMs: Milliseconds
  pages: SuiteCutPage[]
  events: SuiteCutEvent[]
  artifacts: SuiteCutCapturedArtifact[]
  videos: SuiteCutCapturedVideo[]
}

interface SuiteCutCapturedCheckpointArtifact {
  id: SuiteCutArtifactId
  attachmentName: string
  role: 'checkpoint'
  contentType: 'video/webm'
  capturedAtMs: Milliseconds
  durationMs: Milliseconds
  pageId: SuiteCutPageId
}

interface SuiteCutCapturedNarrationArtifact {
  id: SuiteCutArtifactId
  attachmentName: string
  role: 'narration-audio'
  contentType: MimeType
  createdAtMs: Milliseconds
  sourceEventId: SuiteCutEventId
  provider: string
  voice: string
}

type SuiteCutCapturedArtifact =
  SuiteCutCapturedCheckpointArtifact | SuiteCutCapturedNarrationArtifact

interface SuiteCutCapturedVideo {
  pageId: SuiteCutPageId
  attachmentName: string
  firstFrameEpochMs: EpochMilliseconds
  sourceStartedAtMs: Milliseconds
}

import type { Page } from '@playwright/test'

interface SuiteCutActiveScreencast {
  pageId: SuiteCutPageId
  page: Page
  outputPath: FilePath
  attachmentName: string
  firstFrameEpochMs?: EpochMilliseconds
}
```

Checkpoint and narration records use attachment names as internal transport. The reporter resolves those names to durable artifact paths and validates their roles. Narration records also retain the source event, provider, and requested voice.

The fixture starts one Playwright screencast for every registered page. It supplies `onFrame`, streams the JPEG payloads into a SuiteCut-owned FFmpeg encoder, attaches the completed WebM through `TestInfo`, and records the attachment name plus the first presented-frame timestamp in `videos`.

The callback receives JPEG data for every frame. SuiteCut writes each completed frame to the encoder and retains only the latest compressed JPEG until the next frame or stop, allowing a final duplicate to preserve an idle tail without creating a frame archive. The first callback also stores `firstFrameEpochMs`. `sourceStartedAtMs` is derived once:

```text
sourceStartedAtMs = firstFrameEpochMs - clock.originEpochMs
```

The active screencast is runtime-only because it contains a live Playwright `Page`. The fixture starts it like this:

```ts
const active: SuiteCutActiveScreencast = {
  pageId,
  page,
  outputPath,
  attachmentName,
}

await page.screencast.start({
  onFrame: async ({ data, timestamp }) => {
    active.firstFrameEpochMs ??= timestamp
    await encoder.writeFrame(data, timestamp)
  },
})
```

At teardown or page close, the fixture calls `page.screencast.stop()`, finalizes the encoder, attaches the completed WebM, and converts the runtime object into `SuiteCutCapturedVideo`. It fails with `FIRST_FRAME_TIMESTAMP_MISSING` if no callback supplied a timestamp. Retained frame data is released as soon as the encoder is finalized.

The reporter resolves referenced screenshot and video attachment names into `SuiteCutArtifact` records and reports missing or duplicate matches. It decodes `suitecut-events.json` into attempt fields, then discards that internal attachment instead of adding it to `artifacts`.

### Runtime recording session

The fixture may use this internal object to build the attachment. It is not exported to test authors and is never serialized as-is.

```ts
interface SuiteCutRecordingSession {
  readonly attemptId: SuiteCutAttemptId
  readonly clock: SuiteCutAttemptClock
  readonly pages: Map<SuiteCutPageId, SuiteCutPage>
  readonly activePageId: SuiteCutPageId
  readonly events: SuiteCutEvent[]
  readonly artifacts: SuiteCutCapturedArtifact[]
  readonly screencasts: Map<SuiteCutPageId, SuiteCutActiveScreencast>
  readonly videos: SuiteCutCapturedVideo[]

  now(): Milliseconds
  pageFor(pageId: SuiteCutPageId): Page
  pageIdFor(page: Page): SuiteCutPageId
  selectPage(page: Page): SuiteCutPageId
  nextEventId(): SuiteCutEventId
  record(event: SuiteCutEvent): void
  seal(): Promise<SuiteCutEventAttachment>
}
```

The fixture creates one session for each test attempt. It registers the fixture page as `main` and listens for later pages from the same browser context. Each later page starts as `secondary`; resolving a Playwright opener changes it to `popup` and records `openerPageId`. SuiteCut records close times for both kinds. A runtime `WeakMap<Page, SuiteCutPageId>` identifies the recorded page for a Playwright object, while a runtime `Map<SuiteCutPageId, Page>` resolves an active ID back to a live page. Neither lookup is serialized. New pages become active when Playwright emits the context page event. SuiteCut installs capture-phase passive listeners for pointer, keyboard, input, change, wheel, and scroll interactions in existing and future documents. A browser-context binding uses Playwright's `source.page` to select the page that produced each interaction without preventing defaults, stopping propagation, waiting, or replacing Playwright methods. Page-specific fixture calls also select their locator's page. Closing the active page selects its live opener, the live main page, or another live page in that order. If no live page remains, `activePageId` retains the last recorded page ID so the renderer can hold its final frame. Fixture methods, screencast callbacks, and browser bindings append to that session. `seal()` removes page listeners, browser instrumentation, and live page references, stops active screencasts, attaches their WebM files, fixes `endedAtMs`, validates references, and returns the attachment body. It fails if instrumentation setup, page registration, or a registered page video fails. No session is shared between retries or parallel workers.

## 8. Playwright execution steps

SuiteCut stores a normalized flat list. Parent IDs preserve Playwright's step tree without nesting mutable arrays.

```ts
type SuiteCutStepCategory =
  'hook' | 'fixture' | 'pw:api' | 'expect' | 'test.step' | 'test.attach' | 'unknown'

type SuiteCutStepOutcome = 'passed' | 'failed' | 'skipped' | 'interrupted'

interface SuiteCutExecutionStep {
  id: SuiteCutStepId
  parentStepId?: SuiteCutStepId
  title: string
  category: SuiteCutStepCategory
  atMs: Milliseconds
  durationMs: Milliseconds
  outcome: SuiteCutStepOutcome
  location?: SuiteCutSourceLocation
  error?: SuiteCutError
}
```

Reporter step titles are display text. SuiteCut must not parse locator coordinates, selectors, or business meaning out of them. The step record supplies timing and action meaning. Browser instrumentation supplies pointer geometry.

Unknown Playwright categories map to `unknown`; the original category may be preserved later in a separate `sourceCategory` field if we find that useful during implementation.

## 9. Artifacts and media

### Artifact records

```ts
type SuiteCutArtifactRole =
  | 'checkpoint'
  | 'source-video'
  | 'playwright-trace'
  | 'narration-audio'
  | 'captions'
  | 'rendered-video'
  | 'render-report'
  | 'other'

type SuiteCutPathKind = 'absolute' | 'manifest-relative'

interface SuiteCutArtifact {
  id: SuiteCutArtifactId
  name: string
  role: SuiteCutArtifactRole
  contentType: MimeType
  path: FilePath
  pathKind: SuiteCutPathKind
  sizeBytes?: number
  pageId?: SuiteCutPageId
  createdAtMs?: Milliseconds
}
```

Artifact roles are explicit. A renderer must not decide that a file is the source video merely because its name ends in `.webm`. Every `source-video` artifact must include `pageId`.

### Probed media

```ts
interface SuiteCutVideoStream {
  kind: 'video'
  codec: string
  pixelFormat?: string
  colorRange?: 'full' | 'limited'
  colorSpace?: string
  colorTransfer?: string
  colorPrimaries?: string
  width: number
  height: number
  durationMs: Milliseconds
  frameRate: number
  timeBase?: string
  hasVariableFrameRate: boolean
}

interface SuiteCutAudioStream {
  kind: 'audio'
  codec: string
  channels: number
  sampleRate: number
  durationMs: Milliseconds
}

type SuiteCutMediaStream = SuiteCutVideoStream | SuiteCutAudioStream

interface SuiteCutMedia {
  id: SuiteCutMediaId
  artifactId: SuiteCutArtifactId
  formatName: string
  durationMs: Milliseconds
  streams: SuiteCutMediaStream[]
}
```

The media probe, normally `ffprobe`, creates these records. Container duration and stream duration may differ. Timeline calculations should use video presentation timestamps when available.

## 10. First-frame video timing

SuiteCut starts each page screencast after establishing the attempt clock. Calling `screencast.start()` and receiving the first presented frame are separate moments, so the start-call time is not used as source-video time zero.

Playwright's `onFrame` callback provides the Unix epoch timestamp when the browser presented the frame. SuiteCut retains the first timestamp and derives the corresponding position on the attempt timeline.

```ts
interface SuiteCutVideoTiming {
  pageId: SuiteCutPageId
  mediaId: SuiteCutMediaId
  firstFrameEpochMs: EpochMilliseconds
  sourceStartedAtMs: Milliseconds
}
```

SuiteCut excludes capture pauses, such as narration synthesis, before it stores
`sourceStartedAtMs`. The mapping is:

```text
sourceVideoTimeMs = attemptTimeMs - sourceStartedAtMs
```

Example:

```text
clock.originEpochMs:   10,000
firstFrameEpochMs:     10,180
sourceStartedAtMs:        180

event.atMs:             2,180
sourceVideoTimeMs:      2,000
```

Each page has one timing record and one source-video media record. Their `pageId` values must match. Source time before zero means the selected page video has not started and cannot be rendered for that attempt interval. SuiteCut pauses frame ingestion while narration is prepared. `sourceStartedAtMs` uses the pause-adjusted attempt clock, while `firstFrameEpochMs` retains the browser timestamp as capture evidence.

The first implementation stores no frame-image archive, frame index, drift estimate, or confidence score. Later frame timestamps are used transiently to pace the streaming encoder but are not serialized. Real Chromium, Firefox, and WebKit recordings validate the timing mapping. Browser timestamps pace received frames by relative deltas. Final-frame padding uses the pause-adjusted attempt clock and `sourceStartedAtMs`; it must not subtract a browser timestamp from `Date.now()` because browser engines may use different epoch offsets. If later tests demonstrate drift, the schema can add a final timestamp or sparse samples.

This design requires `@playwright/test` 1.59 or newer. Screenshot matching and hidden synchronization screenshots are not part of the normal or fallback path.

SuiteCut starts the recording itself, so Playwright Test's separate `use.video` recording is not required. Running both would create duplicate videos. The supported renderer configuration uses `video: 'off'`; SuiteCut reports `SCREENCAST_START_FAILED` if another screencast already owns the page.

## 11. Run manifest

The run manifest is the renderer's required structured input.

```ts
type SuiteCutTestStatus = 'passed' | 'failed' | 'timedOut' | 'skipped' | 'interrupted'

type SuiteCutRunStatus = 'passed' | 'failed' | 'timedout' | 'interrupted'

interface SuiteCutAttempt {
  id: SuiteCutAttemptId
  retry: number
  status: SuiteCutTestStatus
  clock: SuiteCutAttemptClock
  durationMs: Milliseconds
  pages: SuiteCutPage[]
  events: SuiteCutEvent[]
  steps: SuiteCutExecutionStep[]
  artifacts: SuiteCutArtifact[]
  media: SuiteCutMedia[]
  videoTiming: SuiteCutVideoTiming[]
  errors: SuiteCutError[]
  diagnostics: SuiteCutDiagnostic[]
}

interface SuiteCutTest {
  id: SuiteCutTestId
  order: number
  title: string
  titlePath: string[]
  projectName: string
  location: SuiteCutSourceLocation
  attempts: SuiteCutAttempt[]
}

interface SuiteCutManifest {
  schemaVersion: 1
  startedAt: ISODateTime
  endedAt: ISODateTime
  status: SuiteCutRunStatus
  rootDirectory: FilePath
  tests: SuiteCutTest[]
}
```

Each retry is a separate `SuiteCutAttempt`. Attempts never share mutable event arrays, page IDs, or pointer state. Parallel workers merge through reporter results, not through a global in-memory recording session.

## 12. Runtime public API

These objects exist in user test code and are not serialized directly.

```ts
import type { Locator, Page } from '@playwright/test'

interface SuiteCutFixture {
  selectPage(page: Page): void
  narrate(text: string, options?: SuiteCutNarrationOptions): Promise<void>
  checkpoint(label: string, options?: SuiteCutCheckpointOptions): Promise<void>
  highlight(locator: Locator, options?: SuiteCutHighlightOptions): Promise<void>
  zoom(locator: Locator, options?: SuiteCutZoomOptions): Promise<void>
  hold(durationMs: Milliseconds): Promise<void>
  hover(locator: Locator, options?: SuiteCutPointerActionOptions): Promise<void>
  click(locator: Locator, options?: SuiteCutPointerActionOptions): Promise<void>
  type(locator: Locator, text: string, options?: SuiteCutTypeOptions): Promise<void>
  scrollTo(locator: Locator, options?: SuiteCutScrollOptions): Promise<void>
  scrollTop(options?: SuiteCutScrollOptions): Promise<void>
}

interface SuiteCutNarrationOptions {
  provider?: SuiteCutNarrationProvider
  voice?: SuiteCutNarrationVoice
  speed?: number
  caption?: string
}

interface SuiteCutCheckpointOptions {
  durationMs?: Milliseconds
}

interface SuiteCutTypeOptions {
  delayMs?: Milliseconds
  settleMs?: Milliseconds
  clearExisting?: boolean
}

interface SuiteCutAudioPluginReference {
  provider: SuiteCutNarrationProvider
  module: string
  options?: SuiteCutJsonValue
}
```

Runtime method behavior:

- Zod schemas define fixture inputs and infer their public TypeScript option types. Fixture methods record the parsed values and expose Zod's structured issue paths for invalid author input.
- `selectPage` validates that the Playwright page is registered and open, then changes SuiteCut's active video source. It does not call `page.bringToFront()`, navigate, wait, or change Playwright state.
- `narrate` requires non-empty text, a provider ID, a non-empty voice ID when supplied, and speed from `0.5` through `2`. Non-built-in provider IDs must match `suitecutAudioPlugins` in Playwright Test or `audioPlugins` in the plain Playwright recorder. An explicit empty caption suppresses displayed caption text. SuiteCut pauses frame ingestion while it prepares and measures speech, then records the caption and application for the measured duration.
- `checkpoint` validates a non-empty label and an optional duration from greater than zero through
  `10000` milliseconds. It records the event immediately and blocks later test actions for the
  requested window. After the page screencast closes, SuiteCut trims that window into a WebM clip,
  attaches it, probes it, and registers it as the checkpoint artifact. The default window is `500`
  milliseconds.
- `highlight` and `zoom` reject unknown option keys, invalid enum values, non-finite numbers, negative dimensions and timing values, and opacity outside `0` through `1`. They resolve the locator's current bounding box and fail clearly if the locator cannot produce valid geometry.
- `hold` records a required positive duration and waits in browser time so ongoing application animation remains in the source video.
- `hover` and `click` are direct Playwright action wrappers. SuiteCut moves the injected cursor before the action. Both wait for CSS or Web Animations created by the action unless `waitForAnimations` is false. `click` also records a ripple.
- `type` scrolls the locator into view, clears it by default, then uses Playwright `locator.pressSequentially()` to produce a real keyboard and input event sequence for each character. The default delay is `70` milliseconds per character and the default final settle is `250` milliseconds.
- `scrollTo` and `scrollTop` use native browser scrolling, wait for three stable animation frames, and then apply the requested settle time.

The fixture requires non-empty color strings but leaves full CSS color parsing to the selected browser. Zoom scale is validated from `1` through the fixed maximum of `1.25`. The renderer additionally limits the scale when necessary to retain the target's requested padding.

Example per-call customization:

```ts
suitecut.selectPage(popup)
await suitecut.narrate('The preview is open in a separate page.')

await suitecut.highlight(page.getByRole('button', { name: 'Publish' }), {
  mode: 'spotlight',
  paddingPx: 12,
  borderWidthPx: 4,
  borderStyle: 'solid',
  borderColor: '#22C55E',
  borderRadiusPx: 12,
  fillColor: '#22C55E',
  fillOpacity: 0.1,
  durationMs: 1400,
  enter: { type: 'fade', durationMs: 180, easing: 'ease-out' },
})

await suitecut.zoom(page.getByRole('dialog'), {
  scale: 1.18,
  paddingPx: 32,
  holdMs: 1200,
  enter: { type: 'scale', durationMs: 320, easing: 'ease-out' },
  exit: { type: 'scale', durationMs: 260, easing: 'ease-in-out' },
})
```

The public fixture contract includes page selection, narration, checkpoints, highlights, zooms, pointer actions, scrolling, and holds. Narration, captions, highlights, cursor travel, click ripples, scrolling, holds, and application animations run while Playwright records the selected browser. Recorded sources remain unzoomed; the final renderer applies camera crops from saved events.

With `capture.stream`, a separate encoder publishes the selected page over RTMP/RTMPS. Live zoom crops and resizes the current JPEG before encoding without changing browser layout. Browser presentation effects and captions are visible live; narration audio is not mixed into the broadcast. Disconnected or stalled encoders retry with configurable exponential backoff while browser actions continue. Only the latest frame is retained for replay after reconnect.

Configuring `capture.stream` always disables recording. There is no simultaneous recording option. The session retains the latest frame per open page but no event history, source videos, or checkpoint clips. Closed pages release their capture state. Completed narration synthesis files are removed, and the returned manifest contains an end-of-session summary without replay events or artifacts. `checkpoint()` only holds the current view in this mode.

The main `suitecut` entry point exports the author-facing fixture and option types. Durable event, recording, media, and manifest types are also available from `suitecut/types`. `SuiteCutReporterOptions` is exported from `suitecut/reporter`.

### Reporter configuration

```ts
interface SuiteCutReporterOptions {
  outputFile?: FilePath
  pathKind?: SuiteCutPathKind
  includeStepCategories?: SuiteCutStepCategory[]
}
```

The reporter writes a validated manifest after pending attempts are collected. `outputFile` defaults to `.suitecut/latest-run.json`, `pathKind` defaults to `absolute`, and `includeStepCategories` filters normalized Playwright steps when supplied. `SUITECUT_MANIFEST_PATH` takes precedence over `outputFile`.

## 13. Renderer input and configuration

The renderer takes a manifest plus a test-attempt selection. It does not take a live Playwright page.

```ts
interface SuiteCutRenderRequest {
  manifestPath: FilePath
  selection?: SuiteCutAttemptSelection
  outputPath: FilePath
  config?: SuiteCutRenderConfig
}

interface SuiteCutAttemptSelection {
  testId?: SuiteCutTestId
  retry?: number
}

interface SuiteCutRenderConfig {
  output?: SuiteCutOutputConfig
  narrationEnabled?: boolean
  resultHoldMs?: Milliseconds
  backgroundColor?: string
  ffmpegPath?: FilePath
  failureMode?: SuiteCutRenderFailureMode
}

type SuiteCutRenderFailureMode = 'strict' | 'best-effort'
```

Strict mode stops when any selected narration artifact or required video input is unavailable. Best-effort mode may omit unavailable narration or narration whose audio encoder is unavailable, and writes an `OPTIONAL_TRACK_OMITTED` warning to the render report. Source video, the video encoder, invalid configuration, and FFmpeg process failures remain fatal.

### Output settings

```ts
type SuiteCutOutputContainer = 'mp4' | 'webm' | 'mov' | 'mkv'
type SuiteCutColorRange = 'auto' | 'full' | 'limited'

interface SuiteCutOutputConfig {
  container?: SuiteCutOutputContainer
  /** @deprecated Use container. */
  format?: SuiteCutOutputContainer
  videoCodec?: string
  audioCodec?: string
  pixelFormat?: string
  colorRange?: SuiteCutColorRange
  width?: number
  height?: number
  framesPerSecond?: 30 | 60
  quality?: 'standard' | 'high' | 'master'
}
```

The default output is 1920 by 1080 at 30 frames per second with the `standard` quality profile. Output
width and height must be supplied together and must be even. SuiteCut infers MP4, WebM, MOV, or
Matroska from the output extension. MP4, MOV, and Matroska default to `libx264` and `aac`. WebM
defaults to `libvpx-vp9` and `libopus`.

Codec fields contain FFmpeg encoder names. SuiteCut checks them against the selected FFmpeg build
before rendering. The muxer and encoder validate container, codec, and pixel-format compatibility.
Named quality profiles supply tuned options for H.264, HEVC, VP8, VP9, AV1, and ProRes. Unknown
installed video encoders use their FFmpeg defaults. Known lossy audio encoders receive the profile's
audio bitrate. Lossless and unknown audio encoders use their FFmpeg defaults.

Color range is independent of the codec. The media probe records source pixel format, range,
primaries, transfer function, and matrix when FFprobe reports them. `auto` uses the probed input
range and chooses full-range output for RGB pixel formats or limited-range output for YUV pixel
formats. Explicit `full` and `limited` values override only the output range.

### Highlight and zoom defaults

Highlight defaults are resolved in the browser from each fixture call. Zoom defaults are shared by
the fixture's wait and the render-time camera compiler, so capture and render durations cannot drift.

```ts
interface SuiteCutResolvedAnimation {
  type: SuiteCutVisualAnimation
  durationMs: Milliseconds
  easing: SuiteCutEasing
}

interface SuiteCutHighlightDefaults {
  durationMs?: Milliseconds
  mode?: SuiteCutHighlightMode
  paddingPx?: number
  borderWidthPx?: number
  borderStyle?: SuiteCutBorderStyle
  borderColor?: string
  borderRadiusPx?: number
  fillColor?: string
  fillOpacity?: number
  backdropColor?: string
  backdropOpacity?: number
  label?: string
  enter?: SuiteCutAnimationOptions
  exit?: SuiteCutAnimationOptions
}

interface SuiteCutZoomDefaults {
  scale?: number
  paddingPx?: number
  holdMs?: Milliseconds
  enter?: SuiteCutAnimationOptions
  exit?: SuiteCutAnimationOptions
}
```

Built-in highlight defaults:

```ts
const DEFAULT_HIGHLIGHT = {
  durationMs: 1200,
  mode: 'outline',
  geometry: 'element',
  paddingPx: 8,
  borderWidthPx: 4,
  borderStyle: 'solid',
  borderColor: '#7C3AED',
  borderRadiusPx: 10,
  fillColor: '#7C3AED',
  fillOpacity: 0.08,
  backdropColor: '#000000',
  backdropOpacity: 0.45,
  enter: { type: 'fade', durationMs: 180, easing: 'ease-out' },
  exit: { type: 'fade', durationMs: 140, easing: 'ease-in' },
} satisfies Required<SuiteCutHighlightDefaults>
```

`backdropColor` and `backdropOpacity` apply only in `spotlight` mode. `fillColor` and `fillOpacity` apply to `fill` and `spotlight`. Outline mode draws only its border.

Built-in zoom defaults:

```ts
const DEFAULT_ZOOM = {
  scale: 1.15,
  geometry: 'element',
  paddingPx: 24,
  holdMs: 900,
  enter: { type: 'scale', durationMs: 260, easing: 'ease-out' },
  exit: { type: 'scale', durationMs: 220, easing: 'ease-in-out' },
} satisfies Required<SuiteCutZoomDefaults>
```

The maximum zoom scale is `1.25`. SuiteCut rejects an explicit per-call scale above the limit and
reduces an otherwise valid scale when the padded target would not fit inside the crop.

### Pacing

```ts
interface SuiteCutRenderConfig {
  resultHoldMs?: Milliseconds
}
```

`resultHoldMs` adds a frozen final-frame segment. Other fixture timing runs in the browser and is
therefore already present in the recorded source; the renderer does not reconstruct or compress it.

### Captions and audio

The cursor, click ripple, highlights, and captions are recorded in the selected browser rather than rebuilt from
renderer theme configuration. `narrationEnabled` is the only audio switch in render configuration.
Narration is the only audio track supported in v1. Source-audio capture, mixing, gain, and ducking
remain future work.

## 14. Presentation timeline and edit map

Execution time and presentation time are separate coordinate systems. The edit map is the only supported conversion between them.

```ts
type SuiteCutEditKind = 'play' | 'speed' | 'hold' | 'trim'

interface SuiteCutEditSegment {
  id: string
  kind: SuiteCutEditKind
  executionStartMs: Milliseconds
  executionEndMs: Milliseconds
  presentationStartMs: Milliseconds
  presentationEndMs: Milliseconds
  playbackRate: number
  freezeAtExecutionMs?: Milliseconds
  reason: SuiteCutEditReason
}

type SuiteCutEditReason =
  | 'normal'
  | 'minimum-action-duration'
  | 'post-action-result'
  | 'idle-compression'
  | 'narration'
  | 'checkpoint'
  | 'author-hold'
  | 'trim'

interface SuiteCutEditMap {
  executionDurationMs: Milliseconds
  presentationDurationMs: Milliseconds
  segments: SuiteCutEditSegment[]
}
```

Segments are ordered, non-overlapping in presentation time, and cover the rendered output. A `hold` has a fixed execution frame and a positive presentation duration. A `trim` records a removed execution range for diagnostics and has no rendered duration.

Every visual and audio event maps through the same edit map. Cursor timing, captions, highlights, camera movement, and narration must not maintain separate ad hoc offsets.

## 15. Render plan

The timeline compiler produces a plain, deterministic plan. This is the input to the FFmpeg compiler.

If the main-page source begins before its first recorded event, the compiler turns that leading span
into a hold at the first event's frame-aligned source timestamp. FFmpeg selects that video frame and
clones it until normal playback reaches the same timestamp. This removes the visible `about:blank`
startup without adding a screenshot asset or changing event timing.

```ts
interface SuiteCutRenderPlan {
  attemptId: SuiteCutAttemptId
  output: SuiteCutResolvedOutput
  durationMs: Milliseconds
  editMap: SuiteCutEditMap
  video: SuiteCutVideoPlan
  cursor: SuiteCutCursorPlan
  camera: SuiteCutCameraPlan
  highlights: SuiteCutHighlightPlan
  captions: SuiteCutCaptionPlan
  audio: SuiteCutAudioPlan
  assets: SuiteCutRenderAsset[]
  diagnostics: SuiteCutDiagnostic[]
}

interface SuiteCutResolvedOutput {
  path: FilePath
  container: SuiteCutOutputContainer
  width: number
  height: number
  framesPerSecond: number
  videoCodec: string
  audioCodec: string
  pixelFormat: string
  colorRange: 'full' | 'limited'
}

interface SuiteCutRenderAsset {
  id: string
  role: 'video' | 'narration-audio' | 'cursor' | 'font' | 'captions'
  path: FilePath
  inputIndex?: number
}
```

### Video plan

```ts
interface SuiteCutVideoPlan {
  backgroundColor: string
  frame: SuiteCutRect
  cornerRadiusPx: number
  segments: SuiteCutPageVideoSegment[]
}

interface SuiteCutPageVideoSegment {
  pageId: SuiteCutPageId
  mediaId: SuiteCutMediaId
  sourceStartMs: Milliseconds
  sourceEndMs: Milliseconds
  presentationStartMs: Milliseconds
  presentationEndMs: Milliseconds
  playbackRate: number
  freezeSourceAtMs?: Milliseconds
}
```

Each segment selects one page and that page's recorded video. The referenced media must resolve to a `source-video` artifact with the same `pageId`:

```text
segment.pageId = sourceVideoArtifact.pageId
segment.mediaId = sourceVideoMedia.id
sourceVideoMedia.artifactId = sourceVideoArtifact.id
```

`sourceStartMs` and `sourceEndMs` are timestamps inside the selected page's video after applying that video's `sourceStartedAtMs`. They are not attempt timestamps. `presentationStartMs` and `presentationEndMs` place the segment in the final SuiteCut video.

Segments are ordered and cannot overlap in presentation time. A boundary may switch the rendered source between the main page, a popup, or a secondary page:

```ts
const segments: SuiteCutPageVideoSegment[] = [
  {
    pageId: 'page-main',
    mediaId: 'video-main',
    sourceStartMs: 0,
    sourceEndMs: 2400,
    presentationStartMs: 0,
    presentationEndMs: 2400,
    playbackRate: 1,
  },
  {
    pageId: 'page-payment',
    mediaId: 'video-payment',
    sourceStartMs: 180,
    sourceEndMs: 2880,
    presentationStartMs: 2400,
    presentationEndMs: 5100,
    playbackRate: 1,
  },
]
```

All cursor, highlight, and zoom items shown during a segment must belong to the same `pageId`. Narration and captions start on their event's `pageId`, but their audio or text may continue after the selected video changes page.

The main page is selected at the start of an attempt. A click target, author-written hover target, highlight, or zoom on another page selects that page. Authors may call `selectPage(page)` to override passive selection. The selected page remains active through narration, holds, assertions, passive reads, and idle periods. Closing the selected page returns selection to its opener when it has one, or to the main page otherwise.

### Cursor plan

```ts
interface SuiteCutCursorPlan {
  enabled: boolean
  assetId?: string
  widthPx: number
  keyframes: SuiteCutCursorKeyframe[]
  clicks: SuiteCutClickEffect[]
}

interface SuiteCutCursorKeyframe {
  atMs: Milliseconds
  point: SuiteCutPoint
  opacity: number
  pressed: boolean
}

interface SuiteCutClickEffect {
  atMs: Milliseconds
  point: SuiteCutPoint
  durationMs: Milliseconds
  style: 'ring' | 'pulse'
  color: string
}
```

Render-plan points are output-canvas pixels after viewport scaling, letterboxing, and camera transforms. This differs from captured event points.

### Camera and highlights

```ts
interface SuiteCutCameraPlan {
  keyframes: SuiteCutCameraKeyframe[]
}

interface SuiteCutCameraKeyframe {
  atMs: Milliseconds
  center: SuiteCutPoint
  scale: number
  easingToNext: SuiteCutEasing
}

interface SuiteCutHighlightPlan {
  items: SuiteCutResolvedHighlight[]
}

interface SuiteCutResolvedHighlight {
  startMs: Milliseconds
  endMs: Milliseconds
  rect: SuiteCutRect
  mode: SuiteCutHighlightMode
  paddingPx: number
  borderWidthPx: number
  borderStyle: SuiteCutBorderStyle
  borderColor: string
  borderRadiusPx: number
  fillColor: string
  fillOpacity: number
  backdropColor: string
  backdropOpacity: number
  enter: SuiteCutResolvedAnimation
  exit: SuiteCutResolvedAnimation
  label?: string
}
```

Camera and highlight coordinates are resolved into output-canvas space. `easingToNext` tells the compiler how to interpolate each camera segment. The FFmpeg compiler may expand an easing curve into sampled keyframes, but it must not choose a different curve.

### Captions

```ts
interface SuiteCutCaptionPlan {
  enabled: boolean
  cues: SuiteCutCaptionCue[]
  assFilePath?: FilePath
}

interface SuiteCutCaptionCue {
  startMs: Milliseconds
  endMs: Milliseconds
  text: string
}
```

The compiler can write cues to an ASS subtitle file. ASS gives FFmpeg stable font, background, line wrapping, and safe-area placement controls.

### Audio

```ts
interface SuiteCutAudioPlan {
  sampleRate: number
  tracks: SuiteCutAudioTrack[]
}

interface SuiteCutAudioTrack {
  assetId: string
  role: 'narration'
  sourceStartMs: Milliseconds
  sourceEndMs: Milliseconds
  presentationStartMs: Milliseconds
  gainDb: number
}
```

The capture worker creates each narration audio artifact before the presentation continues because the browser caption needs the clip's measured duration. SuiteCut pauses frame ingestion and the attempt clock during synthesis, resumes capture, records the caption for the measured duration, and then attaches the finished WAV or AIFF file. Kokoro and macOS speech produce the same artifact role, so the renderer does not need provider-specific audio logic.

## 16. FFmpeg command model

SuiteCut should build an argument array for `spawn`, not concatenate a shell command.

```ts
interface SuiteCutFfmpegCommand {
  executable: FilePath
  args: string[]
  inputs: SuiteCutFfmpegInput[]
  filterGraph: string
  outputPath: FilePath
}

interface SuiteCutFfmpegInput {
  inputIndex: number
  assetId: string
  path: FilePath
  options: string[]
}
```

The command uses `-filter_complex` because SuiteCut has several inputs and branching tracks. A typical graph needs:

- `trim`, `setpts`, `tpad`, and `concat` for edit segments;
- `scale`, `pad`, and `crop` for browser layout and camera movement;
- `overlay` for the cursor and click artwork;
- `drawbox` or generated transparent overlays for highlights;
- `subtitles` for ASS captions;
- `atrim`, `asetpts`, `adelay`, `volume`, and `amix` for audio.

Before compiling the command, SuiteCut asks the selected FFmpeg executable for its encoder list.
It rejects missing video encoders and rejects a missing audio encoder when the render has an audio
track. FFmpeg reports unsupported container, codec, and pixel-format combinations during the render.

The FFmpeg compiler is a serializer. It must not decide pacing, cursor paths, camera movement, or narration placement. Those decisions already exist in `SuiteCutRenderPlan`.

## 17. Diagnostics and render report

```ts
type SuiteCutDiagnosticLevel = 'info' | 'warning' | 'error'

type SuiteCutDiagnosticCode =
  | 'INVALID_EVENT'
  | 'MISSING_ATTACHMENT'
  | 'MISSING_SOURCE_VIDEO'
  | 'MEDIA_PROBE_FAILED'
  | 'SCREENCAST_START_FAILED'
  | 'FIRST_FRAME_TIMESTAMP_MISSING'
  | 'VIDEO_TIMING_INVALID'
  | 'POINTER_OUTSIDE_VIEWPORT'
  | 'LOCATOR_GEOMETRY_MISSING'
  | 'OPTIONAL_TRACK_OMITTED'
  | 'RENDER_FAILED'
  | 'FFMPEG_FAILED'

interface SuiteCutDiagnostic {
  level: SuiteCutDiagnosticLevel
  code: SuiteCutDiagnosticCode
  message: string
  atMs?: Milliseconds
  eventId?: SuiteCutEventId
  artifactId?: SuiteCutArtifactId
}

interface SuiteCutRenderReport {
  attemptId: SuiteCutAttemptId
  startedAt: ISODateTime
  endedAt: ISODateTime
  status: 'rendered' | 'failed'
  outputArtifact?: SuiteCutArtifact
  executionDurationMs: Milliseconds
  presentationDurationMs: Milliseconds
  edits: SuiteCutEditSegment[]
  videoTiming: SuiteCutVideoTiming[]
  diagnostics: SuiteCutDiagnostic[]
  ffmpeg?: SuiteCutProcessResult
}

interface SuiteCutProcessResult {
  executable: FilePath
  args: string[]
  exitCode: number | null
  signal: NodeJS.Signals | null
  stderrTail: string
}
```

The report explains every speed change, hold, trim, warning, and video-timing result. SuiteCut
replaces local project, home, temporary, input, and output paths with named placeholders before it
persists FFmpeg arguments or stderr.

## 18. Validation rules

Every durable schema needs a strict decoder at its trust boundary.

Required checks:

- numeric fields are finite and respect their minimum values;
- event IDs, artifact IDs, page IDs, and media IDs resolve within the attempt;
- event arrays and edit segments are ordered;
- step parent IDs do not form cycles;
- page opener IDs resolve and do not form cycles;
- source and presentation intervals have valid bounds;
- every registered page has exactly one first-frame timing record and one source-video artifact;
- every first-frame timing `pageId` and `mediaId` resolves to the same source-video artifact;
- `sourceStartedAtMs` equals `firstFrameEpochMs - clock.originEpochMs` within the chosen timestamp precision;
- render assets exist and match their declared roles before FFmpeg starts;
- output paths cannot overwrite a required input artifact;
- CSS colors, codecs, and filter options come from validated values;
- strings passed to FFmpeg expressions are escaped by the compiler, never interpolated from raw user text.

Malformed input must produce a typed diagnostic or a thrown `SuiteCutSchemaError`.

```ts
class SuiteCutSchemaError extends Error {
  readonly path: string
  readonly code: string

  constructor(message: string, path: string, code: string)
}
```

The exact class implementation is not part of the JSON contract. The stable behavior is a clear error with the failing field path and reason.

## 19. Manifest compatibility policy

Every newly written manifest includes `schemaVersion: 1`. The decoder accepts unversioned manifests
from pre-version builds as legacy v1 input and returns the normalized versioned shape. It rejects an
unknown version at `schemaVersion` instead of attempting a partial decode.

```ts
type UntrustedInput = object | string | number | boolean | bigint | symbol | null | undefined

function decodeManifest(input: UntrustedInput): SuiteCutManifest
```

Add a new decoder or migration before writing a new schema version. Keep golden fixtures for every
supported version. Removing a decoder follows the package's next major-version policy.

## 20. Implemented type map

| Area             | Local v1 status                                                                                                                                               |
| ---------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Fixture          | captures checkpoints, validates public inputs, records narration, typewriter input, highlights, zooms, holds, page selection, and main-frame pointer activity |
| Events           | strict encoding and decoding for narration, checkpoint, pointer, highlight, zoom, hold, and page lifecycle events                                             |
| Event attachment | attaches the sealed attempt document through `TestInfo` with captured artifacts and per-page video records                                                    |
| Steps            | reporter collects normalized Playwright steps with stable parent IDs                                                                                          |
| Artifacts        | reporter resolves checkpoints, narration, source videos, and traces into durable artifact records                                                             |
| Media            | FFprobe-backed container and stream metadata for audio and video                                                                                              |
| Video timing     | first presented-frame epoch and attempt-relative source start for each registered page                                                                        |
| Manifest attempt | strict clock, pages, steps, media, timing, errors, retry status, and diagnostics                                                                              |
| Rendering        | deterministic page sequence, trims, holds, zooms, highlights, pointer marks, rasterized captions, narration mixing, MP4/WebM encoding, and render report      |

## 21. V1 renderer decisions

The first renderer release uses these rules:

1. `highlight`, `zoom`, and `hold` are stable fixture methods.
2. Pointer capture stores click targets and author-written hover targets. It does not store the continuous pointer-move stream.
3. The main page starts selected. A page-specific click, hover, highlight, or zoom selects its page. Authors may override that selection with `selectPage(page)`. The page remains selected until another selection event occurs or the selected popup closes.
4. V1 supports narration audio only. It does not capture or mix source audio.
5. V1 supports Kokoro 82M through a local WASM runtime and macOS native speech through `/usr/bin/say`. Kokoro defaults to `af_heart`; macOS speech defaults to the system voice. Optional packages can add narration providers through the audio plugin worker contract.

The local v1 implements the capture-to-render path, exports the renderer API, and runs a focused Chromium, Firefox, and WebKit recording matrix. Before a public release, the remaining work is richer cursor interpolation and effect styling, schema compatibility policy, and automated golden-video comparison.
