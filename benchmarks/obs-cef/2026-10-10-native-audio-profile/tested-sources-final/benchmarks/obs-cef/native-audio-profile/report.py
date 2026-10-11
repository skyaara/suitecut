"""Produce the reviewable comparison from saved measurements."""
import json
import pathlib
import sys
from datetime import datetime
from zoneinfo import ZoneInfo
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

root=pathlib.Path(sys.argv[1])
names=['obs-matched','obs-tap-off','suitecut-matched','suitecut-final','suitecut-tap-off','obs-cold','suitecut-cold','suitecut-trace-fixed','obs-traced','suitecut-baseline-traced']
cases={name:json.loads((root/name/'analysis.json').read_text()) for name in names}
table=['| Case | Requested active seconds | Recording silence total / longest (ms) | RTMP silence total / longest (ms) | Mean CPU cores | Quota-throttled periods |',
       '|---|---:|---:|---:|---:|---:|']
summary={}
for name,data in cases.items():
    result=json.loads((root/name/'result.json').read_text())
    a=data['media']['recording.mkv']['silence'];b=data['media']['receiver.flv']['silence']
    table.append(f"| {name} | {result['durationRequestedMs']/1000:g} | {a['interiorTotalMs']} / {a['interiorMaximumMs']} | {b['interiorTotalMs']} / {b['interiorMaximumMs']} | {data['processTreeCPUMeanCores']:.2f} | {data['hostCPUStatDelta']['nr_throttled']} |")
    summary[name]={'recordingSilence':a,'streamSilence':b,'cpuCores':data['processTreeCPUMeanCores'],
                   'rssPeakMiB':data['processTreeRSSPeakMiB'],'nativeSourceCopy':data.get('nativeSourceCopy'),
                   'finalRecord':result.get('finalRecord'),'finalStream':result.get('finalStream')}
(root/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
labels=['OBS taps on','OBS taps off','SuiteCut before fixes','SuiteCut fixes, taps on','SuiteCut fixes, taps off']
selected=names[:5]
fig,axes=plt.subplots(1,2,figsize=(12,4.4))
for axis,key,title in zip(axes,['interiorTotalMs','interiorMaximumMs'],['Total unexpected silence','Longest unexpected interruption']):
    values=[cases[n]['media']['recording.mkv']['silence'][key] for n in selected]
    axis.barh(labels,values,color=['#536dfe','#536dfe','#00897b','#00897b','#00897b'])
    axis.invert_yaxis();axis.set_xlabel('Milliseconds, decoded AAC, 5 ms resolution');axis.set_title(title)
    for i,v in enumerate(values):axis.text(v+max(values)*.015,i,str(v),va='center',fontsize=9)
    axis.set_xlim(0,max(values)*1.16)
fig.suptitle('60-second browser workload: scrolling + page/controller stalls\nDifferent runs and CEF versions; these bars do not establish a causal before/after speedup',fontsize=10)
fig.tight_layout();fig.savefig(root/'audio-comparison.png',dpi=160);plt.close(fig)

text=f"""# OBS embedded CEF versus SuiteCut: dropped-audio investigation

Saved {datetime.now(ZoneInfo('Asia/Calcutta')).isoformat()}. All recordings, raw traces, collectors, tested source snapshots, profiles, package hashes, and validation logs are local to this directory. Previous sealed evidence was not changed.

**SuiteCut audio is not clean yet.** The final 60-second control with PCM tracing disabled recorded **1,095 ms of unexpected interior silence, with a longest interruption of 195 ms**, in both recording and RTMP. OBS's 60-second control without its profiling taps recorded **445 ms, longest 80 ms**. This establishes a remaining problem on this host; it does not establish a universal ranking, because the CEF versions and process architecture differ and there are too few repeated trials.

Two SuiteCut problems were addressed: small audio timestamp deviations were immediately turned into silent gaps; explicit Playwright CDP sessions received replies but lost events. Larger upstream capture/clock discontinuities remain. C++ encoding already runs separately from the browser and controller, so another language rewrite does not address the measured failure.

## What was actually tested

OBS **32.2.0**, from a checksum-pinned official OBS Ubuntu package, running a real **browser_source** with rerouted page audio. Its embedded browser is the OBS-patched **CEF/Chromium 127.0.6533.120** distribution named `6533-fix-stutter-and-osr-extra-info`. SuiteCut uses stock **CEF 154.0.34 / Chromium 154.0.8037.98**, native C++ libav encoding, and its private Playwright transport. This is a real embedded-CEF comparison, unlike the earlier OBS 30.2 external Chromium/XSHM control.

Both use 1920×1080, 30 FPS, H.264 veryfast/zerolatency, one video encoder thread per output, CRF 18 recording, 4,500 kbps RTMP, stereo 48 kHz AAC at 128 kbps. OBS links FFmpeg 6 and SuiteCut links FFmpeg 7, another version difference. OBS source packets are 1,024 frames; SuiteCut requests 960. Both use the same continuous AudioWorklet signal and Playwright wheel input. The host has a four-core CPU quota, a 16 GiB memory limit, software graphics, and no `/dev/dri` GPU device. Official OBS disables its CEF sandbox; SuiteCut keeps its renderer sandbox enabled.

Stress cases scroll every five seconds, block the page main thread for four seconds at about 20 seconds, and block the SDK controller for four seconds at about 40 seconds. The AudioWorklet uses its own sample clock. No encoders, analyzers, or builds were deliberately run concurrently with the main matched captures. The OBS traced diagnostic overlapped a read-only libcef strings search and is excluded from performance comparisons. Runtime locks prevented one attempted overlapping launch; that failed setup log was retained.

Playwright connection to OBS needs one benchmark-only adapter: its old CEF does not support `Browser.setDownloadBehavior`, which Playwright sends during initialization. The adapter acknowledges only that command; page input, evaluation and protocol traffic go to the real OBS CEF. This does not demonstrate OBS download or browser-context support. `probe-sixth/probe.json` and `browser-source.png` establish the loaded browser source and successful wheel input.

## Audio measurements

The signal is deterministic, continuous interpolated hash noise at amplitude 0.15, generated directly by AudioWorklet `currentFrame`. **There is no intentional interior silence.** Cold cases deliberately start sound after the outputs are running. Leading and trailing quiet regions are excluded from the table.

Every saved output is decoded. Unexpected silence means RMS below 0.001 in a 5 ms decoded window. Totals are audible quiet intervals, not an exact count of all lost waveform content: short corruptions, repeats, and non-silent missing content can also occur. Output lengths differ slightly from requested active time because recording and streaming start/stop separately.

{chr(10).join(table)}

![Decoded audio comparison](audio-comparison.png)

`suitecut-final` and `suitecut-tap-off` use both fixes. `suitecut-matched` precedes both. Cold cases precede the timestamp fix. The 30-second traced SuiteCut case has the CDP event fix but precedes timestamp smoothing. `suitecut-baseline-traced` completed and finalized both outputs, but its tracing collector failed because of the CDP event bug; its trace file is empty. Its **30.275 seconds of recording silence, longest 2.260 seconds**, are retained as a severe diagnostic failure, not hidden or counted as a successful benchmark. CPU quota and memory reclaim events increased during that diagnostic run. These results cannot demonstrate long-run reliability.

The two untapped controls are essential: both implementations still interrupt audio when PCM profiling writes are disabled. The difference between tapped and untapped runs cannot be attributed entirely to the instrumentation, because source activity and scheduler load varied. Traced/tapped results are not treated as observer-free production performance.

## Where the problem occurs

1. **CEF can lose actual samples.** In the initial SuiteCut matched run, raw callback timestamps skipped 80 ms and 20 ms. Correlation against the known generator recovered approximately 79.98 ms and 20 ms of corresponding sample sequence loss. The 80 ms event was outside the deliberate page/controller stalls. Both output queues reported zero overflow and zero late discards. Every supplied source sample was copied correctly: the exact native PCM comparison found zero interior mismatched frames. The missing timestamp coverage was 100 ms in each output.

2. **Timestamp holes can also exaggerate the audible loss.** In the 30-second SuiteCut trace, one cluster advances callback timestamps by 360 ms while high-confidence generated-frame anchors advance only about 4.46 ms beyond the amount of supplied PCM. Another cluster has a 420 ms timestamp discontinuity but about 8.31 ms of extra generated-frame progression. Some intervening packets contain silence or distortion, so these anchors do not prove that every sample was preserved; they do show that the whole timestamp hole was not simply missing waveform content. The previous implementation placed subsequent PCM at the new timestamps and inserted zero PCM across the full hole. The native output faithfully encoded that schedule, amplifying the audible interruption and delaying content.

3. **OBS also has upstream and mixed-output interruptions.** Its tapped matched capture records quiet PCM at the CEF ingress and additional interruptions at the mixer/output. Its untapped output still has an 80 ms interruption. OBS's source timing snaps small deviations below 70 ms onto its expected sample clock and uses its own adaptive mixer buffering; it cannot recover waveform content that CEF did not supply.

4. **Zero queue drops does not mean zero missing audio.** In the final untapped SuiteCut recording, `droppedAudioSamples=0`, `lateAudioSamples=0`, but `missingAudioSamples=38633` (about 804.85 ms). The stream reports 38636 missing samples (about 804.92 ms). These counters include output edges and uncovered silence as well as active-source failures, so the continuous fixture and decoded result are needed to classify the actual audible effect. The output still has a 195 ms interruption.

Chromium 154's inspected `services/audio/loopback_stream.cc`, `LoopbackSignalForwarder::GenerateMoreAudio()`, explicitly skips ahead when its timer falls behind. That is a plausible mechanism for whole capture intervals disappearing. The successful page-scoped trace includes browser/renderer events and **18 `Glitch!` notifications**, but does not include the audio utility process's `GenerateMoreAudio Is Behind` events. Therefore the exact skip branch was **not directly observed**; this report does not claim a complete internal Chromium stack attribution. Glitch notifications can repeat across threads/streams, so their durations are not summed as independently dropped audio.

The trace also shows playback parameters of **44.1 kHz / 512 frames**, while the fixture and CEF capture use 48 kHz. This reveals additional rate conversion and clock boundaries even though the requested capture format is 48 kHz. It is a useful investigation target, not proof that changing the rate alone fixes the problem. CEF's `CefAudioCapturer::Capture` also discards Chromium's supplied glitch metadata instead of exposing it in the audio packet callback.

**Callback timings narrow the responsible layer further.** In the successful SuiteCut audio trace, the browser-side `AudioInputDevice::AudioThreadCallback::Process` span, which includes delivery through CEF into the framework callback, has a median of **0.069 ms**, a 99th percentile of **1.025 ms**, and a maximum of **6.503 ms**. The renderer's `AudioOutputDevice::FireRenderCallback` instead reaches **229.944 ms**, with about **138.904 ms of recorded thread CPU time** in that event; its median is 0.258 ms. This trace supports a stall in Chromium's renderer audio-generation path rather than a 200 ms encoder or framework capture-callback stall. It does not identify whether the renderer delay is AudioWorklet execution, V8/GC, resampling, or another internal operation. The separate OBS traced diagnostic reports 34.533 ms maximum render-callback time and 65.766 ms maximum input-delivery time, but that run overlapped the documented diagnostic search and is not a clean comparative timing benchmark. `callback-profiling.json` preserves the distributions and slowest events.

## Fixes made and their limits

**Bounded packet timestamp smoothing:** `SuiteCutAudioClock::NormalizePacket` follows the supplied 48 kHz frame count when the raw timestamp differs by less than 70 ms from the expected packet start, following OBS's source-timing approach. The cumulative correction is bounded below 70 ms; larger differences preserve source timing. Original `pts_ms` and `raw_mapped_ms` remain in diagnostic CSV alongside effective `mapped_ms`. The final tapped run applied corrections to 1,021 packets, with a maximum correction of 60 ms; exact native source-copy mismatches remained zero.

This prevents isolated small PTS steps from immediately inserting zeros. It **does not reconstruct lost samples, fix large timestamp jumps, or guarantee lower total silence**. Accumulated timing differences can reach the bound and require a larger resynchronization; the final real runs were worse than the earlier favorable run. The new media regression feeds continuous nonzero PCM with a 20 ms timestamp step and verifies a complete 9,600-frame output with zero uncovered, late or dropped frames. Clock tests also verify the 70 ms boundary, larger-gap preservation, reset, and fractional 1,024-frame timing.

**CDP event routing:** explicit `context.newCDPSession(page)` aliases now receive their page's agent events, with alias-relative nested worker session IDs. The regression checks console events, isolation from another page, tracing completion, detach, and continued primary Playwright operation. Real audio tracing now flushes successfully. Extra sessions continue to share CEF agent domain state; this is not a claim of fully independent CDP agents or full upstream Playwright conformance.

**Readiness documentation:** corrected the old beta documentation that said recording waits for source sound. The implementation waits for encoders, muxer, and video readiness, then emits timestamped zero PCM for silent pages. Stock CEF may still create its loopback only after audibility. Waiting for the engine cannot guarantee preservation of the first sound.

## First-sound startup

Both cold cases use worklet `firstFrame=0`. A high-confidence 960-sample boundary correlation, beginning 5 ms after the first audible sample and extrapolating back, estimates SuiteCut's first retained generated frame at 4,458 (about **92.9 ms** into generated audio), and OBS's at 7,235 (about **150.7 ms**). These are short-run estimates through resampling, not sub-sample laboratory measurements. They exclude deliberately silent output startup and quantify the missing beginning of the generated signal.

The native cold run also had a **395 ms interior interruption**, while this OBS cold run had only 5 ms in the recording and no 5 ms stream interruption. That is another unfavorable SuiteCut result, not a startup pass. The existing persistent-capture CEF patch addresses audibility-triggered initialization, but is a **candidate source patch, not compiled into the tested binary and not validated as a fix for ongoing gaps**.

## What still needs fixing

The remaining primary work is the CEF capture path and its timestamp/content relationship, including missed loopback deadlines, delayed/retained PCM, and first-sound initialization. A capture-level fix must preserve recoverable queued PCM with correct capture times, explicitly account for unrecoverable loss, and verify A/V alignment. Simply snapping all large timestamps onto a continuous sample clock could hide missing intervals and introduce large A/V drift; that was not done.

The OBS package's patched CEF versus SuiteCut's newer stock CEF remains an important confound. Compare the same CEF build and packet size before blaming Chromium version alone or adopting an older release. Port/build persistent capture and evaluate bounded catch-up together with retained loopback history; do not delete Chromium's skip logic without confirming that old PCM is still available. The current host cannot build and validate a full Chromium/CEF fork within its disk budget, and no custom rebuilt CEF was tested here.

Opt-in native diagnostic PCM writes are synchronous on the audio callback, and the OBS passive taps also perform buffered writes. They can perturb scheduling; a bounded asynchronous diagnostic writer is a separate profiling improvement. Neither untapped control eliminates the real interruptions.

MP4, Matroska, and FLV multiplex timestamps and tracks; they cannot recover uncaptured audio or decide whether a bad source timestamp represents real loss. More encoder threads, a language change, or a different container is not a demonstrated fix. SuiteCut's lower CPU use here does not establish better audio quality than OBS.

## Validation and saved artifacts

- Native CMake build and all **five CTest cases pass**, including clock and media jitter regressions.
- **12 real CEF/Playwright cases pass**, including the new explicit-session event regression.
- TypeScript type checking and lint pass; **309 SDK tests in 37 files pass**.
- **Five analyzer tests pass:** timestamp-only jitter, 120 ms loss, 20 ms repetition, inserted silence, and sequence reacquisition after a 1.5-second loss. Generated sample identity uses normalized correlation ≥0.95. Low-confidence windows remain explicit; global reacquisition avoids mistaking loss of the correlator's local lock for permanent missing audio.
- `media-validation.json` contains full video decoding, actual probes and frame markers; per-file logs retain decode errors. Forced page stalls intentionally freeze both visual markers, so marker holds are not a standalone A/V-sync failure. This investigation does **not** establish complete A/V synchronization under every load.
- `summary.json`, each case's `analysis.json`, source/mixer/pre-encoder `.csv`/`.s16`, decoded AAC `.s16`, sequence JSONL, status samples, Chromium trace JSONL, process samples, and recordings preserve the measurements. Raw source traces can include warmup and shutdown beyond the output window, so their total quiet duration must not be compared directly to a shorter output duration.
- `runtime-provenance.json`, `obs-package.json`, `obs-dependencies.json`, `obs-effective-profile`, and `upstream` record versions, SHA-256s, ABI headers and reviewed source. The official package SHA is `629de31a94385c82037fd4932a1678210a2cedfb65fd1c0cb36f308acf99f53f`.
- `tested-sources-before-session-fix` preserves the original relevant implementation and binary hash. `tested-sources-final` preserves final native sources, the executable, collectors, SDK source/build, package lock and tests. The initial snapshot-path TypeScript failure and all OBS setup failures remain in logs; the final checks pass.

RSS is summed across processes and can double-count shared mappings. These short runs do not prove absence of CEF/library leaks or stable performance over hours. The older 30-minute external-browser OBS comparison and the interrupted 14-minute native run remain separate historical evidence; neither is relabeled as a completed matched OBS-CEF soak.

## Reproduce focused runs

The isolated OBS runtime uses `.suitecut/obs-cef-profile/root` for the pinned official package and its downloaded dependencies, and `.suitecut/obs-deps/root` for Qt, Xvfb and PulseAudio. It does not install system packages or modify the user's normal OBS profile. Review the saved effective profile and package hashes before recreating that environment.

```sh
PROFILE_DIRECTORY=/absolute/new-case PROFILE_BACKEND=obs PROFILE_DURATION_MS=60000 PROFILE_STRESS=1 PROFILE_TAP=0 python3 benchmarks/obs-cef/native-audio-profile/runtime.py benchmarks/obs-cef/native-audio-profile/capture.mjs
env -u SUITECUT_AUDIO_TRACE_DIRECTORY PROFILE_DIRECTORY=/absolute/new-case PROFILE_BACKEND=suitecut PROFILE_DURATION_MS=60000 PROFILE_STRESS=1 python3 benchmarks/native-engine/runtime.py benchmarks/obs-cef/native-audio-profile/capture.mjs
OPENBLAS_NUM_THREADS=1 python3 benchmarks/obs-cef/native-audio-profile/analyze.py /absolute/new-case
```

For native PCM attribution, set `SUITECUT_AUDIO_TRACE_DIRECTORY` to the same existing case directory. `PROFILE_COLD=1` starts sound after outputs; `PROFILE_CHROMIUM_TRACE=1` enables page-scoped audio tracing. Run captures serially, keep analyzers/builds outside the capture window, and use fresh evidence paths.
"""
(root/'report.md').write_text(text)
print('Wrote report.md, summary.json and audio-comparison.png')
