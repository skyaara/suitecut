"""Generate the saved report, flat CSV, aggregate JSON, and exportable charts."""
import csv, html, json, math
from datetime import datetime
from collections import defaultdict
from pathlib import Path
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

root = Path('benchmarks/obs-cef/2026-10-09')
trials = json.loads((root/'trials.json').read_text()) if (root/'trials.json').exists() else []
for row in trials:
    if row.get('startedAt') and row.get('completedAt'):
        row['attemptWallMs']=(datetime.fromisoformat(row['completedAt'].replace('Z','+00:00'))-datetime.fromisoformat(row['startedAt'].replace('Z','+00:00'))).total_seconds()*1000
    samples_path=root/'trials'/row['id']/'resources.jsonl'
    if 'resources' not in row and samples_path.exists():
        samples=[json.loads(line) for line in samples_path.read_text().splitlines() if line.strip()]
        if samples:
            row['resources']={'sampleCount':len(samples), 'meanCpuPercent':sum(s['cpuPercent'] for s in samples[1:])/max(1,len(samples)-1),
                              'peakRssBytes':max(s['rssBytes'] for s in samples),'firstRssBytes':samples[0]['rssBytes'],
                              'lastRssBytes':samples[-1]['rssBytes'],'derivedFromSavedSamples':True}
(root/'trials-enriched.json').write_text(json.dumps(trials,indent=2)+'\n')
features = json.loads((root/'features/results.json').read_text()) if (root/'features/results.json').exists() else []
# This run's loaded harness used wall duration for the initial streaming motion
# denominator. Keep raw results unchanged and derive playback metrics from the
# saved receiver duration, which includes drained publisher output.
for row in features:
    if 'durationMs' not in row:continue
    checks={c['name']:c for c in row['checks']}
    motion=checks.get('decoded receiver motion',{}).get('details')
    mux=checks.get('receiver media and timestamps',{}).get('details')
    if motion and mux and 'mediaSeconds' not in motion:
        duration=mux['durationSeconds']
        motion['originalWallDurationDistinctFps']=motion['distinctFps']
        motion['originalWallDurationLongestHoldMs']=motion['longestHoldMs']
        motion['mediaSeconds']=duration
        motion['distinctFps']=motion['markerTransitions']/duration
        motion['longestHoldMs']*=duration/(row['wallMs']/1000)
        motion['targetMotionStatus']='within 10% of 60 fps' if motion['distinctFps']>=54 else 'below target'
        motion['denominatorCorrection']='Playback rate uses received media duration, including drained output; original wall-duration calculation retained.'
(root/'features/results-enriched.json').write_text(json.dumps(features,indent=2)+'\n')
coverage=[]
for name in ['typing','click','in-page select control','wheel scrolling','scroll to element and back to top','navigation and return','source switching','encoded audio tones','encoded audio disabled','decoded source identity','recorded input and scrolling states','receiver media and timestamps','decoded receiver motion','receiver audible page tone','decoded audio and video sync']:
    item={'feature':name,'backends':{}}
    for backend in ['obs','native']:
        checks=[{'run':row['id'],**check} for row in features if row['backend']==backend for check in row['checks'] if check['name']==name]
        item['backends'][backend]={'status':'not tested' if not checks else 'failed' if any(c['status']=='failed' for c in checks) else 'passed','evidence':checks}
    coverage.append(item)
for name,detail in [
    ('OBS embedded browser source','unavailable in installed OBS package; external Chromium/XSHM was measured'),
    ('hardware encoding and GPU texture sharing','hardware encoder unavailable in this host; SuiteCut GPU texture sharing is not implemented'),
    ('zoom transforms, highlight overlays, cursor animation, and captions','not tested; separate fixtures would be required to compare these with OBS transforms/overlays'),
    ('real speech-model synthesis','outside OBS capture comparison; tone fixtures do not establish narration-model quality'),
    ('automatic RTMP reconnection and streaming source switches','not tested; recording source switches and normal RTMP start/stop were tested'),
    ('SuiteCut runner checkpoints, retries, and test-report semantics','not a direct OBS operation; excluded from comparable timed workloads')]:
    coverage.append({'feature':name,'status':'not tested / unavailable / excluded as described','detail':detail})
(root/'coverage.json').write_text(json.dumps(coverage,indent=2)+'\n')
with (root/'coverage.csv').open('w',newline='') as file:
    writer=csv.writer(file);writer.writerow(['Feature','OBS','SuiteCut native','Scope'])
    for item in coverage:
        writer.writerow([item['feature'],item.get('backends',{}).get('obs',{}).get('status',item.get('status')),item.get('backends',{}).get('native',{}).get('status',item.get('status')),item.get('detail','')])

environment = json.loads((root/'environment.json').read_text())
stream_resources=[]
for row in features:
    path=root/'features'/row['id']/'resources.jsonl'
    if not path.exists():continue
    samples=[json.loads(line) for line in path.read_text().splitlines() if line.strip()]
    if not samples:continue
    times=np.array([s['monotonic']-samples[0]['monotonic'] for s in samples])/60
    rss=np.array([s['rssBytes'] for s in samples])/1024**2
    summary={'id':row['id'],'samples':len(samples),'meanCpuPercent':float(np.mean([s['cpuPercent'] for s in samples[1:]])) if len(samples)>1 else None,
             'peakRssMiB':float(max(rss)),'firstRssMiB':float(rss[0]),'lastRssMiB':float(rss[-1]),
             'rssTrendMiBPerMinute':float(np.polyfit(times,rss,1)[0]) if len(samples)>2 and times[-1]>0 else None}
    stream_resources.append(summary)
(root/'stream-resources.json').write_text(json.dumps(stream_resources,indent=2)+'\n')
groups = defaultdict(list)
for row in trials:
    groups[(row['case']['name'], row['backend'])].append(row)
metrics = {'distinctFps': ('decoded','distinctActiveFps'), 'captureFps': ('nativeCapture','fps'),
           'repeatFraction': ('decoded','repeatFraction'), 'longestHoldMs': ('decoded','longestActiveHoldMs'),
           'cpuPercent': ('resources','meanCpuPercent'), 'peakRssBytes': ('resources','peakRssBytes'),
           'startupMs': ('startupMs',), 'finalizationMs': ('finalizationMs',), 'attemptWallMs': ('attemptWallMs',)}
def value(row, keys):
    for key in keys:
        if not isinstance(row, dict): return None
        row = row.get(key)
    return row if isinstance(row,(float,int)) and math.isfinite(row) else None
aggregates = []
for (case,backend),rows in groups.items():
    entry={'case':case,'backend':backend,'trials':len(rows),'passed':sum(r['status']=='passed' for r in rows),
           'failed':sum(r['status']=='failed' for r in rows),'running':sum(r['status']=='running' for r in rows),
           'configuredFps':rows[0]['case']['fps'], 'expectedTrials':5, 'metrics':{}}
    for name,keys in metrics.items():
        numbers=[v for r in rows if (v:=value(r,keys)) is not None]
        entry['metrics'][name]={'observed':len(numbers),'missing':len(rows)-len(numbers),
                                **({'median':float(np.median(numbers)), 'p05':float(np.percentile(numbers,5)),
                                    'p95':float(np.percentile(numbers,95)), 'min':min(numbers),'max':max(numbers)} if numbers else {})}
    aggregates.append(entry)
(root/'aggregates.json').write_text(json.dumps(aggregates,indent=2)+'\n')
columns=['id','case','backend','trial','status','configuredFps','targetMotionStatus',*metrics,'durationSeconds','decodedFrames','inputAnimationFrames','scrollEvents','error','mediaPath']
with (root/'trials.csv').open('w',newline='') as file:
    writer=csv.DictWriter(file,fieldnames=columns);writer.writeheader()
    for row in trials:
        fps=value(row,metrics['distinctFps'])
        flat={'id':row['id'],'case':row['case']['name'],'backend':row['backend'],'trial':row['trialNumber'],
              'status':row['status'],'configuredFps':row['case']['fps'],
              'targetMotionStatus':'not applicable' if row['case']['workload']=='static' else ('missing' if fps is None else 'within 10%' if fps>=row['case']['fps']*.9 else 'below target'),
              **{name:value(row,keys) for name,keys in metrics.items()},
              'durationSeconds':value(row,('decoded','durationSeconds')),'decodedFrames':value(row,('decoded','decodedFrames')),
              'inputAnimationFrames':value(row,('workload','snapshot','markerCount')),'scrollEvents':value(row,('workload','snapshot','scrollEvents')),
              'error':row.get('error',''),'mediaPath':row.get('mediaPath','')}
        writer.writerow(flat)
order=list(dict.fromkeys(r['case']['name'] for r in trials))
plt.rcParams.update({'font.size':10,'axes.spines.top':False,'axes.spines.right':False,'figure.dpi':140})
colors={'obs':'#2a6fbb','native':'#b55422'}
def plot(metric, filename, ylabel, excluded=()):
    cases=[c for c in order if c not in excluded];x=np.arange(len(cases))
    fig,ax=plt.subplots(figsize=(12,5))
    for backend,offset in [('obs',-.19),('native',.19)]:
        heights=[]
        for case in cases:
            group=next((a for a in aggregates if a['case']==case and a['backend']==backend),None)
            number=group['metrics'][metric].get('median') if group else None
            heights.append(number if number is not None else np.nan)
        scale=1024**2 if metric=='peakRssBytes' else 1000 if metric=='finalizationMs' else 1
        bars=ax.bar(x+offset,np.array(heights)/scale,.36,label=backend,color=colors[backend])
        for bar,height in zip(bars,heights):
            if math.isfinite(height):ax.annotate(f'{height/scale:.1f}',(bar.get_x()+bar.get_width()/2,bar.get_height()),xytext=(0,3),textcoords='offset points',ha='center',fontsize=8)
            else:ax.text(bar.get_x()+bar.get_width()/2,.5,'N/A',ha='center',fontsize=8,color=colors[backend])
    if metric=='distinctFps':
        targets=[next(a['configuredFps'] for a in aggregates if a['case']==case) for case in cases]
        ax.scatter(x,targets,marker='_',s=400,color='#333333',label='configured FPS')
    ax.set_xticks(x,cases,rotation=25,ha='right');ax.set_ylabel(ylabel);ax.legend();ax.grid(axis='y',alpha=.2);ax.set_axisbelow(True);fig.tight_layout();fig.savefig(root/filename);fig.savefig((root/filename).with_suffix('.png'));plt.close(fig)
if aggregates:
    plot('distinctFps','motion.svg','Distinct decoded frames per second',('720p60-static',))
    plot('finalizationMs','finalization.svg','Finalization seconds')
    plot('peakRssBytes','memory.svg','Peak summed process-tree RSS (MiB)')
soaks=[r for r in features if r.get('durationMs')==300000 and (root/'features'/r['id']/'resources.jsonl').exists()]
if soaks:
    fig,axes=plt.subplots(2,1,figsize=(12,6),sharex=True)
    for row in soaks:
        samples=[json.loads(line) for line in (root/'features'/row['id']/'resources.jsonl').read_text().splitlines() if line.strip()]
        if not samples:continue
        times=[(s['monotonic']-samples[0]['monotonic'])/60 for s in samples]
        axes[0].plot(times,[s['cpuPercent'] for s in samples],label=row['backend'],color=colors[row['backend']],alpha=.8)
        axes[1].plot(times,[s['rssBytes']/1024**2 for s in samples],label=row['backend'],color=colors[row['backend']])
    axes[0].set_ylabel('Summed CPU %');axes[1].set_ylabel('Summed RSS MiB');axes[1].set_xlabel('Minutes from sampler start')
    for ax in axes:ax.legend();ax.grid(alpha=.2)
    fig.tight_layout();fig.savefig(root/'sustained-resources.svg');fig.savefig(root/'sustained-resources.png');plt.close(fig)
def fmt(n,digits=2): return '—' if n is None else f'{n:.{digits}f}'
def med(entry,metric,divisor=1):
    n=entry['metrics'][metric].get('median');return fmt(n/divisor if n is not None else None)
def esc(s):return html.escape(str(s))
table=[]
for entry in aggregates:
    table.append(f"<tr><td>{esc(entry['case'])}</td><td>{entry['backend']}</td><td>{entry['passed']}/{entry['trials']} (fail {entry['failed']})</td><td>{med(entry,'distinctFps')}</td><td>{fmt(entry['metrics']['distinctFps'].get('p05'))}–{fmt(entry['metrics']['distinctFps'].get('p95'))}</td><td>{med(entry,'captureFps')}</td><td>{med(entry,'longestHoldMs')}</td><td>{med(entry,'cpuPercent')}</td><td>{med(entry,'peakRssBytes',1024**2)}</td><td>{med(entry,'finalizationMs',1000)}</td></tr>")
feature_rows=[]
for row in features:
    for check in row['checks']:
        feature_rows.append(f"<tr><td>{esc(row['id'])}</td><td>{esc(check['name'])}</td><td>{esc(check['status'])}</td><td><details><summary>Evidence</summary><pre>{esc(json.dumps(check.get('details',check.get('error')),indent=2))}</pre></details></td></tr>")
    if row.get('error'):feature_rows.append(f"<tr><td>{esc(row['id'])}</td><td>Run error</td><td>failed</td><td><pre>{esc(row['error'])}</pre></td></tr>")
sustained_chart='<img src="sustained-resources.svg" alt="Five-minute stream CPU and memory time series">' if soaks else ''
links=''
for row in trials:
    path=root/'trials'/row['id']
    frame='frame.png' if (path/'frame.png').exists() else 'partial-frame.png' if (path/'partial-frame.png').exists() else None
    image_link=f" · <a href='trials/{esc(row['id'])}/{frame}'>decoded {'partial ' if frame=='partial-frame.png' else ''}frame</a>" if frame else ' · no finalized frame'
    links+=f"<li><a href='trials/{esc(row['id'])}/result.json'>{esc(row['id'])}: {esc(row['status'])}</a>{image_link} · <a href='trials/{esc(row['id'])}/resources.jsonl'>resources</a></li>"
outcome={'recordingTrials':len(trials),'recordingPassed':sum(r['status']=='passed' for r in trials),'recordingFailed':sum(r['status']=='failed' for r in trials),
         'featureRuns':len(features),'featureChecks':sum(len(r['checks']) for r in features),'featureChecksFailed':sum(c['status']=='failed' for r in features for c in r['checks']),
         'featureRunErrors':sum(bool(r.get('error')) for r in features),'incompleteRuns':[r['id'] for r in features if not r.get('completedAt')]}
(root/'outcome.json').write_text(json.dumps(outcome,indent=2)+'\n')
stream_table=[]
for row in features:
    if 'durationMs' not in row:continue
    evidence={c['name']:c.get('details',{}) for c in row['checks'] if c['status']=='passed'}
    motion=dict(evidence.get('decoded receiver motion',{}));mux=evidence.get('receiver media and timestamps',{})
    probe_path=root/'features'/row['id']/'probe.json'
    if not mux and probe_path.exists():
        probe=json.loads(probe_path.read_text());duration=float(probe.get('format',{}).get('duration',0));size=float(probe.get('format',{}).get('size',0))
        mux={'durationSeconds':duration,'averageMuxedKbps':size*8/duration/1000 if duration else None}
    if motion and mux.get('durationSeconds') and 'mediaSeconds' not in motion:
        duration=mux['durationSeconds'];motion['distinctFps']=motion['markerTransitions']/duration;motion['longestHoldMs']*=duration/(row['wallMs']/1000)
    sync_path=root/'features'/row['id']/'sync.json'
    sync=json.loads(sync_path.read_text()) if sync_path.exists() else {}
    status='failed' if row.get('error') or any(c['status']=='failed' for c in row['checks']) else 'completed' if row.get('completedAt') else 'incomplete'
    stream_table.append(f"<tr><td>{esc(row['id'])}</td><td>{status}</td><td>{fmt(row['durationMs']/1000)}</td><td>{fmt(mux.get('durationSeconds'))}</td><td>{fmt(motion.get('distinctFps'))}</td><td>{esc(motion.get('targetMotionStatus','unavailable'))}</td><td>{fmt(mux.get('encodedFps'))}</td><td>{fmt(mux.get('averageMuxedKbps'))}</td><td>{fmt(sync.get('p95AbsoluteOffsetMs'))}</td></tr>")
stream_summary='<div class="scroll"><table><tr><th>Stream</th><th>Checks</th><th>Requested s</th><th>Received s</th><th>Distinct FPS</th><th>60 FPS motion target</th><th>Encoded FPS</th><th>Muxed kbps</th><th>A/V p95 abs ms</th></tr>'+''.join(stream_table)+'</table></div>'
body=f'''<!doctype html><meta charset="utf-8"><title>SuiteCut and OBS benchmark — 9 October 2026</title>
<style>body{{font:16px/1.55 system-ui;max-width:1400px;margin:40px auto;padding:0 24px;color:#172330;background:#fafbfc}}h1{{font-size:34px}}h2{{margin-top:40px}}table{{border-collapse:collapse;width:100%;font-size:13px}}td,th{{padding:10px;text-align:left;border-bottom:1px solid #dbe1e8;vertical-align:top}}th{{background:#eaf0f6}}pre{{white-space:pre-wrap;max-width:700px;overflow:auto}}img{{width:100%;max-width:1200px}}a{{color:#18568b}}.scroll{{overflow:auto}}.note{{padding:20px;background:#fff1dd;border-left:4px solid #b55422}}code{{background:#edf1f4;padding:2px 5px}}</style>
<h1>SuiteCut CEF / OBS benchmark</h1><p>9 October 2026 · Asia/Calcutta · Linux cloud host · evidence from actual recordings and a local RTMP receiver.</p>
<p><a href="report.md">Detailed Markdown report</a> · <a href="trials.csv">Trial CSV</a> · <a href="trials.json">Raw trial JSON</a> · <a href="trials-enriched.json">Trials with resource summaries</a> · <a href="aggregates.json">Aggregate JSON</a> · <a href="environment.json">Environment</a> · <a href="features/results-enriched.json">Functional and stream evidence</a> · <a href="features/results.json">Original feature results</a> · <a href="coverage.json">Coverage status</a> · <a href="coverage.csv">Coverage CSV</a> · <a href="failure-artifacts.json">Partial failure artifacts</a></p>
<div class="note"><strong>Interpretation:</strong> a passed trial verifies readable media, dimensions, decoded motion, and scrolling where applicable. It does not mean the requested FPS was achieved. Recording codecs and browser versions differ. This is an end-to-end comparison of the available pipelines, with separate capture and decoded-motion measurements.</div>
<h2>Measured pipelines and scope</h2><ul><li>SuiteCut: sandboxed CEF {environment['cefVersion']}, I420 raw frames, production <code>recordNative</code>, VP9 CRF 18, realtime, cpu-used 5, row-mt, four encoder threads. Audio disabled during recording performance trials.</li><li>OBS: {environment['obsVersion']}, Chromium 151.0.7922.34 on Xvfb, XSHM screen capture cropped to webpage pixels, x264 veryfast CRF 18, H.264 MKV. Its recording includes a silent AAC track. OBS's installed package has no browser-source plugin.</li><li>Four CPU equivalents / 16 GiB cgroup limit; Mesa llvmpipe software graphics. Xvfb is 3840×2300; OBS crop removes 87 pixels of Chromium UI. Captured dimensions are checked at every resolution.</li><li>Five independent trials per case, ten-second active workload, fresh browser profile, one-second page warmup and half-second encoder warmup. Backend order alternates. Tests run sequentially. Idle OBS screen capture is disabled during native trials.</li><li>Actual wheel events move a long document up/down. A fixed binary animation counter distinguishes repeated encoded frames from newly rendered frames; static pages have no motion target.</li></ul>
<h2>Recording results</h2><p>{outcome['recordingTrials']} completed attempts: {outcome['recordingPassed']} valid recordings, {outcome['recordingFailed']} failed trials. Failures remain in the results and evidence archive.</p><p>Metrics summarize available observations, with failed counts retained. Five samples support descriptive ranges, not precise population tail estimates. Missing observations are counted in <a href="aggregates.json">aggregates.json</a>. CPU is summed process-tree usage over capture and finalization: 100% means one busy CPU; shared memory can be counted more than once in summed RSS.</p>
<div class="scroll"><table><thead><tr><th>Case</th><th>Pipeline</th><th>Valid / attempted</th><th>Distinct FPS median</th><th>FPS p05–p95</th><th>CEF capture FPS</th><th>Longest hold median (ms)</th><th>Mean CPU %</th><th>Peak RSS MiB</th><th>Finalize s</th></tr></thead><tbody>{''.join(table)}</tbody></table></div>
<img src="motion.svg" alt="Distinct decoded FPS by case and pipeline"><img src="finalization.svg" alt="Recording finalization cost"><img src="memory.svg" alt="Peak summed process memory">
<h2>Common functionality, audio, and streaming</h2><p>{outcome['featureRuns']} runs; {outcome['featureChecks']} operation/media checks; {outcome['featureChecksFailed']} failed checks; {outcome['featureRunErrors']} run errors. See <a href="outcome.json">saved outcome counts</a> and <a href="evidence-validation.json">final evidence validation</a>.</p>{stream_summary}<p>Streaming motion rates use received media duration, including drained output. The loaded harness initially used wall duration; the report derives playback rates from saved receiver durations and preserves original calculations in results-enriched.json. The executed harness snapshot is saved as features-executed.mjs. A passed motion check confirms moving output; the separate target status says whether it reached 90% of the configured 60 FPS. These checks exercise operations observable through OBS capture: typing, clicks, wheel scrolling, scroll-to-element/top, navigation, source changes, recording, audio on/off, final rendering, local RTMP publishing, and five-minute sustained streaming. Browser input in OBS is driven by a separate Playwright controller; OBS records that browser. OBS itself does not provide SuiteCut's locator or scripting API. A final audit verifies the encoded action sequence 0 → 1 → 3 → 7 → 15 only in primary-source frames, excluding blank navigation frames; per-recording recorded-states-validation.json files retain that evidence.</p><div class="scroll"><table><tr><th>Run</th><th>Check</th><th>Status</th><th>Evidence</th></tr>{''.join(feature_rows)}</table></div>
<p>Streaming also compares production settings rather than a normalized encoder: SuiteCut uses x264 veryfast/zerolatency, a 4500 kbps target/maxrate, 9000 kbps VBV buffer, and AAC 128 kbps; OBS uses x264 veryfast/high, 4500 kbps CBR, normal B-frame reordering, and AAC 160 kbps. Muxed kbps is measured from receiver files and can differ substantially from the configured target.</p><h2>Sustained stream resources</h2><p><a href="stream-resources.json">Resource summaries and memory trends</a>. Trends summarize this bounded run; summed RSS includes shared pages and does not establish a leak. Sampling includes the controller, its browser/publisher/receiver descendants, Xvfb, and OBS during OBS runs. The separate shared PulseAudio server is excluded.</p>{sustained_chart}
<h2>Setup verification</h2><p>The <a href="logs/ctest.log">native C++ checks</a>, <a href="native-browser-report.json">CEF browser verifier</a>, and <a href="logs/typescript-build.log">TypeScript build</a> passed. The browser verifier covers navigation, real mouse input, audible native samples, dialog suppression, 2× device scale, 3840×2160 frames, profile isolation, renderer failure handling, and shutdown. These infrastructure checks are saved separately from the OBS-comparable benchmarks. See <a href="setup-fixes.patch">setup source fixes</a>, <a href="cef-linux64-distribution.json">CEF distribution metadata</a>, and <a href="package-checksums.sha256">package checksums</a>.</p><h2>Setup corrections and limitations</h2><ul><li>Qualified the ambiguous CEF popup disposition type for GCC. Suppressed Chromium's first-run UI for fresh profiles; it previously prevented the windowless browser handshake. Native C++ checks and browser integration verification pass.</li><li>OBS is extracted under a user-owned directory. Its compiled muxer path is relocated to a wrapper that supplies the extracted runtime libraries. The original package checksums and relocation procedure are saved.</li><li>Loopback RTMP measures this host's media pipeline, not Internet uplink or remote-service latency. Publisher-ready timing is not end-to-end latency. Periodic A/V flash/tone matching has a ±500 ms ambiguity and includes browser scheduling error. Offsets describe detected flashes; slow video can miss pulses, and matched-flash counts are saved. The fixture target is p95 absolute offset below 200 ms; decoded audio and video are padded to a common media start before matching.</li><li>Hardware encoding, GPU texture sharing, OBS browser-source performance, external web services, real speech models, full Playwright compatibility, and automated benchmark thresholds are outside the measured scope.</li><li>OBS desktop audio captures its monitor sink. The source-switch fixture stops the inactive browser page's audio; this is explicit controller behavior. SuiteCut renders its selected source audio from isolated source clips.</li><li>SuiteCut's low decoded recording FPS and long held frames are preserved as measurements. Encoder overload and constant-rate repeated-frame catch-up are plausible contributors; these results do not establish one universal cause or performance on another host.</li></ul>
<h2>Saved trial evidence</h2><details><summary>All trials</summary><ul>{links}</ul></details>
<p>Setup diagnostics, failed smoke recordings, and the interrupted pilot are retained separately and excluded from the repeated-trial aggregates. Exact settings, logs, media probes, marker IDs, resource samples, screenshots, manifests, and raw recording paths are saved. See <a href="../README.md">reproduction instructions</a>.</p>'''
(root/'report.html').write_text(body)
md=['# SuiteCut CEF / OBS benchmark — 9 October 2026','','Client timezone: Asia/Calcutta. Raw timestamps are UTC.','',
    'This report compares the available production recording pipelines. Valid media does not establish the configured motion rate. OBS uses Chromium/XSHM/x264; SuiteCut uses CEF/I420/VP9. Browser versions, codec behavior, audio-track presence, and encoder settings differ. No codec-normalized speedup is claimed.','',
    '| Case | Backend | Valid/attempted | Distinct FPS median | Capture FPS | Longest hold ms | CPU % | RSS MiB | Finalize s |',
    '|---|---|---:|---:|---:|---:|---:|---:|---:|']
for a in aggregates:md.append(f"| {a['case']} | {a['backend']} | {a['passed']}/{a['trials']} | {med(a,'distinctFps')} | {med(a,'captureFps')} | {med(a,'longestHoldMs')} | {med(a,'cpuPercent')} | {med(a,'peakRssBytes',1024**2)} | {med(a,'finalizationMs',1000)} |")
md+=['','Five trials per case; ten-second active workloads; one-second page warmup and half-second encoder warmup. Alternating backend order, serial execution. Actual wheel events; fixed binary animation markers decoded from saved media. Static content has no distinct-FPS target. CPU/RSS cover capture and finalization, not solely the steady active interval. PNG and SVG chart exports are saved beside this report. Raw resource samples are retained.','',
     'CPU quota: four equivalents. Memory limit: 16 GiB. Graphics: Mesa llvmpipe. Software encoding. Five-sample percentiles are descriptive. Aggregates retain failed counts and missing observations.','',
     '## Functional / streaming results','',f"{outcome['featureRuns']} runs; {outcome['featureChecks']} checks, {outcome['featureChecksFailed']} failed checks, {outcome['featureRunErrors']} run errors. Counts: outcome.json.",'']
for row in features:
    md.append(f"### {row['id']}")
    md.append('')
    if row.get('error'):md.append('Run error: '+row['error'])
    for check in row['checks']:md.append(f"- {check['name']}: **{check['status']}**. `{json.dumps(check.get('details',check.get('error')))}`")
    md.append('')
md+=['## Scope and practical limits','',
     '- OBS browser-source plugin is absent; comparison uses real OBS capture of external Chromium. Browser input is driven by Playwright. This is observable functional equivalence, not native API parity.',
     '- Audio on/off is verified from decoded PCM; source identities are verified from encoded color patches. Source switching in OBS uses browser foreground selection and explicitly stops inactive page audio.',
     '- Streaming encoder settings also differ: native x264 veryfast/zerolatency, 4500 kbps target/maxrate with a 9000 kbps VBV buffer, AAC 128 kbps; OBS x264 veryfast/high, 4500 kbps CBR with B-frames, AAC 160 kbps. Configured bitrate is not observed throughput.',
     '- Streaming uses a local FFmpeg RTMP receiver. Packet timestamps (strictly increasing DTS and unique PTS, permitting B-frame presentation reordering), distinct decoded markers, tones, periodic A/V flashes, and time-series resource/status evidence are saved. Loopback results do not characterize Internet upload performance.',
     '- Periodic sync analysis resolves only offsets within half of the one-second pulse period. It includes browser AudioContext/rAF scheduling and is not a hardware latency measurement.',
     '- Five-minute runs are bounded soak observations, not proof of indefinitely stable streaming.',
     '- No hardware encoder, GPU sharing, OBS embedded-browser comparison, speech-model quality benchmark, external site benchmark, or complete Playwright compatibility claim is made.',
     '- CEF native C++ checks and browser verification passed after qualifying the popup callback type and disabling first-run UI. Sandbox settings remain enabled.',
     '- All smoke/setup failures and the interrupted pilot are retained outside final aggregates. Prior-chat temporary artifacts were absent in this fresh environment; their numeric results were not reused.','',
     '## Setup verification','',
     'Native C++ checks: logs/ctest.log. CEF browser integration: native-browser-report.json and logs/native-browser-verification.log. TypeScript build: logs/typescript-build.log. These passed; they are infrastructure verification rather than timed OBS comparisons.',
     '', '## Saved evidence','',
     'Open report.html for charts and expandable checks. trials.json, trials.csv, aggregates.json, environment.json, per-trial settings/results/probes/markers/resources/frames/manifests, feature evidence, setup logs, package hashes, and setup-fixes.patch are saved here. Media paths are in the raw JSON; the evidence archive includes recordings. Reproduction commands are in ../README.md.','']
(root/'report.md').write_text('\n'.join(md))
print(json.dumps({'trials':len(trials),'passed':sum(r['status']=='passed' for r in trials),'failed':sum(r['status']=='failed' for r in trials),'featureRuns':len(features)}))
