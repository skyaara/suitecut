# OBS Browser Source audio review

Reviewed 10 October 2026 (Asia/Calcutta). This is a pinned source-code review,
not a new OBS Browser Source recording benchmark. Production SuiteCut code was
not changed by this review.

## What OBS embeds

OBS's Browser Source plugin (`obs-browser`) embeds **CEF**, Chromium Embedded
Framework. Browser sources with audio rerouting enabled return a
`CefAudioHandler`; when disabled, the plugin leaves audio on Chromium's normal
device-output path, which may be recorded separately through desktop audio.

The installed benchmark OBS package has **no Browser Source plugin**. Earlier
host comparisons capture an external Chromium window through XSHM/Xvfb. They
cannot establish how OBS's embedded CEF audio behaves in our failing workload.
See the preserved [benchmark scope](../README.md).

Reviewed upstream commits:

- `obs-browser`: `9ecf652c9df4a0cace934917ce5ae754da58a529` (2026-10-08T17:14:52Z).
- `obs-studio`: `7d98bebe1115b12608837b622164ea5fe6d9483a` (2026-10-08T20:03:49Z).
- Upstream OBS 30.2.3 core: `144599fbff18e348652ccadfc3ca08794a03d970`. The installed Debian package's
  downstream patches were not audited. Its upstream core also has the 70 ms
  timestamp-smoothing threshold.

## Capture and startup

[Browser audio callbacks](https://github.com/obsproject/obs-browser/blob/9ecf652c9df4a0cace934917ce5ae754da58a529/browser-client.cpp#L567-L605) cache format information in
`OnAudioStreamStarted` and forward every valid packet from
`OnAudioStreamPacket` as **planar float PCM** to `obs_source_output_audio`.
The CEF millisecond PTS is converted to nanoseconds. There is no sound-amplitude
threshold or test that rejects zero-valued packets. Stop/error handlers in this
plugin implementation do not themselves stop OBS recording.

[Audio parameters](https://github.com/obsproject/obs-browser/blob/9ecf652c9df4a0cace934917ce5ae754da58a529/browser-client.cpp#L631-L639) use the OBS output sample rate
and channel layout, with 1024 frames per callback. At 48 kHz that is 21.33 ms of
samples, compared with SuiteCut's 960-frame/20 ms callback blocks. This block
size is not a measured end-to-end capture delay.

[Browser tick/render](https://github.com/obsproject/obs-browser/blob/9ecf652c9df4a0cace934917ce5ae754da58a529/obs-browser-source.cpp#L567-L629) waits for the CEF startup
event before creating the browser and renders its available texture independently
of audio. Neither callback tests for sound or the first audio packet. The OBS
recording timeline is controlled by its output engine, not by a browser's
first sound. A silent source, a source with no active audio stream, or a browser
still loading does not impose a first-sound gate on recording. If no browser
texture exists, that source has no texture to draw yet; this does not certify
that webpage/video initialization has completed.

## Continuous audio clock and silence

OBS has a [dedicated audio I/O thread](https://github.com/obsproject/obs-studio/blob/7d98bebe1115b12608837b622164ea5fe6d9483a/libobs/media-io/audio-io.c#L158-L244) that advances its
output clock by sample count, independently of CEF callback arrivals. Each mix
buffer is cleared to zero before requesting source audio. The
[core mixer](https://github.com/obsproject/obs-studio/blob/7d98bebe1115b12608837b622164ea5fe6d9483a/libobs/obs-audio.c#L620-L690) adds available, non-pending source audio to these
buffers and skips pending sources. That provides a valid silent output when
there is no contribution; delivered zero PCM is likewise valid source input.
Silence is not treated as a signal that video must wait.

Buffers and scheduling can smooth delayed arrivals, but a running output clock
does not guarantee the browser produced every required audio sample. OBS cannot
reconstruct narration that a stalled page never synthesized.

## Timing and recovery

OBS's [source timestamp handling](https://github.com/obsproject/obs-studio/blob/7d98bebe1115b12608837b622164ea5fe6d9483a/libobs/obs-source.c#L1485-L1693) aligns input
clocks to its system clock and tracks the timestamp expected from the previous
sample count. Differences **under 70 ms** can be snapped to the expected sample
position, placing PCM continuously instead of preserving every small timestamp
hole. It also resets timing/data on sufficiently large discontinuities. The
[same threshold exists in upstream 30.2.3](https://github.com/obsproject/obs-studio/blob/144599fbff18e348652ccadfc3ca08794a03d970/libobs/obs-source.c#L1396-L1400). This
threshold is a recovery policy, not 70 ms of mandatory recording latency.

The mixer supports [adaptive or fixed buffering](https://github.com/obsproject/obs-studio/blob/7d98bebe1115b12608837b622164ea5fe6d9483a/libobs/obs-audio.c#L315-L448), marks
insufficient input as pending, discards stale input, and clears stopped-source
buffers so they can recover. The [default maximum](https://github.com/obsproject/obs-studio/blob/7d98bebe1115b12608837b622164ea5fe6d9483a/libobs/obs.c#L1615-L1626) is 45
1024-frame ticks when no explicit limit is supplied: 960 ms at 48 kHz. That is a
maximum allowed buffering budget, not an always-applied one-second delay. A
caller may configure another limit. Larger buffering trades latency for more
tolerance of scheduling jitter.

## Comparison with current SuiteCut

| Behavior | OBS Browser Source/core | Current SuiteCut |
| --- | --- | --- |
| Browser capture | CEF video and audio callbacks | CEF video and audio callbacks |
| Audio entry format | Planar float PCM into core mixer | Float CEF PCM converted to S16 into native outputs |
| Silent PCM | Accepted | Accepted after startup, but cannot satisfy the startup sound threshold |
| Start with no browser audio | Output clock can continue with silence | Audio-enabled realtime output waits for sound and times out |
| Clock/scheduling | Dedicated audio mixer thread; video renders separately | Audio/video encoding share one native worker per output |
| Small timestamp holes | Can smooth deviations under 70 ms | Places samples at their mapped original timestamps; uncovered intervals become zeros |
| Jitter handling | Bounded adaptive/fixed buffering and reset/recovery logic | Fixed 150 ms realtime jitter delay, bounded queues, missing/late counters |

SuiteCut already performs capture, encoding and muxing natively. C++ itself is
not the differentiator. OBS has a more developed clock/mixer and source-recovery
policy. SuiteCut's worker chooses whichever of audio/video is due, then encodes
it; a slow video encode delays that output's audio scheduling. This is a code
observation, not proof that it caused our measured CEF source discontinuities.

Our earlier first-audible-sample gate was too strict for silent audio. Waiting
for the first packet would accept delivered silent PCM, but would still block a
page with no CEF audio stream. Matching OBS requires separating **output/audio
engine readiness** from **browser source audibility or packet arrival** and
letting a prepared output clock represent intentional silence. Recording can
start while the browser is silent; later audio joins the existing timeline.

A timestamp hole in a CEF trace is not alone proof of missing audible content.
OBS may stitch a small timestamp discontinuity that SuiteCut currently renders
as a gap. To distinguish genuine loss from timestamp jitter, compare sample
sequences, browser phase/markers and the input clock before applying smoothing.
Blindly compressing all gaps would conceal genuine pauses or damage A/V sync.

The 7.73-second intermittent failure remains unresolved. This source review does
not establish that OBS Browser Source would pass it, or that copying buffering
and timestamp code would fix it. A matched embedded-CEF runtime test is still
needed, with source callbacks, audio-start/stop events, sample-count continuity,
output PCM and video timestamps all retained.

## Saved evidence

`upstream/` contains the downloaded files and immutable commit identities, plus
the repository trees used to locate them. `source-references.json` contains
permalinks to the reviewed code. `suitecut-source/` snapshots the current
implementation compared here. `checksums.sha256` covers all saved evidence.
Prior sealed benchmark directories and archives are untouched.
