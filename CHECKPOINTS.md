# SuiteCut build checkpoints

This is the working build order. Each checkpoint should add one small behavior or one boundary. A checked item means its implementation and focused verification are complete. Unchecked items may already have partial code.

## Contracts and Playwright boundary

- [x] 1. Define the durable schema types under `src/types`.
- [x] 2. Define the full public v1 `suitecut` fixture interface: page selection, narration, checkpoints, highlights, zoom, pointer actions, native scrolling, and holds, with exact sync or async return types.
- [x] 3. Type the extended Playwright `test` export with `{ page, suitecut }`.
- [x] 4. Specify when one attempt starts, seals, and releases its recording session.
- [x] 5. Trace one example test into an event attachment and a minimal manifest.

## Attempt timing

- [x] 6. Create an attempt clock from epoch and monotonic time.
- [x] 7. Convert an epoch timestamp to attempt-relative milliseconds.
- [x] 8. Allow reporter steps to start before the attempt origin.
- [x] 9. Normalize one Playwright reporter step without its children, map unknown categories to `unknown`, and never parse meaning or geometry from its title.
- [x] 10. Flatten a reporter step tree while retaining parent IDs.

## First recording path

- [x] 11. Create one recording session for each test attempt.
- [x] 12. Keep retry sessions and parallel sessions isolated.
- [x] 13. Register the Playwright main page in the session.
- [x] 14. Prepare one narration clip while frame ingestion and the attempt clock are paused, then record its event, caption, and measured playback time.
- [x] 14a. Run narration synthesis on a per-attempt worker, attach each completed WAV or AIFF file, and make the awaited fixture call include playback and the configured narration tail.
- [x] 15. Mark a checkpoint window in the active page screencast and honor `durationMs`.
- [x] 16. Cut and register the checkpoint WebM as a media artifact.
- [x] 17. Record one checkpoint event that references its artifact.
- [x] 18. Seal the session into a serializable event attachment.
- [x] 19. Attach the event document through Playwright `TestInfo`.
- [x] 20. Decode and discard the internal event attachment at the reporter boundary.
- [x] 21. Resolve captured screenshot, source-video, and optional Playwright trace attachments into explicit artifact roles, reporting missing or duplicate matches.

## Minimal manifest path

- [x] 22. Map Playwright identity, retry, status, duration, and errors into one attempt.
- [x] 23. Add normalized reporter steps to that attempt.
- [x] 24. Group attempts under their Playwright test without sharing mutable attempt state.
- [x] 25. Derive run timing and status, then write one manifest to disk using the configured path policy.
- [x] 26. Strictly decode manifest fields, finite numbers, enums, and ordered arrays with exact error paths.
- [x] 27. Reject duplicate IDs and unresolved attempt-local references.
- [x] 28. Reject cyclic page-opener and step-parent references.
- [x] 29. Validate source-video, media, page, first-frame timing, and duration relationships.
- [x] 29a. Version newly written manifests as schema v1, accept unversioned legacy v1 input, and reject unsupported versions at the compatibility boundary.

## Pages and source video

- [x] 30. Assign a stable ID to each new Playwright page.
- [x] 31. Record popup opener and close relationships.
- [x] 32. Start one undecorated screencast for a registered page and report a clear conflict if another screencast owns it.
- [x] 33. Stream callback JPEG data directly into the per-page encoder, retaining only the latest compressed frame until the next frame or stop.
- [x] 34. Stop and attach a screencast when its page closes, failing clearly when no first frame arrived.
- [x] 35. Probe one attached video into typed container, stream, duration, dimensions, frame-rate, and timestamp metadata.
- [x] 36. Calculate the source-video start on the attempt clock.
- [x] 37. Prove first-frame alignment and measure callback overhead in one real Chromium run.

## Pointer and author controls

- [x] 38. Install pointer listeners in the main document.
- [x] 39. Install the same listeners after navigation and in future documents.
- [x] 40. Record pointer-down, pointer-up, and click anchors.
- [x] 41. Record author-written hover targets without continuous pointer movement.
- [x] 42. Store viewport and target geometry with each pointer anchor.
- [ ] 43. Verify pointer-event ordering against the corresponding Playwright reporter actions.
- [x] 44. Persist active-page selection events for page openings, author choices, interactions, and close fallbacks, then restore the opener or main page when the selected page closes.
- [x] 45. Measure and record one `highlight` request with its call options, failing clearly when locator geometry is unavailable.
- [x] 46. Measure and record one `zoom` request with its call options, failing clearly when locator geometry is unavailable.
- [x] 47. Record one explicit `hold` request and keep Chromium recording for its required browser-time duration.
- [x] 47a. Type into a form field one character at a time with strict delay, settle, and replacement options.

## Renderer request and configuration

- [x] 48. Define and strictly decode a render request, attempt selection, renderer configuration, and failure mode.
- [x] 49. Select one test attempt by test ID and optional retry from a saved manifest.
- [x] 50. Resolve output dimensions, frame rate, container, codecs, pixel format, and quality against supported FFmpeg combinations.
- [ ] 51. Resolve theme and browser-layout defaults into concrete output geometry and validated colors.
- [x] 52. Resolve highlight and zoom call options against shared built-in defaults, enforce the maximum zoom scale, and retain requested target padding.
- [ ] 53. Resolve pacing, cursor, caption, and narration defaults, including bundled cursor and font assets.
- [x] 54. Apply strict or best-effort narration failure behavior and retain every omission as a typed diagnostic.

## Presentation timeline

- [x] 55. Compile and validate an identity edit map with no pacing changes.
- [ ] 56. Add a minimum visible duration around an action.
- [x] 57. Add a configurable post-action result hold.
- [x] 58. Preserve author hold intervals directly from the browser recording without adding a second renderer hold.
- [x] 59. Extend a segment to fit narration and its configured tail.
- [ ] 60. Compress one idle interval while preserving deliberate application animation.
- [x] 61. Select the active page video for each segment and map attempt time into that page's source-video time.
- [x] 62. Map cursor, click, highlight, zoom, caption, and audio timing through the same edit map.
- [ ] 63. Interpolate a deterministic cursor path with configured travel, settle, fade, and click timings.
- [ ] 64. Transform viewport geometry through scrolling, video scaling, letterboxing, crop, and camera transforms into output-canvas coordinates.

## Render plan, media, and FFmpeg

- [x] 65. Compile the selected page videos into a base video track without overlays.
- [x] 66. Add the deterministic SuiteCut cursor layer.
- [ ] 67. Add pointer press and configured click-effect states.
- [x] 68. Record resolved outline, fill, and spotlight highlight overlays in Chromium.
- [x] 69. Add output-frame-sampled zoom camera transforms, padding limits, and easing.
- [x] 70. Generate narration audio with local Kokoro WASM or `/usr/bin/say` on the capture worker, record the provider and voice, and avoid hosted speech services.
- [x] 71. Measure and place narration audio on the presentation timeline without adding source audio.
- [x] 72. Generate transparent caption-card assets with local Chromium and place them on the presentation timeline.
- [ ] 73. Compile all resolved tracks and assets into one plain deterministic render plan.
- [x] 74. Validate render assets, artifact roles, paths, configuration values, and that the output cannot overwrite an input.
- [x] 75. Compile FFmpeg arguments as an array and escape validated filter-expression values without invoking a shell.
- [x] 76. Run FFmpeg for the selected MP4 or WebM output and capture its process result.
- [x] 77. Write a render report that explains every edit and timing decision, records diagnostics, and redacts local project, home, temporary, input, and output paths from process output.

## Package flow

- [x] 78. Run Playwright through the SuiteCut CLI while forwarding arguments and process status.
- [x] 79. Render a selected saved-manifest attempt through the CLI with an explicit output path and optional configuration.
- [x] 80. Support reporter `outputFile`, path policy, and explicit step-category filtering.
- [x] 81. Export the fixture contract, extended `test` and `expect`, reporter, public types, and supported helpers from documented package paths.
- [x] 82. Document fixture composition, Playwright 1.59 or newer, `video: 'off'`, optional traces, the main-frame pointer scope, and v1's lack of source-audio mixing.
- [x] 83. Smoke-test the packed package from a consumer project without importing internal source or `dist` paths.

## Release evidence

- [x] 84. Record a fast Chromium test and prove first-frame event alignment without screenshot matching.
- [x] 85. Store a failed attempt, its retry, and parallel tests without cross-attempt event or page-state leakage.
- [x] 86. Record main-page, popup, and secondary-page videos and prove the rendered source switches through stable page IDs.
- [ ] 87. Verify pointer and target mapping in desktop and mobile viewports, including scrolling and retimed segments.
- [ ] 88. Prove narration extends a visual hold and idle compression shortens presentation time without slowing the Playwright test.
- [x] 89. Compare rendered cursor, click, highlight, and zoom frames at known event timestamps.
- [x] 90. Render the same manifest deterministically to supported MP4 and WebM outputs.
- [x] 91. Verify the recording path and first-frame mapping in Chromium, Firefox, and WebKit.

## Working rule

A checkpoint is complete only when its focused tests pass. Recording checkpoints also need one real Playwright run. Visual checkpoints need a rendered artifact that can be inspected. Release-evidence checkpoints must retain the manifest, media, report, and referenced frames needed to inspect the claim.
