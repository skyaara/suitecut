# SuiteCut CEF / OBS benchmark — 9 October 2026

Client timezone: Asia/Calcutta. Raw timestamps are UTC.

This report compares the available production recording pipelines. Valid media does not establish the configured motion rate. OBS uses Chromium/XSHM/x264; SuiteCut uses CEF/I420/VP9. Browser versions, codec behavior, audio-track presence, and encoder settings differ. No codec-normalized speedup is claimed.

| Case | Backend | Valid/attempted | Distinct FPS median | Capture FPS | Longest hold ms | CPU % | RSS MiB | Finalize s |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| 720p60-static | obs | 5/5 | — | — | — | 241.66 | 1489.81 | 0.65 |
| 720p60-static | native | 5/5 | — | 57.91 | — | 348.40 | 1339.20 | 11.06 |
| 720p60-canvas | obs | 5/5 | 47.00 | — | 150.00 | 282.09 | 1529.20 | 0.69 |
| 720p60-canvas | native | 5/5 | 0.51 | 55.93 | 4500.00 | 352.57 | 1353.15 | 16.05 |
| 720p60-scroll | obs | 5/5 | 33.32 | — | 83.33 | 308.15 | 1612.65 | 0.72 |
| 720p60-scroll | native | 4/5 | 0.41 | 56.09 | 5200.00 | 355.31 | 1384.76 | 23.14 |
| 1080p30-canvas | obs | 5/5 | 27.62 | — | 66.67 | 304.84 | 1809.84 | 1.16 |
| 1080p30-canvas | native | 5/5 | 2.25 | 29.59 | 4266.67 | 358.96 | 1573.88 | 17.43 |
| 1080p60-canvas | obs | 5/5 | 22.05 | — | 133.33 | 356.70 | 1840.51 | 0.90 |
| 1080p60-canvas | native | 5/5 | 0.33 | 53.26 | 5816.67 | 370.07 | 1600.68 | 39.34 |
| 1080p60-scroll | obs | 5/5 | 24.88 | — | 100.00 | 375.41 | 1943.25 | 0.94 |
| 1080p60-scroll | native | 5/5 | 0.31 | 50.83 | 6583.33 | 372.50 | 1664.66 | 43.84 |
| 4k30-canvas | obs | 5/5 | 4.27 | — | 300.00 | 397.83 | 3261.16 | 3.82 |
| 4k30-canvas | native | 0/5 | — | 15.56 | — | 356.49 | 2871.22 | — |
| 4k60-canvas | obs | 5/5 | 1.50 | — | 1883.33 | 395.08 | 3328.03 | 18.10 |
| 4k60-canvas | native | 0/5 | — | 16.05 | — | 347.97 | 2869.32 | — |
| 4k60-scroll | obs | 5/5 | 1.41 | — | 1700.00 | 397.69 | 3424.39 | 17.22 |
| 4k60-scroll | native | 0/5 | — | 14.24 | — | 346.58 | 2941.65 | — |

Five trials per case; ten-second active workloads; one-second page warmup and half-second encoder warmup. Alternating backend order, serial execution. Actual wheel events; fixed binary animation markers decoded from saved media. Static content has no distinct-FPS target. CPU/RSS cover capture and finalization, not solely the steady active interval. PNG and SVG chart exports are saved beside this report. Raw resource samples are retained.

CPU quota: four equivalents. Memory limit: 16 GiB. Graphics: Mesa llvmpipe. Software encoding. Five-sample percentiles are descriptive. Aggregates retain failed counts and missing observations.

## Functional / streaming results

8 runs; 56 checks, 0 failed checks, 0 run errors. Counts: outcome.json.

### obs-record-audio-false

- typing: **passed**. `{"markerCount": 0, "scrollY": 0, "scrollEvents": 0, "keyEvents": 18, "clicks": 0, "value": "SuiteCut OBS input", "choice": "First", "secondary": false}`
- click: **passed**. `{"markerCount": 0, "scrollY": 0, "scrollEvents": 0, "keyEvents": 18, "clicks": 1, "value": "SuiteCut OBS input", "choice": "First", "secondary": false}`
- in-page select control: **passed**. `{"markerCount": 0, "scrollY": 0, "scrollEvents": 0, "keyEvents": 18, "clicks": 1, "value": "SuiteCut OBS input", "choice": "Second", "secondary": false}`
- wheel scrolling: **passed**. `{"markerCount": 0, "scrollY": 1800, "scrollEvents": 10, "keyEvents": 18, "clicks": 1, "value": "SuiteCut OBS input", "choice": "Second", "secondary": false}`
- scroll to element and back to top: **passed**. `null`
- navigation and return: **passed**. `null`
- source switching: **passed**. `null`
- encoded audio disabled: **passed**. `{"samples": 504832, "seconds": 10.517333333333333, "rms": 0, "peak": 0, "tone440": 0, "tone880": 0}`
- decoded source identity: **passed**. `{"red": 240, "green": 2, "blue": 67, "changes": [{"frame": 0, "identity": "other"}, {"frame": 3, "identity": "primary"}, {"frame": 139, "identity": "navigation-secondary"}, {"frame": 140, "identity": "primary"}, {"frame": 187, "identity": "other"}, {"frame": 190, "identity": "navigation-secondary"}, {"frame": 191, "identity": "second-source"}, {"frame": 258, "identity": "other"}, {"frame": 260, "identity": "primary"}]}`
- recorded input and scrolling states: **passed**. `{"observedMask": 15, "typedComplete": true, "clicked": true, "selected": true, "scrolled": true}`

### native-record-audio-false

- typing: **passed**. `{"markerCount": 0, "scrollY": 0, "scrollEvents": 0, "keyEvents": 18, "clicks": 0, "value": "SuiteCut OBS input", "choice": "First", "secondary": false}`
- click: **passed**. `{"markerCount": 0, "scrollY": 0, "scrollEvents": 0, "keyEvents": 18, "clicks": 1, "value": "SuiteCut OBS input", "choice": "First", "secondary": false}`
- in-page select control: **passed**. `{"markerCount": 0, "scrollY": 0, "scrollEvents": 0, "keyEvents": 18, "clicks": 1, "value": "SuiteCut OBS input", "choice": "Second", "secondary": false}`
- wheel scrolling: **passed**. `{"markerCount": 0, "scrollY": 1800, "scrollEvents": 10, "keyEvents": 18, "clicks": 1, "value": "SuiteCut OBS input", "choice": "Second", "secondary": false}`
- scroll to element and back to top: **passed**. `null`
- navigation and return: **passed**. `null`
- source switching: **passed**. `null`
- encoded audio disabled: **passed**. `{"audioStream": false, "rms": 0, "peak": 0, "tone440": 0, "tone880": 0}`
- decoded source identity: **passed**. `{"red": 342, "green": 1, "blue": 83, "changes": [{"frame": 0, "identity": "other"}, {"frame": 2, "identity": "primary"}, {"frame": 190, "identity": "navigation-secondary"}, {"frame": 191, "identity": "primary"}, {"frame": 270, "identity": "other"}, {"frame": 271, "identity": "second-source"}, {"frame": 354, "identity": "primary"}]}`
- recorded input and scrolling states: **passed**. `{"observedMask": 15, "typedComplete": true, "clicked": true, "selected": true, "scrolled": true}`

### obs-record-audio-true

- typing: **passed**. `{"markerCount": 0, "scrollY": 0, "scrollEvents": 0, "keyEvents": 18, "clicks": 0, "value": "SuiteCut OBS input", "choice": "First", "secondary": false}`
- click: **passed**. `{"markerCount": 0, "scrollY": 0, "scrollEvents": 0, "keyEvents": 18, "clicks": 1, "value": "SuiteCut OBS input", "choice": "First", "secondary": false}`
- in-page select control: **passed**. `{"markerCount": 0, "scrollY": 0, "scrollEvents": 0, "keyEvents": 18, "clicks": 1, "value": "SuiteCut OBS input", "choice": "Second", "secondary": false}`
- wheel scrolling: **passed**. `{"markerCount": 0, "scrollY": 1800, "scrollEvents": 10, "keyEvents": 18, "clicks": 1, "value": "SuiteCut OBS input", "choice": "Second", "secondary": false}`
- scroll to element and back to top: **passed**. `null`
- navigation and return: **passed**. `null`
- source switching: **passed**. `null`
- encoded audio tones: **passed**. `{"samples": 508928, "seconds": 10.602666666666666, "rms": 0.07424821704626083, "peak": 0.12453900277614594, "tone440": 0.11312692612409592, "tone880": 0.1131206527352333}`
- decoded source identity: **passed**. `{"red": 241, "green": 2, "blue": 68, "changes": [{"frame": 0, "identity": "other"}, {"frame": 6, "identity": "primary"}, {"frame": 142, "identity": "navigation-secondary"}, {"frame": 144, "identity": "primary"}, {"frame": 192, "identity": "other"}, {"frame": 193, "identity": "second-source"}, {"frame": 261, "identity": "other"}, {"frame": 263, "identity": "primary"}]}`
- recorded input and scrolling states: **passed**. `{"observedMask": 15, "typedComplete": true, "clicked": true, "selected": true, "scrolled": true}`

### native-record-audio-true

- typing: **passed**. `{"markerCount": 0, "scrollY": 0, "scrollEvents": 0, "keyEvents": 18, "clicks": 0, "value": "SuiteCut OBS input", "choice": "First", "secondary": false}`
- click: **passed**. `{"markerCount": 0, "scrollY": 0, "scrollEvents": 0, "keyEvents": 18, "clicks": 1, "value": "SuiteCut OBS input", "choice": "First", "secondary": false}`
- in-page select control: **passed**. `{"markerCount": 0, "scrollY": 0, "scrollEvents": 0, "keyEvents": 18, "clicks": 1, "value": "SuiteCut OBS input", "choice": "Second", "secondary": false}`
- wheel scrolling: **passed**. `{"markerCount": 0, "scrollY": 1800, "scrollEvents": 10, "keyEvents": 18, "clicks": 1, "value": "SuiteCut OBS input", "choice": "Second", "secondary": false}`
- scroll to element and back to top: **passed**. `null`
- navigation and return: **passed**. `null`
- source switching: **passed**. `null`
- encoded audio tones: **passed**. `{"samples": 718848, "seconds": 14.976, "rms": 0.07375441491603851, "peak": 0.12699690461158752, "tone440": 0.11277654021978378, "tone880": 0.033380478620529175}`
- decoded source identity: **passed**. `{"red": 362, "green": 2, "blue": 82, "changes": [{"frame": 0, "identity": "other"}, {"frame": 3, "identity": "primary"}, {"frame": 208, "identity": "navigation-secondary"}, {"frame": 210, "identity": "primary"}, {"frame": 291, "identity": "second-source"}, {"frame": 373, "identity": "primary"}]}`
- recorded input and scrolling states: **passed**. `{"observedMask": 15, "typedComplete": true, "clicked": true, "selected": true, "scrolled": true}`

### obs-stream-20000

- decoded receiver motion: **passed**. `{"decodedFrames": 1224, "markerTransitions": 558, "distinctFps": 27.288732394366196, "targetMotionStatus": "below target", "repeatFraction": 0.5441176470588236, "longestHoldMs": 267.2941176470588, "originalWallDurationDistinctFps": 27.706695336438788, "originalWallDurationLongestHoldMs": 263.26191407843135, "mediaSeconds": 20.448, "denominatorCorrection": "Playback rate uses received media duration, including drained output; original wall-duration calculation retained."}`
- receiver media and timestamps: **passed**. `{"durationSeconds": 20.448, "bytes": 11529353, "averageMuxedKbps": 4510.701486697965, "encodedPackets": 1224, "encodedFps": 60.09828009828011, "presentationReorders": 601, "duplicatePresentationTimes": 0, "nonIncreasingDecodeTimes": 0}`
- receiver audible page tone: **passed**. `{"samples": 977920, "seconds": 20.373333333333335, "rms": 0.051480405032634735, "peak": 0.2305591106414795, "tone440": 0.025473657995462418, "tone880": 0.0001481200015405193}`
- decoded audio and video sync: **passed**. `{"matchedFlashes": 18, "audioBursts": 18, "medianAudioMinusVideoMs": 33.333333333333215, "p95AbsoluteOffsetMs": 39.16666666666717, "firstThirdMedianMs": 28.333333333333545, "lastThirdMedianMs": 36.66666666666618, "offsetsMs": [36.66666666666663, 20.000000000000018, 36.66666666666707, 20.000000000000462, 36.66666666666707, 3.3333333333338544, 20.00000000000135, 36.66666666666707, -13.333333333331865, -19.999999999999574, 30.000000000001137, 30.000000000001137, 36.66666666666707, 36.66666666666707, 19.999999999999574, 36.66666666666529, 53.333333333334565, 36.66666666666529]}`

### native-stream-20000

- decoded receiver motion: **passed**. `{"decodedFrames": 192, "markerTransitions": 143, "distinctFps": 6.610882529702741, "targetMotionStatus": "below target", "repeatFraction": 0.25520833333333337, "longestHoldMs": 563.3072916666666, "originalWallDurationDistinctFps": 7.113466158293527, "originalWallDurationLongestHoldMs": 523.5082659374999, "mediaSeconds": 21.631, "denominatorCorrection": "Playback rate uses received media duration, including drained output; original wall-duration calculation retained."}`
- receiver media and timestamps: **passed**. `{"durationSeconds": 21.631, "bytes": 378317, "averageMuxedKbps": 139.91660117424067, "encodedPackets": 192, "encodedFps": 10.342773596144475, "presentationReorders": 0, "duplicatePresentationTimes": 0, "nonIncreasingDecodeTimes": 0}`
- receiver audible page tone: **passed**. `{"samples": 1038336, "seconds": 21.632, "rms": 0.04845216125249863, "peak": 0.23115839064121246, "tone440": 0.025354571640491486, "tone880": 0.00019941528444178402}`
- decoded audio and video sync: **passed**. `{"matchedFlashes": 7, "audioBursts": 23, "medianAudioMinusVideoMs": 6.666666666667709, "p95AbsoluteOffsetMs": 70.6666666666651, "firstThirdMedianMs": -16.666666666666607, "lastThirdMedianMs": 16.66666666666572, "offsetsMs": [23.333333333333428, -56.66666666666664, 23.333333333333428, 6.666666666667709, -13.333333333331865, -43.333333333333, 76.66666666666444]}`

### obs-stream-300000

- decoded receiver motion: **passed**. `{"decodedFrames": 18020, "markerTransitions": 6377, "distinctFps": 21.229634265701673, "targetMotionStatus": "below target", "repeatFraction": 0.6461154273029966, "longestHoldMs": 666.7746947835738, "originalWallDurationDistinctFps": 21.23918452064677, "originalWallDurationLongestHoldMs": 666.4748777957826, "mediaSeconds": 300.382, "denominatorCorrection": "Playback rate uses received media duration, including drained output; original wall-duration calculation retained."}`
- receiver media and timestamps: **passed**. `{"durationSeconds": 300.382, "bytes": 170502620, "averageMuxedKbps": 4540.9543847500845, "encodedPackets": 18020, "encodedFps": 60.00672698754176, "presentationReorders": 8855, "duplicatePresentationTimes": 0, "nonIncreasingDecodeTimes": 0}`
- receiver audible page tone: **passed**. `{"samples": 14414848, "seconds": 300.3093333333333, "rms": 0.0514666773378849, "peak": 0.23149748146533966, "tone440": 0.025488046929240227, "tone880": 0.0002645560016389936}`
- decoded audio and video sync: **passed**. `{"matchedFlashes": 284, "audioBursts": 303, "medianAudioMinusVideoMs": 23.333333333340533, "p95AbsoluteOffsetMs": 73.33333333333209, "firstThirdMedianMs": 39.99999999999915, "lastThirdMedianMs": 16.66666666665151, "offsetsMs": [-33.333333333333215, -43.333333333333, 43.333333333333, -73.33333333333236, -56.66666666666664, 9.999999999999787, 26.666666666667282, -40.000000000000924, 60.0000000000005, 26.666666666667282, 59.99999999999872, -23.333333333333428, 10.000000000001563, 43.333333333336554, 10.000000000001563, -73.33333333333059, -30.000000000001137, -30.000000000001137, 19.999999999999574, 70.00000000000028, 70.00000000000028, -13.333333333335418, 13.333333333335418, 46.666666666666856, 30.000000000001137, 13.333333333335418, 13.333333333335418, 63.333333333332575, 46.666666666666856, 46.666666666666856, 30.000000000001137, 30.000000000001137, 46.666666666666856, 30.000000000001137, 39.99999999999915, 53.33333333332746, 53.33333333332746, 53.33333333332746, 36.66666666666174, 23.333333333333428, 39.99999999999915, 56.66666666666487, 39.99999999999915, 39.99999999999915, 56.66666666666487, 39.99999999999915, 39.99999999999915, 56.66666666666487, 39.99999999999915, -9.99999999999801, 39.99999999999915, 39.99999999999915, 39.99999999999915, 56.66666666666487, 6.666666666667709, -26.66666666666373, -10.000000000005116, -13.333333333335418, 13.333333333335418, -10.000000000005116, 23.333333333326323, -43.333333333336554, 23.333333333326323, 56.66666666665776, 23.333333333326323, 56.66666666665776, 39.99999999999204, 39.99999999999204, 39.99999999999204, 39.99999999999204, 39.99999999999204, 23.333333333326323, 39.99999999999204, 43.333333333336554, 53.33333333334167, 56.66666666665776, 73.33333333332348, 73.33333333332348, 56.66666666665776, 73.33333333332348, 39.99999999999204, 73.33333333332348, 6.666666666660603, 69.99999999999318, 86.6666666666589, 69.99999999999318, 69.99999999999318, 103.33333333332462, 86.6666666666589, 86.6666666666589, 86.6666666666589, 86.6666666666589, 69.99999999999318, 86.6666666666589, 69.99999999999318, 69.99999999999318, 69.99999999999318, 86.6666666666589, 53.33333333332746, 86.6666666666589, 53.33333333332746, 86.6666666666589, 69.99999999999318, 86.6666666666589, 69.99999999999318, 69.99999999999318, 69.99999999999318, 69.99999999999318, 19.99999999999602, 53.33333333332746, 53.33333333332746, 53.33333333332746, 53.33333333332746, 36.66666666666174, 69.99999999999318, 69.99999999999318, 36.66666666666174, 53.33333333332746, 53.33333333334167, 70.0000000000216, 3.3333333333303017, -10.000000000019327, 23.333333333312112, 6.666666666660603, -26.666666666670835, 40.000000000020464, 23.333333333340533, 56.66666666667197, 23.333333333340533, 40.000000000020464, 40.000000000020464, 56.66666666667197, 40.000000000020464, 40.000000000020464, 23.333333333340533, 23.333333333340533, 6.666666666689025, 56.66666666667197, 23.333333333340533, 56.66666666667197, 6.666666666689025, 40.000000000020464, 23.333333333340533, 40.000000000020464, 6.666666666689025, -26.666666666670835, 23.333333333340533, 23.333333333340533, 40.000000000020464, 40.000000000020464, 40.000000000020464, 56.66666666667197, 6.666666666689025, 40.000000000020464, 23.333333333340533, 26.666666666670835, 30.000000000001137, -50.00000000001137, -36.66666666666174, -53.33333333334167, -20.000000000010232, -20.000000000010232, -36.66666666666174, -20.000000000010232, -3.3333333333303017, -3.3333333333303017, 13.333333333321207, 13.333333333321207, -20.000000000010232, -36.66666666666174, -36.66666666666174, -20.000000000010232, 30.000000000001137, -6.666666666660603, 26.666666666670835, -6.666666666660603, -39.99999999999204, -23.333333333340533, 9.999999999990905, 26.666666666670835, 9.999999999990905, 26.666666666670835, 26.666666666670835, 26.666666666670835, 53.33333333334167, 3.3333333333587234, 36.66666666669016, 3.3333333333587234, 36.66666666669016, -13.333333333321207, 20.000000000010232, 20.000000000010232, 36.66666666669016, 6.666666666660603, 23.333333333340533, 23.333333333340533, 23.333333333340533, 6.666666666660603, 39.99999999999204, 23.333333333340533, 6.666666666660603, 16.66666666667993, 16.66666666667993, 50.00000000001137, 33.33333333333144, 66.66666666666288, 33.33333333333144, 50.00000000001137, 16.66666666667993, 0, 33.33333333333144, 50.00000000001137, 33.33333333333144, 33.33333333333144, 33.33333333333144, 0, 33.33333333333144, 16.66666666667993, 9.999999999990905, 9.999999999990905, 40.000000000020464, -83.33333333331439, 3.3333333333303017, -96.66666666666401, -13.333333333321207, -26.666666666642413, -43.333333333322344, -60.000000000002274, -106.66666666668334, 33.33333333333144, -39.99999999999204, 26.666666666670835, -39.99999999999204, -23.333333333340533, 9.999999999990905, -6.666666666660603, 16.66666666667993, 16.66666666667993, 0, 16.66666666667993, 16.66666666665151, 33.33333333330302, 16.66666666665151, -16.66666666665151, 16.66666666665151, 16.66666666665151, 0, 16.66666666665151, 0, 16.66666666665151, 0, 16.66666666665151, 0, 0, 0, 33.33333333330302, 16.66666666665151, 16.66666666665151, 0, 33.33333333330302, 0, 33.33333333330302, 16.66666666665151, 16.66666666665151, 33.33333333330302, 16.66666666665151, 16.66666666665151, 16.66666666665151, -16.66666666665151, 33.33333333330302, 0, 33.33333333330302, 0, 16.66666666665151, 0, 0, 16.66666666665151, 0, 16.66666666665151, 0, 16.66666666665151, 33.33333333330302, 33.33333333330302, -16.66666666665151]}`

### native-stream-300000

- decoded receiver motion: **passed**. `{"decodedFrames": 2883, "markerTransitions": 2121, "distinctFps": 7.028275471285468, "targetMotionStatus": "below target", "repeatFraction": 0.26430801248699276, "longestHoldMs": 1256.1123829344433, "originalWallDurationDistinctFps": 7.06304691617802, "originalWallDurationLongestHoldMs": 1249.928530126951, "mediaSeconds": 301.781, "denominatorCorrection": "Playback rate uses received media duration, including drained output; original wall-duration calculation retained."}`
- receiver media and timestamps: **passed**. `{"durationSeconds": 301.781, "bytes": 4664616, "averageMuxedKbps": 123.65565757950301, "encodedPackets": 2883, "encodedFps": 9.622158341596638, "presentationReorders": 0, "duplicatePresentationTimes": 0, "nonIncreasingDecodeTimes": 0}`
- receiver audible page tone: **passed**. `{"samples": 14485504, "seconds": 301.78133333333335, "rms": 0.04999229684472084, "peak": 0.24225226044654846, "tone440": 0.025571215897798538, "tone880": 0.0005869395099580288}`
- decoded audio and video sync: **passed**. `{"matchedFlashes": 114, "audioBursts": 362, "medianAudioMinusVideoMs": 6.666666666664156, "p95AbsoluteOffsetMs": 85.66666666667398, "firstThirdMedianMs": 10.00000000000334, "lastThirdMedianMs": -13.333333333321207, "offsetsMs": [10.000000000000231, 19.999999999999574, 16.666666666667496, 50.00000000000071, 33.33333333333499, 10.000000000001563, -63.33333333333613, -93.33333333333016, 26.66666666666373, -23.333333333333428, 53.33333333332746, -6.666666666660603, -63.333333333332575, 39.99999999999915, 26.666666666670835, 6.666666666667709, 23.333333333333428, -59.99999999999517, 46.666666666666856, -53.333333333334565, 0, 0, 23.333333333333428, 10.000000000005116, 33.33333333333144, 30.000000000001137, -53.33333333332746, 13.333333333335418, -33.33333333333144, 6.666666666674814, 46.666666666666856, 33.33333333334565, -23.333333333326323, -60.000000000002274, 56.66666666667197, -30.000000000001137, 30.000000000001137, -30.000000000001137, -70.00000000000739, -10.000000000005116, 16.66666666666572, -30.000000000001137, -49.99999999999716, -29.999999999986926, -6.666666666660603, 30.000000000001137, 56.66666666667197, 23.333333333340533, 16.66666666666572, 43.333333333336554, -60.000000000002274, 23.333333333340533, 46.66666666668107, -90.00000000000341, 10.000000000019327, 39.99999999999204, -66.66666666666288, 30.000000000001137, -66.66666666666288, 19.99999999998181, 36.66666666666174, -106.66666666665492, 33.33333333335986, -83.33333333334281, -69.99999999999318, 30.000000000001137, -96.66666666666401, 73.33333333332348, 13.333333333349628, 10.000000000019327, 30.000000000001137, -6.666666666660603, 43.333333333350765, 40.000000000020464, 40.000000000020464, 33.33333333333144, 33.33333333333144, 56.66666666667197, 6.666666666660603, 39.99999999999204, 0, -90.00000000000341, 60.000000000002274, 33.33333333333144, 2.8421709430404007e-11, 20.000000000010232, -26.666666666642413, -120.00000000000455, -66.66666666666288, -69.99999999999318, -30.000000000001137, -33.33333333333144, -9.999999999990905, -9.999999999990905, -63.333333333332575, 60.000000000002274, -79.99999999998408, 53.33333333334167, -26.666666666670835, -20.000000000010232, 16.66666666665151, 6.666666666717447, -46.666666666624224, -26.666666666642413, -69.99999999999318, -76.66666666665378, 33.33333333335986, -53.33333333334167, -23.333333333368955, 0, -36.66666666669016, -6.666666666660603, 6.666666666660603, -16.66666666665151]}`

## Scope and practical limits

- OBS browser-source plugin is absent; comparison uses real OBS capture of external Chromium. Browser input is driven by Playwright. This is observable functional equivalence, not native API parity.
- Audio on/off is verified from decoded PCM; source identities are verified from encoded color patches. Source switching in OBS uses browser foreground selection and explicitly stops inactive page audio.
- Streaming encoder settings also differ: native x264 veryfast/zerolatency, 4500 kbps target/maxrate with a 9000 kbps VBV buffer, AAC 128 kbps; OBS x264 veryfast/high, 4500 kbps CBR with B-frames, AAC 160 kbps. Configured bitrate is not observed throughput.
- Streaming uses a local FFmpeg RTMP receiver. Packet timestamps (strictly increasing DTS and unique PTS, permitting B-frame presentation reordering), distinct decoded markers, tones, periodic A/V flashes, and time-series resource/status evidence are saved. Loopback results do not characterize Internet upload performance.
- Periodic sync analysis resolves only offsets within half of the one-second pulse period. It includes browser AudioContext/rAF scheduling and is not a hardware latency measurement.
- Five-minute runs are bounded soak observations, not proof of indefinitely stable streaming.
- No hardware encoder, GPU sharing, OBS embedded-browser comparison, speech-model quality benchmark, external site benchmark, or complete Playwright compatibility claim is made.
- CEF native C++ checks and browser verification passed after qualifying the popup callback type and disabling first-run UI. Sandbox settings remain enabled.
- All smoke/setup failures and the interrupted pilot are retained outside final aggregates. Prior-chat temporary artifacts were absent in this fresh environment; their numeric results were not reused.

## Setup verification

Native C++ checks: logs/ctest.log. CEF browser integration: native-browser-report.json and logs/native-browser-verification.log. TypeScript build: logs/typescript-build.log. These passed; they are infrastructure verification rather than timed OBS comparisons.

## Saved evidence

Open report.html for charts and expandable checks. trials.json, trials.csv, aggregates.json, environment.json, per-trial settings/results/probes/markers/resources/frames/manifests, feature evidence, setup logs, package hashes, and setup-fixes.patch are saved here. Media paths are in the raw JSON; the evidence archive includes recordings. Reproduction commands are in ../README.md.
