"""Compact live checkpoint; full media validation follows capture."""
import json,sys,statistics
from pathlib import Path
p=Path(sys.argv[1]);rows=[json.loads(s) for s in (p/'node-memory.jsonl').read_text().splitlines()];last=rows[-1]
native=[json.loads(s.split('SUITECUT_DIAGNOSTIC ',1)[1]) for s in (p/'logs/native.log').read_text().splitlines() if s.startswith('SUITECUT_DIAGNOSTIC ')]
proc=[json.loads(s) for s in (p/'processes.jsonl').read_text().splitlines()]
recent=[x for x in rows if x['atMs']>=last['atMs']-60000];first=recent[0];seconds=(last['atMs']-first['atMs'])/1000
n=native[-1];summary={'activeMinutes':round(last['atMs']/60000,2),'recentReceivedFps':None if not seconds else round((last['frames']-first['frames'])/seconds,2),'native':{key:n[key] for key in ['maxPaintGapMs','maxAudioGapMs','maxVideoWriteMs','maxAudioQueueMs','audioDropped','audioQueuePeak','videoReplaced']},'maxNodeFrameReceiveGapMs':last['maxFrameReceiveGapMs'],'maxSamplerIntervalSeconds':max(b['monotonic']-a['monotonic'] for a,b in zip(proc,proc[1:])),'nodeRecentMedianMiB':{key:round(statistics.median(r[key] for r in recent)/2**20,2) for key in ['rss','heapUsed','arrayBuffers']},'mediaValidated':(p/'media-validation.json').exists(),'completed':(p/'result.json').exists()}
print(json.dumps(summary))
