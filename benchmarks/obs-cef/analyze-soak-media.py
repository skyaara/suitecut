"""Verify full-file video packet ordering and decoded beginning/middle/end motion."""
import json,subprocess,statistics
from pathlib import Path
out=Path('benchmarks/obs-cef/2026-10-09-optimization/soak');result=json.loads((out/'result.json').read_text());summary={}
assert result.get('completedAt') and result.get('activeMs',0)>=result['requestedMs']*.99
for label in ['recording','receiver']:
 media=result[label+'Path']
 data=subprocess.check_output(['ffprobe','-v','error','-select_streams','v:0','-show_packets','-show_entries','packet=pts_time,dts_time,duration_time','-of','json',media]);(out/(label+'-video-packets.json')).write_bytes(data);packets=json.loads(data)['packets'];pts=[float(p['pts_time']) for p in packets if 'pts_time' in p];dts=[float(p['dts_time']) for p in packets if 'dts_time' in p]
 assert len(pts)>1000 and len(dts)>1000
 assert len(pts)==len(set(pts)),label+' duplicate presentation timestamp'
 assert all(b>a for a,b in zip(dts,dts[1:])),label+' non-increasing decode timestamp'
 windows=[]
 for offset in [5,int(result['requestedMs']/2000),int(result['requestedMs']/1000)-20]:
  raw=subprocess.check_output(['ffmpeg','-v','error','-threads','2','-ss',str(offset),'-i',media,'-t','15','-an','-vf','crop=320:32:0:0,scale=20:1:flags=neighbor,format=gray','-vsync','0','-f','rawvideo','pipe:1']);ids=[]
  for n in range(0,len(raw)-19,20):ids.append(sum((1<<b) for b in range(20) if raw[n+b]>128))
  assert len(ids)>20;transitions=sum(i==0 or value!=ids[i-1] for i,value in enumerate(ids));assert transitions>10
  (out/f'{label}-{offset}'/'marker-ids.json').write_text(json.dumps(ids));windows.append({'offset':offset,'decodedFrames':len(ids),'markerTransitions':transitions,'firstMarker':ids[0],'lastMarker':ids[-1]})
 assert all(b['firstMarker']>a['lastMarker'] for a,b in zip(windows,windows[1:])),label+' motion failed to advance between windows'
 sorted_pts=sorted(pts);gaps=[b-a for a,b in zip(sorted_pts,sorted_pts[1:])];minute_packets=[{'startSecond':start,'videoPackets':sum(start<=t<start+60 for t in pts)} for start in range(0,int(max(pts)),60)]
 summary[label]={'maxPresentationGapSeconds':max(gaps),'minutePacketCounts':minute_packets,'status':'passed','videoPackets':len(pts),'packetIntervalFps':(len(pts)-1)/(max(pts)-min(pts)),'duplicatePresentationTimes':len(pts)-len(set(pts)),'nonIncreasingDecodeTimes':sum(b<=a for a,b in zip(dts,dts[1:])),'presentationReorders':sum(b<a for a,b in zip(pts,pts[1:])),'windows':windows}
(out/'packet-motion-validation.json').write_text(json.dumps(summary,indent=2));print(json.dumps(summary))
