# SuiteCut implementation plan

## Product direction

SuiteCut extends Playwright Test with a controlled recording fixture and a renderer. It is not a replacement test runner and it does not treat Playwright traces as its primary data source.

Consumers use SuiteCut's extended test fixture:

```ts
import { expect, test } from 'suitecut'

test('creates a project', async ({ page, suitecut }) => {
  await page.goto('/projects')
  suitecut.narrate('The projects page is open.')
  await page.getByRole('button', { name: 'New project' }).click()
  await suitecut.checkpoint('Project form opened')
  await expect(page.getByRole('heading')).toHaveText('New project')
})
```

The fixture owns SuiteCut's per-test recording state. The target implementation starts one Playwright screencast for each page. Playwright still owns the browser, page, screencast implementation, test lifecycle, retries, and reporter callbacks.

## Decisions

### Keep the extended fixture

SuiteCut will continue to extend `@playwright/test` and export its own typed `test` fixture. We will not switch to a `setupSuiteCut(test)` API or standalone Recast-style helpers.

The fixture gives SuiteCut:

- guaranteed setup and teardown for each test attempt;
- direct access to `page` and Playwright's `TestInfo`;
- one typed `suitecut` object for narration, checkpoints, highlights, and recording controls;
- a reliable place to create, validate, and flush the event document;
- room for automatic pointer capture and per-page video timing.

Projects with other custom fixtures can compose them through Playwright's fixture APIs. SuiteCut should document that composition without changing its core ownership model.

### Keep SuiteCut's recording model authoritative

Playwright's `TestInfo` is an integration point. It is not SuiteCut's data model.

SuiteCut owns:

- recording start and end;
- narration markers and narration timing;
- checkpoints and screenshots;
- pointer positions and click events;
- highlight and zoom geometry;
- event ordering and relative timestamps;
- per-page video roles and first-frame timing;
- pacing and edit decisions;
- the manifest schema.

Playwright owns:

- test discovery and execution;
- browser contexts, pages, and retries;
- actionability and assertion waiting;
- the public per-page screencast API and optional trace capture;
- output directories and attachments;
- execution-step notifications;
- final status and errors.

SuiteCut will use public `TestInfo` methods such as `outputPath()` and `attach()`. It will not add private fields to `TestInfo`, replace it, or make Playwright annotations the only copy of SuiteCut data.

### Use reporter steps for the execution log

The SuiteCut reporter will use Playwright's supported reporter API:

```ts
onStepBegin(test, result, step)
onStepEnd(test, result, step)
onTestEnd(test, result)
```

The completed `result.steps` tree is the source for Playwright execution steps. SuiteCut will normalize relevant fields into its own schema:

- title;
- category;
- start time;
- duration;
- source location;
- nesting;
- error state.

The raw manifest may retain all categories. The renderer will normally select semantic `test.step`, Playwright API, and assertion steps while excluding fixture and hook noise unless those steps explain a failure.

Reporter titles are display text, not a stable structured protocol. SuiteCut will not parse locator coordinates or business meaning from step-title strings.

### Keep traces optional

Trace capture may remain available for debugging, but the SuiteCut renderer must not require a Playwright trace.

The first renderer will not depend on Playwright's internal trace ZIP format. Trace parsing may be added later as optional enrichment for DOM snapshots, network analysis, or recovery when no native video exists.

The normal recording inputs are:

- SuiteCut's event attachment;
- Playwright reporter steps;
- undecorated per-page WebM files recorded through Playwright's screencast API;
- the first presented-frame timestamp for each page video;
- SuiteCut checkpoint screenshots;
- Playwright status, errors, retries, and source locations.

### SuiteCut controls the cursor and visual annotations

SuiteCut will not use Playwright's `video.show.actions` cursor, action titles, or baked-in highlights for rendered output. The source video must remain undecorated.

SuiteCut will record pointer data during the test and render the cursor afterward. This keeps control over:

- cursor artwork and size;
- movement paths and easing;
- movement duration;
- visibility and fade rules;
- click ripples and press states;
- target outlines and spotlights;
- zoom and crop transforms;
- timing after idle compression or narration holds.

The initial capture design is browser-context instrumentation owned by the SuiteCut fixture:

1. Install listeners in the current document and future documents.
2. Capture click targets, author-written Playwright hover targets, pointer-down, pointer-up, and click events.
3. Record viewport coordinates, button, target bounds, viewport size, page identity, and timestamp.
4. Do not store the continuous browser pointer-move stream.
5. Store captured events in the same SuiteCut recording as narration and checkpoints.

Playwright's execution-step log supplies action timing and meaning. Browser pointer events supply coordinates. SuiteCut correlates them on its own attempt clock.

For explicit visual direction, the v1 fixture exposes stable APIs such as:

```ts
await suitecut.highlight(locator, options)
await suitecut.zoom(locator, options)
suitecut.hold(durationMs)
```

These APIs measure locator geometry at call time and record it. They do not draw into the live application.

### Keep tests fast and control pacing in the renderer

Playwright actions finish as soon as their actionability checks pass. Fast applications can produce raw videos where several actions happen in a fraction of a second.

SuiteCut will not solve this with global `slowMo` or routine `page.waitForTimeout()` calls. Those approaches slow tests and mix presentation timing into test behavior.

The renderer will create a separate presentation timeline that can:

- preserve normal speed around meaningful actions;
- give actions a minimum visible duration;
- hold a result frame after a click;
- extend a frame until narration finishes;
- compress long idle or network waits;
- preserve deliberate application animations;
- keep cursor movement aligned after retiming.

Execution time and presentation time are separate values. The manifest must preserve both.

## Timeline architecture

Each test attempt has one SuiteCut clock and several aligned tracks:

```text
Attempt timeline
├── Playwright execution steps
├── SuiteCut narration events
├── pointer and click events
├── highlight and zoom events
├── checkpoints
├── per-page source video timing
└── rendered presentation segments
```

### Attempt clock

The fixture records a high-resolution epoch origin when the SuiteCut recording starts. Every SuiteCut event stores `atMs` relative to that origin.

The event attachment must include enough information for the reporter to normalize Playwright step times against the same origin. Reporter events and fixture events run in different parts of Playwright, so they are merged through serialized data, not shared process memory.

The timeline must support negative or pre-roll step times when Playwright fixture setup begins before the SuiteCut recording origin. The renderer may trim them, but the raw manifest should not silently discard them.

### Per-page source-video timing

SuiteCut will start an undecorated Playwright screencast for every registered page. It will provide both `path` and `onFrame` to `page.screencast.start()`. Playwright writes the WebM and calls `onFrame` with the browser presentation timestamp for each captured frame.

SuiteCut retains only the first frame timestamp. It does not store, decode, compare, or write the JPEG buffer delivered with the callback. Later callbacks return immediately after checking that the first timestamp already exists.

For each page video, SuiteCut calculates where source-video time zero belongs on the attempt timeline:

```text
sourceStartedAtMs = firstFrameEpochMs - recordingOriginEpochMs
sourceVideoTimeMs = attemptTimeMs - sourceStartedAtMs
```

Example:

```text
attempt origin epoch:     10,000ms
first frame epoch:        10,180ms
sourceStartedAtMs:           180ms

event attempt time:        2,180ms
event source-video time:   2,000ms
```

Every page has its own WebM, first frame timestamp, and `sourceStartedAtMs`. Page-specific events and source-video artifacts share the same `pageId`. The renderer uses that relationship to select the main-page, popup, or secondary-page video for each presentation segment.

Checkpoint screenshots are not synchronization inputs. Screenshot matching is not part of the first implementation.

This design requires Playwright 1.59 or newer because that release introduced the public Screencast API. We must verify that the first `onFrame` timestamp maps to source-video time zero across Chromium, Firefox, and WebKit before treating the mapping as proven.

SuiteCut's recording path does not require Playwright Test's `use.video` setting. Enabling both would create duplicate recordings, so SuiteCut should document `video: 'off'` for its renderer path and report a clear configuration conflict when another screencast already owns the page.

### Pointer coordinates

Pointer events use main-viewport pixel coordinates whenever possible. Each event also records the viewport dimensions used at capture time.

The renderer maps viewport coordinates through video scaling, letterboxing, crop, and zoom transforms before drawing the cursor.

Initial support should target the main frame. Same-origin and cross-origin iframe coordinates need explicit tests because child-frame coordinates require translation into the main viewport. Multi-page and popup recordings also need page identities so pointer events are assigned to the correct source video.

### Active page selection

The main page starts selected. A click target, author-written Playwright hover target, highlight, or zoom on another page selects that page. Authors may also call `selectPage(page)` when passive interaction tracking cannot express the intended source page. The selected page remains active through narration, holds, assertions, passive reads, and idle periods. Closing the selected page returns selection to its opener when it has one, or to the main page otherwise.

### Cursor interpolation

Recorded pointer points are anchors, not the final motion path. The renderer owns interpolation.

The first motion model should:

- move from the previous visible point to the next captured click or hover target;
- use a configurable eased or curved path;
- cap minimum and maximum travel duration;
- hold briefly over the target before a click when presentation time allows;
- render pointer-down and click-ripple states;
- avoid movement during frames that do not belong to the active page;
- retime every point through the presentation edit map.

Cursor interpolation must remain deterministic so repeated renders of the same manifest produce the same video.

## Manifest direction

The manifest remains the renderer's only required structured input. Playwright objects must not leak into it.

The schema will grow around these concepts:

```ts
interface SuiteCutAttemptTimeline {
  recordingStartedAt: string
  recordingOriginEpochMs: number
  durationMs: number
  events: SuiteCutEvent[]
  steps: SuiteCutExecutionStep[]
  artifacts: SuiteCutArtifact[]
  media: SuiteCutMedia[]
  videoTiming: SuiteCutVideoTiming[]
}
```

Expected event families include:

- narration;
- checkpoint;
- pointer movement;
- pointer down and up;
- click;
- highlight;
- zoom;
- explicit hold or pacing instruction.

The unpublished schema can change directly during development. Decoding remains strict. Malformed optional events may be excluded with diagnostics, while a malformed attempt header must fail clearly.

Paths in the manifest should describe artifacts without granting them meaning by filename alone. Store roles such as `source-video`, `checkpoint`, and `playwright-trace` explicitly. Every `source-video` artifact must carry the `pageId` of the page it records.

The `suitecut-events.json` attachment is internal fixture-to-reporter transport. The reporter decodes its pages, events, captured artifacts, and video timing into the attempt, then excludes the attachment itself from `artifacts`.

## Public API direction

Keep the current primary shape:

```ts
import { expect, test } from 'suitecut'
```

The `suitecut` fixture is the recording control object. Its v1 renderer API includes:

```ts
interface SuiteCutFixture {
  selectPage(page: Page): void
  narrate(text: string, options?: SuiteCutNarrationOptions): void
  checkpoint(label: string, options?: SuiteCutCheckpointOptions): Promise<void>
  highlight(locator: Locator, options?: SuiteCutHighlightOptions): Promise<void>
  zoom(locator: Locator, options?: SuiteCutZoomOptions): Promise<void>
  hold(durationMs: Milliseconds): void
}
```

The current fixture records narration, hold, highlight, and zoom events in an in-memory session. It also registers pages and tracks the active page. Checkpoint capture, event attachment, screencast capture, reporter manifest output, CLI execution, and rendering remain incomplete.

The package remains Node ESM with TypeScript `NodeNext`. Relative imports in TypeScript source continue to use emitted `.js` specifiers. Consumers import public package names such as `suitecut` and `suitecut/reporter`, never internal `dist` or source paths.

## Delivery phases

### Phase 1: authoritative attempt timeline

- Add the recording epoch origin to event attachments.
- Normalize Playwright reporter steps into typed manifest steps.
- Preserve step nesting, timing, categories, locations, and errors.
- Assign explicit roles to video, trace, and screenshot artifacts.
- Decode and discard the internal event attachment after building the attempt.
- Add manifest validation for the expanded attempt timeline.
- Prove parallel tests and retries remain isolated.

### Phase 2: SuiteCut pointer capture

- Install pointer instrumentation through the fixture.
- Capture page identity, viewport size, coordinates, button, and target bounds.
- Cover navigation, scrolling, hover, click, and popup behavior.
- Store click targets and author-written hover targets without retaining continuous pointer movement.
- Verify event ordering against reporter actions.
- Keep Playwright's native video action annotations disabled.

### Phase 3: per-page screencast timing

- Require Playwright 1.59 or newer.
- Register the main page, every popup, and every secondary page with a stable `pageId`.
- Start one undecorated WebM screencast per page with `path` and `onFrame`.
- Retain only `firstFrameEpochMs`; never retain or decode callback JPEG buffers.
- Calculate and store `sourceStartedAtMs` for every page video.
- Read video duration, dimensions, frame rate, and presentation timestamps with `ffprobe`.
- Verify that the first callback timestamp maps to source-video time zero across Chromium, Firefox, and WebKit.
- Measure callback overhead and fail clearly when no first frame arrives.

### Phase 4: SuiteCut-controlled cursor and highlights

- Render a transparent cursor track over the raw video.
- Add deterministic interpolation and click effects.
- Transform pointer and target geometry through scaling and crop operations.
- Render explicit highlights and zooms from SuiteCut events.
- Test desktop, mobile viewport, scrolling, and retimed segments.

### Phase 5: presentation pacing

- Generate edit segments from actions, idle gaps, checkpoints, and narration.
- Add minimum visible durations by action class.
- Compress idle waits without removing meaningful application changes.
- Add narration-driven frame holds.
- Retime cursor, highlights, captions, and audio through one edit map.
- Make defaults configurable without adding sleeps to test code.

### Phase 6: rendering and audio

- Render captions and provider-independent speech from the SuiteCut narration track.
- Generate narration through Kokoro 82M on a local WASM runtime or `/usr/bin/say` with an installed macOS voice.
- Cache Kokoro's quantized model, tokenizer, and selected voice profile after the first download.
- Do not capture or mix source audio in v1.
- Produce deterministic MP4 or WebM output from the same manifest.
- Preserve an edit report that explains every trim, speed change, and hold.
- Keep hosted model APIs and cloud speech services out of the default path.

## Verification requirements

Each phase needs focused unit tests and at least one real Playwright recording.

Required browser evidence includes:

- a fast test whose raw actions occur too quickly to follow;
- cursor movement that lands on the correct clicked elements;
- an event aligned to the correct frame using only the first frame timestamp;
- separate main-page, popup, and secondary-page videos selected through `pageId`;
- narration that extends a visual hold without slowing the test;
- an idle wait compressed in the rendered video;
- a failed attempt and a retry stored as separate timelines;
- parallel tests with no cross-test pointer or narration leakage;
- mobile and desktop viewport coordinate mapping.

For visual work, compare rendered frames at known event timestamps. Passing type checks and manifest tests alone is not enough to claim cursor or video-timing correctness.

## Non-goals

- Do not switch to standalone Recast-style helpers.
- Do not make Playwright annotations or marker-prefixed `test.step()` titles authoritative.
- Do not require traces for standard rendering.
- Do not parse human-readable reporter step titles for coordinates.
- Do not proxy the entire Playwright `Page` or `Locator` API.
- Do not use Playwright's baked-in cursor or action overlays in the final source video.
- Do not use hidden screenshots or image matching to align source videos.
- Do not retain or decode Screencast callback JPEG buffers.
- Do not add fixed waits throughout user tests to make recordings watchable.
- Do not let the renderer depend directly on live Playwright objects.

## Immediate next work

The next implementation slice should finish the capture boundary before rendering. It should implement checkpoint artifacts, attach the sealed event document through `TestInfo`, add per-page screencasts and first-frame timing, then let the reporter decode that attachment into a manifest. Pointer geometry and execution steps must continue to use the same attempt clock.
