"""Analyze actual decoded PCM, including silence and known fixture tones."""
import json, sys
import numpy as np

samples = np.fromfile(sys.argv[1], dtype='<f4')
window = 48000
tones = {440: [], 880: []}
for start in range(0, len(samples)-window+1, window):
    block = samples[start:start+window]
    spectrum = np.abs(np.fft.rfft(block)) * 2 / window
    for frequency in tones:
        tones[frequency].append(float(max(spectrum[frequency-2:frequency+3])))
print(json.dumps({'samples': len(samples), 'seconds': len(samples)/48000,
                  'rms': float(np.sqrt(np.mean(samples**2))) if len(samples) else 0,
                  'peak': float(np.max(np.abs(samples))) if len(samples) else 0,
                  'tone440': max(tones[440], default=0), 'tone880': max(tones[880], default=0)}))
