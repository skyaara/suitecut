"""Portable visual report and flat comparison; baseline directory is read-only."""
import csv,hashlib,html,json,pathlib,sys
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
root=pathlib.Path(__file__).resolve().parents[3];out=pathlib.Path(sys.argv[1]).resolve();baseline=root/'benchmarks/native-sync/2026-10-11-shared-clock'
names=['720p60-record','720p60-stream','1080p30-record','1080p30-stream','4k30-record','4k30-stream','soak-1080p30-stream']
rows=[];evidence=[]
for name in names:
 for product,base in [('SuiteCut',baseline),('OBS CEF',out)]:
  d=base/'runs'/name
  if not (d/'analysis.json').exists():continue
  a=json.loads((d/'analysis.json').read_text());r=json.loads((d/'result.json').read_text());valid=a['harnessValid'];w=a.get('audio',{}).get('activeWindow',{});resource=a.get('resources',{});motion=a.get('motion',{})
  row={'case':name,'product':product,'harnessValid':valid,'qualityPassed':a['qualityPassed'],'requestedSeconds':r['durationRequestedMs']/1000,'observedAudioSeconds':w.get('observedAudioSeconds'),'quietTotalMs':w.get('quietTotalMs'),'quietMaximumMs':w.get('quietMaximumMs'),'quietMsPerMinute':w.get('quietTotalMs',0)/(w.get('observedAudioSeconds',1)/60) if valid else None,'quietPercent':100*w.get('quietTotalMs',0)/1000/w.get('observedAudioSeconds',1) if valid else None,'missingCoverageMs':w.get('missingCoverageMs'),'cpuMeanCores':resource.get('cpuMeanCores'),'rssPeakMiB':resource.get('rssPeakMiB'),'pssPeakMiB':resource.get('pssPeakMiB'),'pssSlopeMiBPerMinute':resource.get('pssSlopeMiBPerMinuteAfterWarmup'),'distinctFps':motion.get('distinctFps'),'maxHoldMs':motion.get('maxHoldMs'),'contentClockMaxAbsMs':a.get('audio',{}).get('sync',{}).get('maximumAbsoluteMs'),'mediaBytes':a.get('mediaBytes'),'averageMuxBitrateKbps':a.get('mediaBytes',0)*8/a.get('mediaDurationSeconds',1)/1000 if valid else None,'browserVersion':r.get('browserVersion'),'issues':'; '.join(a.get('issues',[])),'error':a.get('error','')}
  rows.append(row);evidence.append({'case':name,'product':product,'analysisPath':str(d/'analysis.json'),'analysisSha256':hashlib.sha256((d/'analysis.json').read_bytes()).hexdigest(),'fixtureSha256':hashlib.sha256((d/'fixture.html').read_bytes()).hexdigest()})
 for name2 in [name]:
  es=[e for e in evidence if e['case']==name2]
  if len(es)==2:assert es[0]['fixtureSha256']==es[1]['fixtureSha256'],f'Fixture mismatch: {name}'
with (out/'comparison.csv').open('w') as f:
 w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
(out/'comparison.json').write_text(json.dumps({'rows':rows,'baseline':str(baseline),'evidence':evidence},indent=2)+'\n')
colors={'SuiteCut':'#44a5dd','OBS CEF':'#df7951'}
fig,axs=plt.subplots(1,2,figsize=(14,6),layout='constrained');y=np.arange(len(names))
for i,product in enumerate(colors):
 found={r['case']:r for r in rows if r['product']==product and r['harnessValid']};pos=y+(i-.5)*.34
 for ax,key,title in [(axs[0],'quietMsPerMinute','Unexpected quiet audio (ms per minute)'),(axs[1],'quietMaximumMs','Longest quiet interval (ms)')]:
  values=[found.get(n,{}).get(key,np.nan) for n in names];bars=ax.barh(pos,values,height=.3,label=product,color=colors[product]);ax.bar_label(bars,fmt='%.0f',padding=3,fontsize=8);ax.set_title(title);ax.grid(axis='x',alpha=.2);ax.set_axisbelow(True)
for ax in axs:ax.set_yticks(y,names);ax.invert_yaxis();ax.legend(loc='upper right');ax.margins(x=.15)
fig.savefig(out/'quiet-comparison.png',dpi=150);plt.close(fig)
fig,axs=plt.subplots(3,1,figsize=(12,9),sharex=True,layout='constrained')
for product,base in [('SuiteCut',baseline),('OBS CEF',out)]:
 d=base/'runs/soak-1080p30-stream';a=json.loads((d/'analysis.json').read_text()) if (d/'analysis.json').exists() else {}
 if not a.get('harnessValid'):continue
 series=json.loads((d/'resource-series.json').read_text())
 for ax,key,title in [(axs[0],'cpuCores','Process-tree CPU cores'),(axs[1],'rssMiB','Process-tree RSS (MiB)')]:
  s=series['series'];ax.plot([r['atSeconds']/60 for r in s],[r[key] for r in s],color=colors[product],alpha=.65,lw=.6,label=product);ax.set_ylabel(title)
 s=series['privateMemory'];axs[2].plot([r['atSeconds']/60 for r in s],[r['pssMiB'] for r in s],color=colors[product],label=product);axs[2].set_ylabel('Process-tree PSS (MiB)')
for ax in axs:ax.grid(alpha=.2);ax.legend()
axs[2].set_xlabel('Active stream minute');fig.savefig(out/'soak-resources.png',dpi=150);plt.close(fig)
# Plot how quiet audio accumulates during each sustained stream.
fig,axs=plt.subplots(2,1,figsize=(12,7),sharex=True,layout='constrained')
for product,base in [('SuiteCut',baseline),('OBS CEF',out)]:
 d=base/'runs/soak-1080p30-stream'
 if not (d/'analysis.json').exists():continue
 a=json.loads((d/'analysis.json').read_text());r=json.loads((d/'result.json').read_text())
 if not a.get('harnessValid'):continue
 decoded=np.load(d/'decoded-markers.npz');ts=decoded['timestamps'];m=decoded['markers'];sel=(m>=r['activeBaseline']['marker'])&(m<=r['afterCapture']['marker']);rms=np.load(d/'audio-rms.npz')['rms'];start=float(a['audio']['stream'].get('start_time',0));step=.005
 begin=max(0,int(np.ceil((ts[sel][0]-start)/step)));end=min(len(rms),int(np.floor((ts[sel][-1]+float(np.median(np.diff(ts[sel])))-start)/step)));quiet=rms[begin:end]<.001
 assert int(quiet.sum()*5)==a['audio']['activeWindow']['quietTotalMs']
 t=np.arange(len(quiet))*step/60;axs[0].plot(t,np.cumsum(quiet)*5/1000,color=colors[product],label=product)
 buckets=[int(quiet[i:i+12000].sum()*5) for i in range(0,len(quiet),12000)];axs[1].plot(np.arange(len(buckets))+.5,buckets,color=colors[product],label=product,marker='o',ms=3)
axs[0].set_ylabel('Cumulative quiet audio (seconds)');axs[1].set_ylabel('Quiet audio per minute (ms)');axs[1].set_xlabel('Active stream minute')
for ax in axs:ax.legend();ax.grid(alpha=.2)
fig.savefig(out/'soak-quiet-timeline.png',dpi=150);plt.close(fig)

fmt=lambda n,dec=2:'—' if n is None else f'{n:,.{dec}f}'
md=['# OBS embedded CEF: unexpected quiet-audio control','', 'Fresh OBS Browser Source captures are compared with the sealed SuiteCut shared-clock captures. Recording and streaming are separate. This report measures decoded quiet audio during a source that intentionally plays continuously; “quiet” does not by itself identify where samples were lost.', '', '| Case | Product | Quiet total ms | Longest ms | Quiet ms/min | Quiet % | CPU cores | Peak RSS MiB | Distinct FPS |', '|---|---|---:|---:|---:|---:|---:|---:|---:|']
for r in rows:md.append('| '+ ' | '.join([r['case'],r['product']]+[fmt(r[k]) for k in ['quietTotalMs','quietMaximumMs','quietMsPerMinute','quietPercent','cpuMeanCores','rssPeakMiB','distinctFps']])+' |')
long={r['product']:r for r in rows if r['case']=='soak-1080p30-stream'}
conclusion='The 30-minute 1080p30 stream produced '+fmt(long['OBS CEF']['quietTotalMs']/1000,3)+' s quiet audio in OBS CEF (longest '+fmt(long['OBS CEF']['quietMaximumMs'],0)+' ms), versus '+fmt(long['SuiteCut']['quietTotalMs']/1000,3)+' s in SuiteCut (longest '+fmt(long['SuiteCut']['quietMaximumMs'],0)+' ms). These individual sustained runs have nearly identical quiet totals, despite quieter short OBS streaming controls. Both outputs contain quiet audio inside continuous AAC packet timelines; this alone does not identify the responsible stage or prove source-sample loss.'
md[3:3]=['',conclusion,'']
md += ['', '![Quiet comparison](quiet-comparison.png)', '', '![30-minute quiet-audio timeline](soak-quiet-timeline.png)', '', '![30-minute resource profiles](soak-resources.png)', '', '## Method and limits', '', '- Actual OBS embedded CEF Browser Source, 1 source, reroute_audio=true, volume 1, unmuted, no microphone or desktop capture. All inputs from the copied profile are removed before creating the browser source.', '- The fixture HTML hashes match SuiteCut for every paired case. Deterministic AudioWorklet continuous noise, CSS/canvas movement and browser scrolling; click, text-entry and selection are exercised. No intentional interior silence.', '- Nominal settings are validated, but achieved distinct FPS and PTS/hold failures are reported separately. The OBS 4K software-rendered cases may fail to achieve the requested 30 FPS; treat their audio results in that overload context.', '- Each short case requests 120 seconds; the 1080p30 streaming soak requests 1,800 seconds. OBS runs are fresh single trials and SuiteCut trials were captured earlier; this is not a randomized repeated-trial estimate.', '- Same shared decoder: AAC to 48 kHz mono float PCM; 5 ms RMS bins below 0.001 count as quiet. Only the encoded workload marker interval is counted, excluding warmup and shutdown. Missing audio coverage is reported separately. Quietness is not a verified count of dropped source samples.', '- Both use H.264 and AAC 48 kHz stereo 128 kbps. Recording CRF 18 / MKV. Streaming local RTMP / FLV copy receiver, bitrate 3500 / 4500 / 12000 kbps for 720p / 1080p / 4K. x264 veryfast, threads=1 below 4K; ultrafast, threads=3 at 4K; zerolatency, no B frames, GOP 2 seconds. OBS and SuiteCut use different encoder integrations; equal knobs do not guarantee identical bitstreams.', '- OBS 32.2.0 embeds Chromium 127.0.6533.120; SuiteCut embeds Chromium 152.0.7977.83. Browser-version differences and mixer/renderer differences prevent attributing an observed difference to CEF or scheduling alone.', '- CPU affinity 4 cores with 4-core host quota. Requested 8 GiB is a sampled sum-RSS termination guard, not an enforced 8 GiB kernel cap; host cap 16 GiB. No exposed hardware GPU: OBS OpenGL uses llvmpipe and CEF uses software rendering. These are software-only host results.', '- During the long stream, three already-completed raw OBS streams were relocated to the separate /tmp filesystem to reserve overlay storage, with original paths preserved by symlinks. storage-relocation.json records exact host monotonic intervals and byte counts; the complete raw media is included in the archive. This brief host housekeeping remains inside the measured long-run interval and is not selectively excluded.', '- Resource sampling every 500 ms; soak PSS/USS every 10 seconds. Product accounting includes OBS/CEF or SuiteCut/CEF plus the controller; local receiver, Xvfb and Pulse are separately sampled and excluded. Clean owned-file page cache is advised away between captures only. No audio taps or sanitizers during primary OBS captures.', '- The long-run AAC packet timelines contain no gaps above the 2 ms rounding allowance in either product. Encoded audio continuity therefore does not imply continuous audible sample content.', '- A post-capture H264 filler-NAL diagnostic explains the larger OBS stream: original average mux bitrate 4638 kbps, about 533 kbps after removing filler/remuxing, versus SuiteCut 458 kbps with no size change from that filter. The full original streams are retained. Matching nominal stream bitrate does not mean identical CBR padding policy.', '- Encoded AAC packet timestamps are independently checked for both products in packet-controls, allowing 2 ms mux rounding. Packet holes and decoded quiet audio are distinct measurements.', '- Generated audio-content correlation versus the encoded AudioContext marker is also saved. Its 10 ms marker plus nearest-frame quantization is an investigation aid, not a certification of lip sync.', '- Setup attempts that failed effective-video/readiness assertions are retained under startup-attempts and excluded from benchmark rows. Any failed primary run appears in the CSV with harnessValid=false; no silent fallback to lower resolution.', '', '## Evidence', '', '- comparison.csv / comparison.json: all paired metrics and baseline analysis/fixture hashes.', '- runs/<case>: MKV or FLV media, OBS log, copied runtime profile, result JSON, process samples, status history, decoded marker arrays, RMS arrays, ffprobe metadata, quiet-interval CSV and analysis JSON.', '- source/: exact harness/fixture/analyzer sources used; provenance.json: binary hashes and platform.', '- packet-controls/: gzip packet metadata and active-timeline-gap summaries for both products.', '- The SuiteCut baseline is referenced without modification; baseline analysis/result/fixture copies are included. Full original media and traces remain in the separately sealed archive suitecut-shared-clock-2026-10-11.tar.gz (SHA256 c5602868e63866a6e2adb61c8c31a5cb98f5b2398604efc4aa0588d5e24d4871).', '']
(out/'report.md').write_text('\n'.join(md))
headers=['Case','Product','Quiet total ms','Longest ms','Quiet ms/min','Quiet %','CPU cores','Peak RSS MiB','Distinct FPS','Analysis']
table='<table><thead><tr>'+''.join('<th>'+html.escape(h)+'</th>' for h in headers)+'</tr></thead><tbody>'
for r in rows:
 link=('baseline/' if r['product']=='SuiteCut' else 'runs/')+r['case']+'/analysis.json'
 vals=[r['case'],r['product']]+[fmt(r[k]) for k in ['quietTotalMs','quietMaximumMs','quietMsPerMinute','quietPercent','cpuMeanCores','rssPeakMiB','distinctFps']]
 table+='<tr data-kind="'+('soak' if r['case'].startswith('soak') else 'short')+'" data-product="'+html.escape(r['product'])+'">'+''.join('<td>'+html.escape(str(v))+'</td>' for v in vals)+'<td><a href="'+link+'">Details</a></td></tr>'
table+='</tbody></table>'

detail='<table><thead><tr><th>Case</th><th>Product</th><th>Longest video hold ms</th><th>Content-clock max offset ms</th><th>Peak PSS MiB</th><th>PSS slope MiB/min</th></tr></thead><tbody>'
for r in rows:
 vals=[r['case'],r['product']]+[fmt(r[k]) for k in ['maxHoldMs','contentClockMaxAbsMs','pssPeakMiB','pssSlopeMiBPerMinute']]
 detail+='<tr>'+''.join('<td>'+html.escape(str(v))+'</td>' for v in vals)+'</tr>'
detail+='</tbody></table>'

method=''.join('<li>'+html.escape(line[2:])+'</li>' for line in md[md.index('## Method and limits'):] if line.startswith('- '))
body='<h1>OBS embedded CEF: quiet-audio comparison</h1><p>Fresh OBS Browser Source captures versus the sealed SuiteCut shared-clock captures. Recording and streaming are separate. Lower quiet totals and shorter intervals are better. These single trials measure output quality; they do not identify the cause of every gap.</p><div style="background:#e6edf4;padding:18px;border-radius:10px"><strong>30-minute result</strong><p>'+html.escape(conclusion)+'</p></div><p><a href="comparison.csv">Download comparison CSV</a> · <a href="comparison.json">Comparison JSON</a> · <a href="report.md">Full method</a> · <a href="provenance.json">Versions and provenance</a></p><p><button onclick="filterRows(\'all\')">All tests</button> <button onclick="filterRows(\'short\')">Short captures</button> <button onclick="filterRows(\'soak\')">30-minute stream</button></p><div style="overflow:auto">'+table+'</div><h2>Quietness across resolutions</h2><img src="quiet-comparison.png" alt="Quiet audio per minute and longest interval for both products"><h2>Sustained streaming quietness</h2><img src="soak-quiet-timeline.png" alt="Cumulative quiet audio and quiet duration by minute"><h2>Sustained resource profiles</h2><img src="soak-resources.png" alt="CPU, RSS and PSS profiles"><h2>Video, content-clock and memory details</h2><p>Content-clock offset uses generated audio correlation and a quantized encoded marker; it is not a certification of lip sync. Memory slopes cover the sustained stream after its first two minutes.</p><div style="overflow:auto">'+detail+'</div><h2>Method and evidence</h2><ul>'+method+'</ul><script>function filterRows(kind){document.querySelectorAll(\'tbody tr\').forEach(row=>{row.hidden=kind!==\'all\'&&row.dataset.kind!==kind})}</script>'
(out/'report.html').write_text('<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>OBS CEF quiet-audio comparison</title><style>body{font:16px/1.6 system-ui;background:#f5f7fa;color:#182434;max-width:1400px;margin:40px auto;padding:0 24px}table{border-collapse:collapse;width:100%;font-size:14px}td,th{padding:10px;border-bottom:1px solid #cbd5df;text-align:left}thead{background:#dde7ee}img{max-width:100%;background:white;border-radius:12px}pre{white-space:pre-wrap}h1{font-size:32px}a{color:#176aa0}button{border:1px solid #aac0cf;background:white;border-radius:6px;padding:8px 14px;cursor:pointer}tbody tr[data-product="OBS CEF"]{background:#fff2e8}</style></head><body>'+body+'</body></html>')
print(json.dumps({'rows':len(rows),'report':str(out/'report.html')}))
