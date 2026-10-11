"""Generate publication artifacts from saved, independently decoded trials."""
import csv,datetime,html,json,pathlib,re,statistics,sys
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

ROOT=pathlib.Path(__file__).resolve().parents[2]
OUT=pathlib.Path(sys.argv[1]) if len(sys.argv)>1 else ROOT/'benchmarks/publication/2026-10-10'
LABELS={'ts-cef':'SuiteCut historical TS / CEF','ts-external':'SuiteCut historical TS / Chromium','obs-cef':'OBS / embedded CEF','obs-external':'OBS / Chromium screen capture','cpp-cef':'SuiteCut C++ / CEF','cpp-external':'SuiteCut C++ / external browser'}
ORDER=list(LABELS)[:5]

def fmt(x,digits=2):
 return '—' if x is None else f'{x:.{digits}f}'

def rng(values,digits=2):
 values=[x for x in values if x is not None]
 return '—' if not values else f'{fmt(statistics.median(values),digits)} [{fmt(min(values),digits)}–{fmt(max(values),digits)}]'

def get(row,key):
 obj=row
 for part in key.split('.'):
  if not isinstance(obj,dict):return None
  obj=obj.get(part)
 return obj

def table(headers,rows):
 return '\n'.join(['| '+' | '.join(headers)+' |','| '+' | '.join(['---']*len(headers))+' |']+['| '+' | '.join(map(str,row))+' |' for row in rows])

def enrich(runs):
 for r in runs:
  directory=pathlib.Path(r['directory']);a=r.get('analysis',{})
  if not a.get('mediaDecoded'):continue
  raw=[json.loads(line) for line in (directory/'resources.jsonl').read_text().splitlines()]
  start=r['result']['activeStartMonotonic'];end=r['result']['activeEndMonotonic'];active=[x for x in raw if start<=x['monotonic']<=end]
  a['outputAverageKbps']=a['mediaBytes']*8/a['mediaDurationSeconds']/1000
  rms_path=directory/'audio-rms.npz'
  if rms_path.exists():
   rms=np.load(rms_path)['rms'];quiet=rms<.001;markers_data=np.load(directory/'decoded-markers.npz');markers=markers_data['markers'];timestamps=markers_data['timestamps'];mask=(markers>=r['result']['activeBaseline']['marker'])&(markers<=r['result']['afterCapture']['marker'])
   if mask.any():
    audio_start=float(get(r,'analysis.audio.stream.start_time') or 0);begin=max(0,int(np.ceil((timestamps[mask][0]-audio_start)/.005)));stop=min(len(quiet),int(np.floor((timestamps[mask][-1]+np.median(np.diff(timestamps))-audio_start)/.005)));selected=quiet[begin:stop];runs_quiet=[];first=None
    for j,q in enumerate(np.r_[selected,False]):
     if q and first is None:first=j
     if not q and first is not None:runs_quiet.append((j-first)*5);first=None
    a['audio']['activeWindow']={'startVideoPtsSeconds':float(timestamps[mask][0]),'endVideoPtsSeconds':float(timestamps[mask][-1]),'observedAudioSeconds':len(selected)*.005,'quietTotalMs':int(selected.sum()*5),'quietMaximumMs':max(runs_quiet,default=0),'quietPercent':float(selected.mean()*100) if len(selected) else None,'definition':'All quiet bins within output video marker active window; includes loss continuing to the end of that window'}
   a['audio']['totalQuietMs']=int(quiet.sum()*5)
  a['observer']={'cpuMeanCores':a['resources'].get('samplerCpuMeanCores'),'p95CollectionMs':a['resources'].get('samplerDurationP95Ms')}
  a['infrastructureCpuMeanByRole']={role:float(np.mean([sum(p['cpuCores'] or 0 for p in x['processes'] if p['role']==role) for x in active])) for role in ['receiver','display','audio-server']}
  host=[x for x in active if x.get('host',{}).get('cpu.stat')]
  def stats(x):return {k:int(v) for k,v in (line.split() for line in x.splitlines())}
  if len(host)>1:
   before=stats(host[0]['host']['cpu.stat']);after=stats(host[-1]['host']['cpu.stat']);delta={k:after[k]-before[k] for k in before}
   a['hostCpu']={'observedSeconds':host[-1]['monotonic']-host[0]['monotonic'],'cpuStatDelta':delta,'throttledPeriodsFraction':delta['nr_throttled']/delta['nr_periods'] if delta['nr_periods'] else None,'scope':'Entire host cgroup, including infrastructure and observer'}
  series=json.loads((directory/'resource-series.json').read_text())
  if r['profile']=='memory':
   common=[x for x in series['series'] if x['atSeconds']<=600];fit=[x for x in common if x['atSeconds']>=120]
   a['commonFirst10Minutes']={'rssPeakMiB':max(x['rssMiB'] for x in common),'cpuMeanCores':float(np.mean([x['cpuCores'] for x in common])),'rssSlopeMiBPerMinuteAfter2m':float(np.polyfit([x['atSeconds']/60 for x in fit],[x['rssMiB'] for x in fit],1)[0]),'rssMedianFirstMinuteMiB':float(np.median([x['rssMiB'] for x in common if x['atSeconds']<60])),'rssMedianLastMinuteMiB':float(np.median([x['rssMiB'] for x in common if x['atSeconds']>=540]))}
   audio_path=directory/'audio-rms.npz'
   if audio_path.exists():
    quiet=np.load(audio_path)['rms'][:120000]<.001;a['commonFirst10Minutes']['first600OutputSecondsQuietAudioSeconds']=float(quiet.sum()*.005);a['commonFirst10Minutes']['first600OutputSecondsQuietAudioPercent']=float(quiet.mean()*100)
   private=series['privateMemory'];fit_private=[x for x in private if x['atSeconds']>=120];a['privateMemoryTotals']={'ussPeakMiB':max((x['ussMiB'] for x in private),default=None),'ussSlopeMiBPerMinuteAfter2m':float(np.polyfit([x['atSeconds']/60 for x in fit_private],[x['ussMiB'] for x in fit_private],1)[0]) if len(fit_private)>=5 else None};a['privateMemoryCoverage']={'completeSamples':len(private),'expectedApproximateSamples':r['durationMs']/10000,'scope':'All product processes must have readable PSS/USS'}
  phases=r['result']['phases'];by_phase={}
  for j,phase in enumerate(phases):
   selected=[x for x in series['series'] if phase['monotonic']-start<=x['atSeconds']<(phases[j+1]['monotonic']-start if j+1<len(phases) else end-start)]
   by_phase.setdefault(phase['name'],[]).extend(selected)
  a['resourcesByPhase']={name:{'samples':len(rows),'cpuMeanCores':float(np.mean([x['cpuCores'] for x in rows])),'rssPeakMiB':max(x['rssMiB'] for x in rows)} for name,rows in by_phase.items() if rows}
 return runs

def html_document(markdown_text):
 def inline(line):
  line=html.escape(line)
  line=re.sub(r'`([^`]+)`',r'<code>\1</code>',line)
  line=re.sub(r'\*\*([^*]+)\*\*',r'<strong>\1</strong>',line)
  line=re.sub(r'!\[([^]]*)\]\(([^)]+)\)',r'<img alt="\1" src="\2">',line)
  return line
 lines=markdown_text.splitlines();body=[];i=0
 while i<len(lines):
  line=lines[i]
  if not line:i+=1;continue
  if line.startswith('```'):
   chunk=[];i+=1
   while i<len(lines) and not lines[i].startswith('```'):chunk.append(lines[i]);i+=1
   body.append('<pre><code>'+html.escape('\n'.join(chunk))+'</code></pre>');i+=1;continue
  if line.startswith('|'):
   header=[inline(x.strip()) for x in line.strip('|').split('|')];i+=2;rows=[]
   while i<len(lines) and lines[i].startswith('|'):
    rows.append('<tr>'+''.join('<td>'+inline(x.strip())+'</td>' for x in lines[i].strip('|').split('|'))+'</tr>');i+=1
   body.append('<div class="table"><table><thead><tr>'+''.join('<th>'+x+'</th>' for x in header)+'</tr></thead><tbody>'+''.join(rows)+'</tbody></table></div>');continue
  if line.startswith('- '):
   rows=[]
   while i<len(lines) and lines[i].startswith('- '):rows.append('<li>'+inline(lines[i][2:])+'</li>');i+=1
   body.append('<ul>'+''.join(rows)+'</ul>');continue
  if line.startswith('#'):
   n=len(line)-len(line.lstrip('#'));body.append(f'<h{n}>'+inline(line[n:].strip())+f'</h{n}>');i+=1;continue
  chunk=[]
  while i<len(lines) and lines[i]:chunk.append(lines[i]);i+=1
  body.append('<p>'+inline(' '.join(chunk))+'</p>')
 return '\n'.join(body)

def render():
 runs=enrich(json.loads((OUT/'runs.json').read_text()));(OUT/'enriched-runs.json').write_text(json.dumps(runs,indent=2)+'\n');env=json.loads((OUT/'environment.json').read_text());progress=json.loads((OUT/'progress.json').read_text())
 perf=[r for r in runs if r['profile']=='performance'];soaks=[r for r in runs if r['profile']=='memory']
 summary=[]
 for mode in ['record','stream']:
  for backend in ORDER:
   rows=[r for r in perf if r['backend']==backend and r['mode']==mode]
   decoded=[r for r in rows if get(r,'analysis.mediaDecoded')]
   item={'backend':backend,'mode':mode,'trials':len(rows),'decodedTrials':len(decoded),'qualityPasses':sum(bool(r.get('mediaValidated')) for r in rows),'processSuccesses':sum(bool(r.get('processSucceeded')) for r in rows)}
   for key in ['resources.cpuMeanCores','resources.cpuP95Cores','resources.rssPeakMiB','resources.rssMeanMiB','motion.distinctFps','motion.maxHoldMs','motion.maxPtsGapMs','motion.repeatedFraction','audio.silence.interiorTotalMs','audio.silence.interiorMaximumMs','audio.silence.leadingMs','audio.sync.medianMs','audio.sync.maximumAbsoluteMs','audio.activeWindow.quietTotalMs','audio.activeWindow.quietMaximumMs','audio.activeWindow.quietPercent','audio.silence.trailingMs','audio.totalQuietMs']:
    values=[get(r,'analysis.'+key) for r in decoded];values=[x for x in values if x is not None]
    item[key]={'median':statistics.median(values),'min':min(values),'max':max(values),'values':values} if values else None
   item['validationIssues']=[{'id':r['id'],'issues':get(r,'analysis.validationIssues'),'error':get(r,'analysis.error'),'cleanupFailure':get(r,'result.cleanupFailure'),'returncode':r['returncode']} for r in rows if not r.get('mediaValidated') or not r.get('processSucceeded')]
   summary.append(item)
 (OUT/'summary.json').write_text(json.dumps({'environment':env,'progress':progress,'comparisons':summary,'memoryRuns':soaks},indent=2)+'\n')
 fields=['id','backend','mode','profile','durationMs','returncode','workloadCompleted','mediaValidated','processSucceeded','cpuMeanCores','cpuP95Cores','rssPeakMiB','distinctFps','maxHoldMs','interiorAudioSilenceMs','longestInteriorAudioSilenceMs','leadingSilenceMs','outputAverageKbps','mediaDurationSeconds','repeatedFrameFraction','maxPtsGapMs','activeAudioQuietMs','activeAudioLongestQuietMs','trailingSilenceMs','totalQuietMs']
 with (OUT/'trials.csv').open('w',newline='') as f:
  w=csv.DictWriter(f,fieldnames=fields);w.writeheader()
  for r in runs:
   row={k:r.get(k) for k in fields[:9]}
   for outkey,path in [('cpuMeanCores','resources.cpuMeanCores'),('cpuP95Cores','resources.cpuP95Cores'),('rssPeakMiB','resources.rssPeakMiB'),('distinctFps','motion.distinctFps'),('maxHoldMs','motion.maxHoldMs'),('interiorAudioSilenceMs','audio.silence.interiorTotalMs'),('longestInteriorAudioSilenceMs','audio.silence.interiorMaximumMs'),('leadingSilenceMs','audio.silence.leadingMs'),('outputAverageKbps','outputAverageKbps'),('mediaDurationSeconds','mediaDurationSeconds'),('repeatedFrameFraction','motion.repeatedFraction'),('maxPtsGapMs','motion.maxPtsGapMs'),('activeAudioQuietMs','audio.activeWindow.quietTotalMs'),('activeAudioLongestQuietMs','audio.activeWindow.quietMaximumMs'),('trailingSilenceMs','audio.silence.trailingMs'),('totalQuietMs','audio.totalQuietMs')]:row[outkey]=get(r,'analysis.'+path)
   w.writerow(row)
 plots=OUT/'figures';plots.mkdir(exist_ok=True)
 plt.rcParams.update({'font.size':10,'figure.dpi':130,'savefig.dpi':180,'axes.spines.top':False,'axes.spines.right':False})
 colors=['#3956a8','#8065b5','#d78c24','#9b5f14','#148775']
 for mode in ['record','stream']:
  fig,axes=plt.subplots(1,3,figsize=(14,4.4))
  for ax,key,title in zip(axes,['resources.cpuMeanCores','resources.rssPeakMiB','motion.distinctFps'],['CPU cores (100% = 1 core)','Peak summed RSS (MiB)','Distinct visible marker frames / second']):
   for i,backend in enumerate(ORDER):
    vals=[get(r,'analysis.'+key) for r in perf if r['backend']==backend and r['mode']==mode and get(r,'analysis.mediaDecoded')];vals=[v for v in vals if v is not None]
    if vals:
     ax.scatter(vals,np.full(len(vals),i)+np.linspace(-.12,.12,len(vals)),color=colors[i],s=25,alpha=.8)
     ax.plot([min(vals),max(vals)],[i,i],color=colors[i],alpha=.4)
     ax.scatter([statistics.median(vals)],[i],color='black',marker='|',s=170)
   ax.set_yticks(range(5),[LABELS[b].replace('SuiteCut historical TS','TS').replace('SuiteCut C++','C++') for b in ORDER] if ax is axes[0] else ['']*5);ax.invert_yaxis();ax.set_xlabel(title);ax.grid(axis='x',alpha=.2)
   if 'cpu' in key:ax.axvline(4,color='#777',ls='--',lw=1,label='4-core budget');ax.set_xlim(left=0)
   if 'distinct' in key:ax.axvline(30,color='#777',ls='--',lw=1);ax.set_xlim(0,32)
  fig.suptitle(f'1080p / 30 FPS — {mode} — all decoded trials, median in black');fig.tight_layout()
  for ext in ['png','svg']:fig.savefig(plots/f'{mode}-comparison.{ext}',bbox_inches='tight')
  plt.close(fig)
 if soaks:
  fig,axes=plt.subplots(3,1,figsize=(11,9),sharex=True)
  for r in soaks:
   p=pathlib.Path(r['directory'])/'resource-series.json'
   if not p.exists():continue
   data=json.loads(p.read_text());series=data['series'];i=ORDER.index(r['backend']);label=LABELS[r['backend']]
   axes[0].plot([x['atSeconds']/60 for x in series],[x['rssMiB'] for x in series],color=colors[i],label=label,lw=.9)
   axes[1].plot([x['atSeconds']/60 for x in series],[x['cpuCores'] for x in series],color=colors[i],alpha=.7,lw=.6)
   private=data['privateMemory']
   if private:axes[2].plot([x['atSeconds']/60 for x in private],[x['pssMiB'] for x in private],color=colors[i],lw=1)
  axes[0].set_ylabel('Summed RSS (MiB)');axes[0].legend(fontsize=8,ncol=2)
  axes[1].set_ylabel('CPU cores');axes[2].set_ylabel('Complete PSS (MiB)');axes[2].set_xlabel('Active streaming minutes')
  for ax in axes:ax.grid(alpha=.2)
  axes[0].set_title('Memory profiles: 10 minutes each; native C++ extended to 30 minutes')
  if not any(get(r,'analysis.resources.pssCompleteSamples') for r in soaks):axes[2].text(.5,.5,'Complete PSS unavailable: cloud /proc permissions',transform=axes[2].transAxes,ha='center')
  fig.tight_layout()
  for ext in ['png','svg']:fig.savefig(plots/f'memory-profiles.{ext}',bbox_inches='tight')
  plt.close(fig)
 if soaks:
  fig,ax=plt.subplots(figsize=(11,4.8))
  for r in soaks:
   path=pathlib.Path(r['directory'])/'audio-rms.npz'
   if not path.exists():continue
   data=np.load(path);quiet=data['rms']<.001;size=2000;n=len(quiet)//size
   values=quiet[:n*size].reshape(n,size).mean(axis=1)*100
   ax.plot((np.arange(n)+.5)*10/60,values,label=LABELS[r['backend']],color=colors[ORDER.index(r['backend'])],lw=1.4)
  ax.set(xlabel='Output minutes',ylabel='Quiet audio bins (%) per 10-second window',ylim=(-1,101),title='Sustained streaming: decoded audio continuity (including output edges)');ax.grid(alpha=.2);ax.legend(fontsize=8,ncol=2);fig.tight_layout()
  for ext in ['png','svg']:fig.savefig(plots/f'audio-continuity.{ext}',bbox_inches='tight')
  plt.close(fig)
 text=['# SuiteCut and OBS: 1080p / 30 FPS browser capture', '',f"Generated {datetime.datetime.now(datetime.timezone(datetime.timedelta(hours=5,minutes=30))).isoformat()} (IST). Series status: **{progress['status']}**, {len(runs)}/{progress['plannedRuns']} runs saved.", '',
 'This report compares historical SuiteCut TypeScript capture, the native C++ implementation, and OBS on a deterministic local web application. Recording and streaming are separate workloads. Results describe this software-only cloud host and these versions; they are not a universal ranking or proof of memory safety.', '',
 '**The native external-browser capture adapter is unavailable.** The new `connectBrowser` SDK supports browser automation, but does not implement native recording/streaming of an external browser. That requested row is deliberately shown as unavailable.', '',
 '## Measurement protocol', '',
 '- Fresh owned browser/display/audio/OBS processes per run, fixed 1920×1080 page viewport, 30 FPS output, four CPU affinity slots (0–3), and an actual four-core cgroup quota.',
 '- Requested RAM budget: 8 GiB. This host has a 16 GiB kernel limit; an 8 GiB sampled sum-of-RSS guard terminates the workload if exceeded. It is **not an enforced 8 GiB kernel limit** and can miss subsecond peaks.',
 '- Five 30-second trials per available backend per mode, serially rotated/reversed in order. Each trial runs 10 seconds of low-motion marker-only content, 10 seconds of animation and form input, and 10 seconds of actual wheel scrolling.',
 '- A fixed 30 Hz wall-clock marker is decoded from every output video frame. The marker remains active in the low-motion phase. Distinct marker FPS and longest held image measure delivered motion; a 30 FPS container alone does not establish 30 FPS visual updates.',
 '- Recorded action markers verify click, text entry, selection, and scroll states in order (0→1→3→7→15). Historical CEF text fill dispatches DOM input; Playwright paths use its own input implementation. These are representative operations, not full Playwright conformance.',
 '- H.264 software encoding, `veryfast`, zero latency, one video encoder thread, no B frames, GOP 60. Recordings use CRF 18; streaming targets 4500 kbit/s video and AAC 48 kHz stereo at 128 kbit/s. Encoder implementations/library generations can still differ.',
 '- Recording source is silent. SuiteCut records video only; the tested OBS recording profile imposes an additional silent AAC track. This is a disclosed unmatched audio-encoding cost; streaming is the matched audio workload.',
 '- Continuous deterministic AudioWorklet audio during streaming; no intentional interior silence. Streaming goes to a local RTMP receiver, saved without re-encoding. This tests capture/encode/mux, not remote Internet or service performance. The receiver accepts one connection; it does not model production RTMP reconnection. The historical startup retry failure is retained under this condition.',
 '- Audio silence is measured from decoded samples in 5 ms RMS bins, threshold 0.001. Leading/trailing silence is reported separately from interior silence. It is signal-specific evidence, not proof that every nonzero sample is correct.',
 '- Audio/video offsets use deterministic waveform matching against the video’s 100 Hz AudioContext marker; only correlation ≥0.85 anchors count. Quantization includes 10 ms marker steps and video-frame timing. These are synchronization observations, not end-to-end latency.',
 '- Browser/encoder/controller process resources sampled every 500 ms; descendant inventory every second. PSS/USS sampled every 10 seconds for memory runs when allowed by `/proc`. Receiver, Xvfb and Pulse are saved separately and excluded from product totals. Common Node harness imports are included for every row.',
 '- Sum of RSS double-counts shared pages. Complete PSS requires every product process to be readable; partial readings are not presented as complete PSS. CPU cores 1.00 means 100% of one CPU core.',
 '- No forced GC, sanitizer, tracing preload, other benchmark, build or video analysis during the active published captures. Media analysis happens after each runtime has shut down.',
 '- Memory soaks use the same repeating 30-second workload for 10 minutes per backend; C++ CEF additionally runs to 30 minutes. Memory slope uses samples after the first two minutes. Compare common first-10-minute windows before making duration-sensitive claims.', '',
 '## Versions and comparison boundaries', '',
 table(['Component','Version / boundary'],[
 ['SuiteCut C++','2.0.0 unpublished development checkout; pinned source/SDK/binary snapshots'],
 ['Historical SuiteCut TS',env['historicalRevision']+'; original capture/queues plus isolated CEF build compatibility patch and encoder-settings adapter'],
 ['SuiteCut CEF','152.0.6 / Chromium 152.0.7977.83; identical libcef SHA-256 for TS and C++'],
 ['OBS','32.2.0, embedded Chromium 127.0.6533.120; OBS-patched CEF distribution'],
 ['Standalone browser','Playwright Chromium 151.0.7922.34 / Playwright 1.62.1'],
 ['FFmpeg','SuiteCut/system 7.1.5; OBS bundled libav generation 6'],
 ['Host CPU',env.get('reportedCpuModel','unavailable')+'; '+env.get('cpuIsolation','four-core cgroup quota')],
 ['Graphics','No /dev/dri hardware GPU device; software rendering/encoding'],
 ['OBS external source','XSHM screen capture of standalone Chromium, 87-pixel UI crop; Pulse monitor for streaming audio'],
 ['C++ external browser','Automation verified separately; native capture unavailable']]), '',
 'Different CEF/Chromium versions, OBS software graphics/composition, screenshot versus native OSR capture, FFmpeg/libav versions, codec rate-control details, and the OBS silent recording track limit causal claims. The historical CEF shim uses the same CEF as C++ to control one of these differences; its small compatibility patch is saved. The historical snapshot is not modified to fix its capture algorithm. These whole-pipeline tests do not isolate programming-language choice or prove that JavaScript single-thread execution alone causes any difference.', '']
 short={}
 for mode in ['record','stream']:
  for backend in ['cpp-cef','obs-cef']:
   rr=[r for r in perf if r['backend']==backend and r['mode']==mode and get(r,'analysis.mediaDecoded')]
   if rr:short[(mode,backend)]={key:statistics.median(values) for key in ['resources.cpuMeanCores','resources.rssPeakMiB','motion.distinctFps','audio.silence.interiorTotalMs'] if (values:=[get(r,'analysis.'+key) for r in rr if get(r,'analysis.'+key) is not None])}
 if len(short)==4:
  nr=short[('record','cpp-cef')];ns=short[('stream','cpp-cef')];ob=short[('record','obs-cef')];os_=short[('stream','obs-cef')]
  text += ['## Principal measured findings','',f"Across the short decoded trials, C++ CEF used median {nr['resources.cpuMeanCores']:.2f} CPU cores recording and {ns['resources.cpuMeanCores']:.2f} streaming, versus OBS CEF’s {ob['resources.cpuMeanCores']:.2f} and {os_['resources.cpuMeanCores']:.2f}. This is {100*(1-nr['resources.cpuMeanCores']/ob['resources.cpuMeanCores']):.1f}% and {100*(1-ns['resources.cpuMeanCores']/os_['resources.cpuMeanCores']):.1f}% lower product CPU in this configuration, with median distinct motion {nr['motion.distinctFps']:.1f} / {ns['motion.distinctFps']:.1f} FPS for C++.", '',f"Streaming audio continuity remains a correctness issue: C++ median interior silence was {ns.get('audio.silence.interiorTotalMs',0):.0f} ms per saved short output versus OBS CEF’s {os_.get('audio.silence.interiorTotalMs',0):.0f} ms. CPU savings do not establish superior overall capture quality. Every available path had at least one short streaming trial with detected interior silence. One historical CEF streaming attempt failed before completing its active workload; it remains in the denominators and evidence.", '',f"C++ did not use less summed RSS than OBS CEF: median short-trial peak RSS was {nr['resources.rssPeakMiB']:.0f} MiB recording and {ns['resources.rssPeakMiB']:.0f} MiB streaming, versus OBS CEF’s {ob['resources.rssPeakMiB']:.0f} / {os_['resources.rssPeakMiB']:.0f} MiB. Different browser generations and shared mappings limit causal interpretation.", '', 'Native external-browser capture remains unimplemented. These results support a CPU-efficiency improvement for the available native CEF path; they do not support declaring every browser flow ready or leak-free.','']
 native_soak=next((r for r in soaks if r['backend']=='cpp-cef'),None)
 if native_soak and get(native_soak,'analysis.mediaDecoded'):
  a=native_soak['analysis'];res=a['resources'];quiet=a['audio']['silence']
  text += [f"The native C++ thirty-minute output still contained {quiet['interiorTotalMs']/1000:.2f} seconds of interior silence, a longest quiet interval of {quiet['interiorMaximumMs']} ms, and a longest held video image of {a['motion']['maxHoldMs']/1000:.2f} seconds. Peak summed RSS was {res['rssPeakMiB']:.0f} MiB, with RSS/PSS growth of {res['rssSlopeMiBPerMinuteAfterWarmup']:.2f}/{res['pssSlopeMiBPerMinuteAfterWarmup']:.2f} MiB/min after the first two minutes. These observations leave capture-fidelity work and memory-growth investigation open; they do not justify a leak-free or production-ready claim.", '']
 for mode in ['record','stream']:
  text += [f'## {mode.capitalize()} trials','', 'Cells show median [minimum–maximum] across all decoded trials, including quality failures. Every individual trial is also saved in `trials.csv` and `runs.json`. A validation pass requires decoded marker/action checks and, for streaming, no detected interior silence. It does not certify 30 distinct FPS; motion performance is reported separately. Process cleanup success is reported independently.','']
  rows=[]
  for backend in ORDER:
   rr=[r for r in perf if r['backend']==backend and r['mode']==mode and get(r,'analysis.mediaDecoded')]
   allr=[r for r in perf if r['backend']==backend and r['mode']==mode]
   rows.append([LABELS[backend],f'{len(rr)}/{len(allr)}',f"{sum(r.get('mediaValidated',False) for r in allr)}/{len(allr)}",rng([get(r,'analysis.resources.cpuMeanCores') for r in rr]),rng([get(r,'analysis.resources.rssPeakMiB') for r in rr],0),rng([get(r,'analysis.motion.distinctFps') for r in rr]),rng([get(r,'analysis.motion.maxHoldMs') for r in rr],0)])
  rows.append([LABELS['cpp-external'],'Unavailable','—','—','—','—','—'])
  text += [table(['Backend','Decoded','Validation pass','Mean CPU cores','Peak RSS MiB','Distinct FPS','Longest hold ms'],rows),'',f'![{mode} comparison](figures/{mode}-comparison.png)','']
  if mode=='stream':
   rows=[]
   for backend in ORDER:
    rr=[r for r in perf if r['backend']==backend and r['mode']==mode and get(r,'analysis.mediaDecoded')]
    rows.append([LABELS[backend],rng([get(r,'analysis.audio.silence.interiorTotalMs') for r in rr],0),rng([get(r,'analysis.audio.silence.interiorMaximumMs') for r in rr],0),rng([get(r,'analysis.audio.silence.leadingMs') for r in rr],0),rng([get(r,'analysis.audio.sync.medianMs') for r in rr])])
   text += [table(['Backend','Interior silence total ms','Longest interior ms','Leading silence ms','Median audio−video ms'],rows),'']
 text += ['## Audio loss during the active workload','','The continuous test source has no intentional silence. Interior-only measurements can hide a stream that loses audio and never recovers: the historical external-Chromium ten-minute output contains about 550 seconds of trailing silence. The table below counts all quiet bins within the video-marker active window, including silence that continues to its end. The first and last selected output frames bound this decoded window; these are 5 ms signal-specific measurements.','']
 rows=[]
 for backend in ORDER:
  rr=[r for r in perf if r['backend']==backend and r['mode']=='stream' and get(r,'analysis.mediaDecoded')]
  rows.append([LABELS[backend],rng([get(r,'analysis.audio.activeWindow.quietTotalMs') for r in rr],0),rng([get(r,'analysis.audio.activeWindow.quietMaximumMs') for r in rr],0),rng([get(r,'analysis.audio.silence.trailingMs') for r in rr],0)])
 text += [table(['Backend','Active quiet total ms','Longest active quiet ms','Full-output trailing silence ms'],rows),'']
 rows=[]
 for r in soaks:
  rows.append([LABELS[r['backend']],fmt(r['durationMs']/60000,0),fmt(get(r,'analysis.audio.activeWindow.quietTotalMs')/1000) if get(r,'analysis.audio.activeWindow.quietTotalMs') is not None else '—',fmt(get(r,'analysis.audio.activeWindow.quietMaximumMs'),0),fmt(get(r,'analysis.audio.activeWindow.quietPercent')),fmt(get(r,'analysis.audio.silence.trailingMs'),0)])
 text += [table(['Backend','Minutes','Active quiet seconds','Longest active quiet ms','Active quiet %','Full-output trailing silence ms'],rows),'']
 text += ['## Native audio and encoding counters','','The native output distinguishes discarded packets from missing samples compensated with silence. A zero `droppedAudioSamples` counter does not mean loss-free audio. Decoded silence also includes quiet source samples and codec effects, so it need not equal the missing-sample counter. Empty final queues show drain completion rather than zero active buffering. These counters identify the observed compensation behavior; they do not by themselves establish whether source callback timing, scheduling or another implementation detail caused it.','']
 rows=[]
 for r in runs:
  if r['backend']!='cpp-cef' or r['mode']!='stream':continue
  f=get(r,'result.finalOutput') or {}
  rows.append([r['id'],fmt(f.get('missingAudioSamples',0)/48,1),f.get('droppedAudioSamples','—'),fmt(f.get('maximumEncodeMs'),1),fmt(f.get('bufferedDelayMs'),1),fmt(get(r,'analysis.audio.silence.interiorTotalMs'),0)])
 text += [table(['Run','Missing samples equivalent ms','Discarded audio samples','Maximum encode ms','Reported buffered delay ms','Decoded interior silence ms'],rows),'']
 text += ['## CPU by browser workload phase','','Median CPU cores across decoded short trials; per-process-role and per-phase details remain in `enriched-runs.json`. Native linked encoders run within the browser process, OBS encoding runs within OBS, and historical FFmpeg encoders are separate processes, so role labels are not identical work boundaries.','']
 rows=[]
 for mode in ['record','stream']:
  for backend in ORDER:
   rr=[r for r in perf if r['backend']==backend and r['mode']==mode and get(r,'analysis.mediaDecoded')]
   rows.append([LABELS[backend],mode]+[rng([get(r,'analysis.resourcesByPhase.'+name+'.cpuMeanCores') for r in rr]) for name in ['static','canvas','scroll']])
 text += [table(['Backend','Mode','Low motion CPU','Animation/input CPU','Scrolling CPU'],rows),'']
 text += ['## Memory profiles','','A flat RSS curve does not prove absence of leaks; allocator caches, browser caches and shared mappings affect RSS. Positive slope is evidence to investigate, not a leak diagnosis. Sanitizer results, if supplied separately, cover instrumented SuiteCut code rather than the prebuilt CEF binary.','']
 rows=[]
 for r in soaks:
  a=r.get('analysis',{});res=a.get('resources',{})
  rows.append([LABELS[r['backend']],fmt(r['durationMs']/60000,0),fmt(res.get('rssPeakMiB'),0),fmt(res.get('rssSlopeMiBPerMinuteAfterWarmup')),fmt(res.get('pssPeakMiB'),0),res.get('pssCompleteSamples',0),fmt(res.get('nodeHeapSlopeMiBPerMinuteAfterWarmup')),str(r.get('mediaValidated'))])
 text += [table(['Backend','Minutes','Peak RSS MiB','RSS MiB/min after 2m','Peak PSS MiB','Complete PSS samples','Node heap MiB/min','Validation pass'],rows),'','![Memory profiles](figures/memory-profiles.png)','', '']
 text += ['## Sustained streaming quality','','These are separate long-run observations, not additional short-trial repetitions. Silence totals cover the full saved output; the percentage normalizes for output duration. Native C++ runs longer, so compare normalized rates and the common first-ten-minute resource window.','']
 rows=[]
 for r in soaks:
  a=r.get('analysis',{});quiet=get(r,'analysis.audio.silence') or {};duration=a.get('mediaDurationSeconds')
  rows.append([LABELS[r['backend']],fmt(r['durationMs']/60000,0),fmt(get(r,'analysis.motion.distinctFps')),fmt(get(r,'analysis.motion.maxHoldMs'),0),fmt(quiet.get('interiorTotalMs',0)/1000) if quiet else '—',fmt(quiet.get('interiorMaximumMs'),0),fmt(100*quiet.get('interiorTotalMs',0)/(duration*1000)) if duration and quiet else '—',r['returncode']])
 text += [table(['Backend','Minutes','Distinct FPS','Longest hold ms','Interior silence seconds','Longest silence ms','Interior silence %','Process exit'],rows),'','![Sustained streaming audio continuity](figures/audio-continuity.png)','']
 text += ['## Private memory and Node heap','','Private-memory metrics use complete product-process snapshots. Node heap/external trends use natural garbage collection; they are not forced-GC retained-heap measurements.','']
 rows=[]
 for r in soaks:
  res=get(r,'analysis.resources') or {};private=get(r,'analysis.privateMemoryTotals') or {}
  rows.append([LABELS[r['backend']],fmt(res.get('pssSlopeMiBPerMinuteAfterWarmup')),fmt(private.get('ussPeakMiB'),0),fmt(private.get('ussSlopeMiBPerMinuteAfter2m')),fmt(res.get('nodeHeapSlopeMiBPerMinuteAfterWarmup')),fmt(res.get('nodeExternalSlopeMiBPerMinuteAfterWarmup'))])
 text += [table(['Backend','PSS MiB/min after 2m','Peak USS MiB','USS MiB/min after 2m','Node heap MiB/min','Node external MiB/min'],rows),'']
 text += ['## Common 10-minute memory window','','The resource window compares the first ten active minutes. Audio values cover the first 600 seconds of saved output and include quiet output edges. The full 30-minute C++ profile is additional durability evidence.','']
 rows=[]
 for r in soaks:
  w=get(r,'analysis.commonFirst10Minutes') or {}
  rows.append([LABELS[r['backend']],fmt(w.get('cpuMeanCores')),fmt(w.get('rssPeakMiB'),0),fmt(w.get('rssSlopeMiBPerMinuteAfter2m')),fmt(w.get('rssMedianFirstMinuteMiB'),0),fmt(w.get('rssMedianLastMinuteMiB'),0),fmt(w.get('first600OutputSecondsQuietAudioSeconds')),fmt(w.get('first600OutputSecondsQuietAudioPercent'))])
 text += [table(['Backend','Mean CPU cores','Peak RSS MiB','RSS MiB/min after 2m','First minute RSS MiB','Last minute RSS MiB','First 600s quiet audio seconds','First 600s quiet audio %'],rows),'','## Observer and host context','','Observer, receiver and display costs are excluded from the product CPU/RSS tables but consume the same host CPU quota. Host throttling counters cover the entire cgroup; unavailable CPU/memory pressure files are explicitly flagged in raw samples. Audio statistics cover the full saved output, including startup/finalization, while product resource totals cover the marked active workload.','']
 rows=[]
 for backend in ORDER:
  rr=[r for r in runs if r['backend']==backend and get(r,'analysis.mediaDecoded')]
  rows.append([LABELS[backend],rng([get(r,'analysis.observer.cpuMeanCores') for r in rr],3),rng([get(r,'analysis.observer.p95CollectionMs') for r in rr],1),rng([get(r,'analysis.hostCpu.throttledPeriodsFraction') for r in rr],3)])
 text += [table(['Backend','Observer CPU cores','Observer p95 ms','Host throttled-period fraction'],rows),'','']
 text += ['## Verification and preserved earlier evidence','']
 verify_path=OUT/'sanitizer-verification.json'
 if verify_path.exists():
  verify=json.loads(verify_path.read_text());text += [f"Fresh ASan/UBSan verification passed {verify['testsPassed']}/{verify['testsTotal']} native tests with leak detection and assertions enabled. Instrumented scope: SuiteCut media, audio clock/startup, codec, pixel conversion, memory lifecycle and scaler failure. Prebuilt CEF and FFmpeg libraries are not fully instrumented. Saved configuration, compile flags and test logs are included. No sanitizer errors were reported in these tests; this does not prove universal browser or long-running leak freedom.", '']
 prior_path=OUT/'prior-evidence-verification.json'
 if prior_path.exists():
  prior=json.loads(prior_path.read_text());text += [prior['explanation'],'']
 text += ['The historical default recording codec was VP9. This controlled comparison adapts it to H.264 with the common encoder settings, and rebuilds its shim on common CEF 152. It is a reconstructed pipeline comparison, rather than a benchmark of untouched historical shipping defaults. OS caches were not flushed; trial order is balanced and browser profiles are fresh.', '', 'The exact executed capture versions are saved as `capture-before-final-scroll-validation.mjs` and `tested-sources/benchmarks/publication/capture.mjs`. The current repository harness was formatted after measurements. The validation-only correction is explained in `scroll-validation-correction.md`; the runner continuation change is explained in `runner-control-changes.md`.','']
 text += ['The reusable analyzer now checks all quiet bins in the active video window, including unrecovered end silence and missing audio coverage over a 60 ms mux/frame-edge allowance. Eight regression tests passed, and all 29 completed streaming datasets reproduce the published active-window quiet totals and maximum intervals exactly. The original executed analyzer and trial validation results remain preserved; this forward correction is saved separately and explained in `audio-oracle-correction.md`. The validation-pass counts above retain the original interior-silence criterion; the active-window audio tables expose additional end silence.', '', '## Failures and saved evidence','']
 issues=[]
 for r in runs:
  if not r.get('mediaValidated') or not r.get('processSucceeded'):
   issues.append([r['id'],r['returncode'],'; '.join(get(r,'analysis.validationIssues') or []) or get(r,'analysis.error') or 'Process/cleanup failure',str(get(r,'result.cleanupFailure') or get(r,'result.error') or '—')])
 text += [table(['Run','Process exit','Validation issue','Runtime/cleanup issue'],issues) if issues else 'Every completed run passed the defined quality checks and process cleanup.', '',
 'Calibration attempts are retained under `calibration-*`, `smoke*`, `preflight`, `failed-runtime-lock-attempt`, and `failed-resource-sampler`. They include rejected scene/crop setup, missing pressure files, incomplete process inventory, and synthetic-audio/encoder pacing experiments. They are excluded from the tables above; their measurements must not be quoted as final product performance.', '',
 'Each accepted trial contains output media, actual fixture HTML, input/output snapshots, encoder arguments where applicable, browser/OBS/runtime logs, original process samples, decoded markers, frame timestamps, audio RMS/anchors, FFprobe metadata and analysis JSON. `tested-sources`, `tested-sdk`, historical snapshots and native binaries pin the exact tested implementations. `environment.json` records CEF/OBS/library hashes and host limits. The final checksum manifest and archive inventory make the saved evidence reviewable.', '',
 '## Reproduction', '',
 'Use the pinned source/SDK snapshots and matching native/CEF/OBS dependencies from `environment.json`. This workspace already contains private OBS/Pulse/Xvfb runtime trees and the codec-enabled CEF SDK; these binaries are not installed by the benchmark script. The historical build compatibility patch and source snapshots are included. Validate all executable versions/hashes and the 87-pixel external-browser UI crop on the target host before reusing this runtime.', '',
 '```sh\npython benchmarks/publication/test_analyze.py\npython benchmarks/publication/run.py\npython benchmarks/publication/publish.py\n```', '',
 '`PUBLICATION_DIRECTORY` selects a fresh results directory. `PUBLICATION_TRIALS`, `PUBLICATION_TRIAL_MS`, `PUBLICATION_MEMORY_MS`, and `PUBLICATION_NATIVE_MEMORY_MS` override the saved defaults. `run.py` resumes completed IDs from `runs.json`; it rejects unfinished existing run directories rather than overwriting evidence. A single-workload lock prevents accidental overlapping captures. Ensure adequate free disk for media and the final archive. Move rejected attempts into a clearly labeled calibration folder before resuming.','',
 'The report is prepared for publication; no public upload or deployment is performed.']
 (OUT/'report.md').write_text('\n'.join(text)+'\n')
 # A self-contained HTML data table with relative artifact links requires no optional Markdown dependency.
 try:
  import markdown
  body=markdown.markdown('\n'.join(text),extensions=['tables','fenced_code'])
 except ImportError:
  body=html_document('\n'.join(text))
 style='body{font:16px/1.6 system-ui;max-width:1280px;margin:40px auto;padding:0 24px;color:#18202c}.table{overflow:auto}table{border-collapse:collapse;width:100%;font-size:14px}td,th{border:1px solid #dde3ea;padding:8px;text-align:left}th{background:#eef2f8}img{max-width:100%}pre{overflow:auto;background:#f3f5f8;padding:16px}h2{margin-top:36px}a{color:#315bb9}'
 (OUT/'report.html').write_text('<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>SuiteCut / OBS 1080p benchmark</title><style>'+style+'</style><body>'+body+'</body></html>')
 print(json.dumps({'report':str(OUT/'report.md'),'trials':len(runs),'seriesStatus':progress['status']}))

if __name__=='__main__':render()
