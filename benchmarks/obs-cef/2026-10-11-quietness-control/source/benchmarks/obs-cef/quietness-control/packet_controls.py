"""Check encoded AAC packet continuity for BOTH products without modifying baseline."""
import gzip,json,pathlib,subprocess,sys
import numpy as np
root=pathlib.Path(__file__).resolve().parents[3];out=pathlib.Path(sys.argv[1]).resolve();baseline=root/'benchmarks/native-sync/2026-10-11-shared-clock';reports=[]
names=['720p60-record','720p60-stream','1080p30-record','1080p30-stream','4k30-record','4k30-stream','soak-1080p30-stream']
for product,base in [('SuiteCut',baseline),('OBS-CEF',out)]:
 for name in names:
  d=base/'runs'/name
  if not (d/'analysis.json').exists():continue
  a=json.loads((d/'analysis.json').read_text())
  if not a.get('harnessValid'):continue
  r=json.loads((d/'result.json').read_text());m=d/('recording.mkv' if r['mode']=='record' else 'receiver.flv');dest=out/'packet-controls'/product/name;dest.mkdir(parents=True,exist_ok=True)
  raw=subprocess.check_output(['ffprobe','-v','error','-select_streams','a:0','-show_packets','-show_entries','packet=pts_time,duration_time,size','-of','json',str(m)])
  with gzip.open(dest/'audio-packets.json.gz','wb') as f:f.write(raw)
  packets=json.loads(raw)['packets'];markers=np.load(d/'decoded-markers.npz');sel=(markers['markers']>=r['activeBaseline']['marker'])&(markers['markers']<=r['afterCapture']['marker']);ts=markers['timestamps'][sel];begin=float(ts[0]);end=float(ts[-1]+np.median(np.diff(ts)))
  previous=None;gaps=[];non_increasing=0;last=None
  for p in packets:
   if 'pts_time' not in p:continue
   start=float(p['pts_time']);duration=float(p.get('duration_time',1024/48000))
   if previous is not None and start>previous+.002 and start>begin and previous<end:
    gaps.append({'fromSeconds':max(previous,begin),'durationMs':max(0,(min(start,end)-max(previous,begin))*1000)})
   if last is not None and start<=last and begin<=start<=end:non_increasing+=1
   previous=start+duration;last=start
  report={'product':product,'case':name,'packetCount':len(packets),'activeBeginSeconds':begin,'activeEndSeconds':end,'activePacketTimelineGapTotalMs':sum(x['durationMs'] for x in gaps),'activePacketTimelineMaximumGapMs':max((x['durationMs'] for x in gaps),default=0),'nonIncreasingActivePacketPts':non_increasing,'activePacketTimelineGaps':gaps,'thresholdMs':2,'note':'Packet timeline holes are separate from decoded quiet PCM. Continuous AAC packets can contain quiet audio. Quantized mux timestamps up to 2 ms are tolerated.'}
  (dest/'summary.json').write_text(json.dumps(report,indent=2)+'\n');reports.append(report)
(out/'packet-controls.json').write_text(json.dumps(reports,indent=2)+'\n');print(json.dumps([{'product':r['product'],'case':r['case'],'gapMs':r['activePacketTimelineGapTotalMs']} for r in reports]))
