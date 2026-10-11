"""Read bounded tails for live progress; never decode media during capture."""
import json,pathlib,sys
out=pathlib.Path(sys.argv[1]);state=json.loads((out/'final-verification.json').read_text());stage=state['phases'][-1]
def tail(p,bytes=262144):
 with p.open('rb') as f:
  f.seek(0,2);start=max(0,f.tell()-bytes);f.seek(start);lines=f.read().decode().splitlines();return lines[1:] if start else lines
info={'stage':stage['name'],'stageStatus':stage['status']}
p=out/'runs/soak-1080p30-stream'
if (p/'status.jsonl').exists():
 d=json.loads(tail(p/'status.jsonl')[-1]);rows=[json.loads(l) for l in tail(p/'resources.jsonl')];latest=rows[-1];product=lambda r:[x for x in r['processes'] if x['role'] not in ['receiver','display','audio-server']]
 private=next((r for r in reversed(rows) if all('pssBytes' in x for x in product(r))),None)
 info.update(elapsedMinutes=round(d['atMs']/60000,2),rssMiB=round(sum(x['rssBytes'] for x in product(latest))/2**20,1),heapMiB=round(d['nodeMemory']['heapUsed']/2**20,1),overflowFrames=d['output']['droppedAudioSamples'],lateFrames=d['output']['lateAudioSamples'],queuedVideoMiB=round(d['output']['queuedVideoBytes']/2**20,1),maximumTrackLeadMs=round(d['output']['maximumTrackLeadMs'],2),bufferedDelayMs=round(d['output']['bufferedDelayMs'],2))
 if private:info.update(pssMiB=round(sum(x['pssBytes'] for x in product(private))/2**20,1),ussMiB=round(sum(x['ussBytes'] for x in product(private))/2**20,1))
if (out/'decode-verification.json').exists():info['decodedCases']=len(json.loads((out/'decode-verification.json').read_text())['cases'])
print(json.dumps(info))
