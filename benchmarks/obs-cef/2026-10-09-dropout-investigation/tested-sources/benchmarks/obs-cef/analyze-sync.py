"""Nearest periodic flash/burst onset offsets, excluding the first two seconds.

Offsets are audio minus video, milliseconds. This periodic fixture can only
resolve offsets within half a period (500 ms); it does not measure network or
display latency. Software AudioContext and requestAnimationFrame introduce their
own scheduling error. Both streams are decoded relative to media start.
"""
import json, sys
import numpy as np

visual = np.fromfile(sys.argv[1], dtype='u1') > 180
pcm = np.fromfile(sys.argv[2], dtype='<f4')
step = 480
audio = np.array([np.sqrt(np.mean(pcm[i:i+step]**2)) for i in range(0, len(pcm)-step, step)]) > .015
v = (np.flatnonzero(visual[1:] & ~visual[:-1])+1) / 60
a = (np.flatnonzero(audio[1:] & ~audio[:-1])+1) * .01
v = v[v > 2]
a = a[a > 1.5]
offsets = np.array([(a[np.argmin(np.abs(a-t))]-t)*1000 for t in v]) if len(a) else np.array([])
print(json.dumps({'matchedFlashes': len(offsets), 'audioBursts': len(a),
                  'medianAudioMinusVideoMs': float(np.median(offsets)) if len(offsets) else None,
                  'p95AbsoluteOffsetMs': float(np.percentile(np.abs(offsets), 95)) if len(offsets) else None,
                  'firstThirdMedianMs': float(np.median(offsets[:max(1,len(offsets)//3)])) if len(offsets) else None,
                  'lastThirdMedianMs': float(np.median(offsets[-max(1,len(offsets)//3):])) if len(offsets) else None,
                  'offsetsMs': offsets.tolist()}))
