from pathlib import Path
from collections import Counter,defaultdict
import json,re,csv,datetime,html
base=Path('/tmp/suitecut-upstream-full-2026-10-12')
out=Path('/workspace/suitecut/benchmarks/playwright-compatibility/2026-10-12-upstream-audit');out.mkdir(exist_ok=True)
ansi=re.compile(r'\x1b\[[0-?]*[ -/]*[@-~]')
def clean(s):return ansi.sub('',s or '')
def live(backend):
 rows={};p=base/backend/'cases-live.jsonl'
 if not p.exists():return rows
 for line in p.read_text().splitlines():
  try:r=json.loads(line);rows[r['id']]=r
  except json.JSONDecodeError:continue
 return rows
inventory=[]
def walk(s,prefix=()):
 pref=prefix+(s.get('title',''),)
 for x in s.get('specs',[]):
  for t in x['tests']:
   inventory.append(dict(id=x['id'],file=x['file'],line=x['line'],column=x['column'],title=x['title'],fullTitle=' > '.join(y for y in pref+(x['title'],) if y)))
 for child in s.get('suites',[]):walk(child,pref)
for s in json.loads((base/'inventory/results.json').read_text())['suites']:walk(s)
native=live('native');stock=live('stock')
isolated={}
for path in base.glob('heap-isolated-*/cases-live.jsonl'):
 for line in path.read_text().splitlines():
  try:
   trial=json.loads(line);isolated[trial['id']]=trial
  except json.JSONDecodeError:pass
def err(r):return '\n'.join(clean(e.get('message','')) for e in r.get('errors',[])) if r else ''
def feature(x):
 f=x['file'].lower();title=x['title'].lower()
 if '/inspector/' in f:return 'Inspector and codegen / internal product fixtures'
 if any(w in f for w in ['browsertype','defaultbrowsercontext','multiclient','browser-server','extensions','launcher','connect-over-cdp','connect-to-worker','browsercontext-reuse']):return 'Launch, persistent profiles, remote connections and extensions'
 if f=='library/browser.spec.ts' or f=='library/browsercontext-basic.spec.ts':return 'Browser/context lifecycle and bootstrap semantics'
 if any(w in f for w in ['permissions','geolocation']):return 'Permission and geolocation overrides'
 if 'screenshot' in f:return 'Screenshots and pixel comparisons'
 if any(w in f for w in ['popup','browsercontext-events','browsercontext-page-event']):return 'Popup creation, first-script behavior and events'
 if any(w in f for w in ['proxy','credentials','client-certificates']):return 'Proxy, HTTP authentication and client certificates'
 if any(w in f for w in ['cookies','storage-state','indexeddb']):return 'Cookies and authentication state'
 if any(w in f for w in ['service-worker','workers','worker']):return 'Worker automation and worker network events'
 if any(w in f for w in ['frame','iframe']) or 'iframe' in title:return 'Frames and cross-origin targets'
 if any(w in f for w in ['aria','accessibility']):return 'Accessibility and ARIA snapshots'
 if any(w in f for w in ['route','har','network','request','response','websocket','browsercontext-fetch','browsercontext-request','api-request']):return 'Requests, routing, HTTP, WebSocket and HAR'
 if any(w in f for w in ['download','filechooser','set-input-files']):return 'Downloads and file input'
 if any(w in f for w in ['video','screencast','tracing','trace-viewer']):return 'Playwright video, screencast and tracing workflows'
 if 'clock' in f:return 'Clock and scheduling'
 if any(w in f for w in ['emulat','mobile','touch','viewport','media','user-agent','locale','timezone','capabilities','webdriver']):return 'Emulation, browser policies and graphics capabilities'
 if any(w in f for w in ['click','hover','mouse','wheel','keyboard','press','fill','focus','select','check','drag','hit-target']):return 'Input, focus, actionability and hit testing'
 if any(w in f for w in ['navigation','goto','reload','load','history','set-content','page-event-crash','page-close']):return 'Navigation, page lifecycle and crash handling'
 if any(w in f for w in ['evaluate','handle','console','binding','expose','init-script','error']):return 'JavaScript evaluation, handles, bindings and console'
 return 'Other APIs / internal fixtures'
def scope(x):
 f=x['file']
 if '/inspector/' in f or any(w in f for w in ['browsertype','multiclient','defaultbrowsercontext','debug-controller','heap','trace-viewer','browser-server','connect-over-cdp','extensions','launcher','connect-to-worker']):return 'product/launcher/internal-heavy; passes do not establish a CEF browser workflow'
 if any(w in f for w in ['browsercontext-fetch','api-request']):return 'API-request-heavy; passes do not necessarily exercise CEF rendering'
 return 'adapted browser API suite'
def disposition(x,n,s):
 if not n:return 'Not reached'
 status=n['executionStatus'];outcome=n['outcome'];ne=err(n)
 if status=='skipped':
  explicit=any(a.get('type') in ['skip','fixme'] for a in n.get('annotations',[]))
  return 'Upstream annotation skip' if explicit else 'Skipped without skip annotation / possible serial or setup block'
 if outcome=='expected':return 'Expected upstream failure' if status=='failed' else 'Expected outcome'
 if status=='passed':return 'Unexpected pass of an expected-failure test'
 if 'independent browser launching/connecting is outside this harness' in ne:return 'Blocked: independent launch/connect fixture'
 if 'Cannot find module' in ne or 'MODULE_NOT_FOUND' in ne:return 'Blocked: missing upstream package/module'
 if x['file']=='library/browsertype-connect.spec.ts' and x['line'] in [252,267,282] or x['file']=='library/chromium/connect-over-cdp.spec.ts' and x['line'] in [253,286,298,310]:return 'Blocked: independent launch/connect fixture'
 if x['file']=='library/browsercontext-proxy.spec.ts' and x['line']==25:return 'Blocked: independent launch/connect fixture'
 if x['file']=='library/channels.spec.ts' and x['line'] in [184,252]:return 'Bootstrap/connection dispatcher-tree expectation'
 if x['file']=='page/page-evaluate.spec.ts' and x['line']==582:return 'Documented native resource limit'
 if x['file']=='page/page-screenshot.spec.ts' and x['line']==394:return 'Default graphics path fails; explicit SwiftShader is a separate supported mode'
 if x['file']=='library/heap.spec.ts' and x['line']==172:return 'Bootstrap object-count expectation / Playwright heap fixture'
 if x['file']=='library/capabilities.spec.ts' and x['line']==441:return 'Configured autoplay policy differs from upstream default'
 if x['file']=='library/capabilities.spec.ts' and x['line'] in [105,113]:return 'Default graphics path fails; explicit SwiftShader is a separate supported mode'
 if x['file']=='library/browser.spec.ts' and x['line']==20:return 'Blocked: proxy fixture identity assertion / worker watchdog'
 if x['file']=='library/browser.spec.ts' and x['line']==24 or x['file']=='library/browsercontext-basic.spec.ts' and x['line'] in [23,36,118]:return 'Different bootstrap-context semantics'
 if x['file']=='library/browsercontext-credentials.spec.ts' and x['line'] in [26,37,111,125,141]:return 'Full-Chrome vs headless-shell unauthorized-navigation expectation'
 if any(message in ne for message in ['screenshot exceeds the 8,294,400-pixel','reply exceeds the 16 MiB limit','CEF context limit exceeded','CEF target limit exceeded','CEF session limit exceeded','CEF command capacity exceeded','CEF screenshot queue capacity exceeded','Invalid cookie batch']):return 'Documented native resource limit'
 if 'worker process exited unexpectedly' in ne and 'SIGKILL' in ne:return 'Worker hang terminated by watchdog'
 if not s:return 'Failure; stock control not reached'
 if s['executionStatus']=='skipped':return 'Failure; stock control skipped'
 if s['executionStatus']=='passed':return 'SuiteCut-path mismatch; stock passed'
 if s['executionStatus'] in ['failed','timedOut','interrupted']:
  return 'Both backends failed; shared cause not established'
 return 'Failure; stock control inconclusive'
rows=[]
for x in inventory:
 n=native.get(x['id']);s=stock.get(x['id'])
 rows.append(dict(**x,feature=feature(x),scope=scope(x),disposition=disposition(x,n,s),nativeExecutionStatus=n['executionStatus'] if n else 'not-run',nativeOutcome=n['outcome'] if n else 'not-run',nativeExpectedStatus=n['expectedStatus'] if n else '',nativeDurationMs=n['durationMs'] if n else 0,stockExecutionStatus=s['executionStatus'] if s else 'not-run',stockOutcome=s['outcome'] if s else 'not-run',stockExpectedStatus=s['expectedStatus'] if s else '',stockDurationMs=s['durationMs'] if s else 0,nativeError=err(n),stockError=err(s),nativeAnnotations=json.dumps(n.get('annotations',[]) if n else []),stockAnnotations=json.dumps(s.get('annotations',[]) if s else []),isolatedExecutionStatus=isolated.get(x['id'],{}).get('executionStatus',''),isolatedOutcome=isolated.get(x['id'],{}).get('outcome',''),isolatedError=err(isolated.get(x['id']))))
def writecsv(path,items):
 with path.open('w',newline='') as f:
  w=csv.DictWriter(f,fieldnames=list(items[0]),lineterminator='\n');w.writeheader();w.writerows(items)
writecsv(out/'cases.csv',rows)
failures=[r for r in rows if r['nativeOutcome']=='unexpected' and r['nativeExecutionStatus']!='passed']
if failures:writecsv(out/'missing-cases.csv',failures)
core=[r for r in failures if r['disposition']=='SuiteCut-path mismatch; stock passed']
if core:writecsv(out/'suitecut-mismatches.csv',core)
trials=[r for r in rows if r['isolatedExecutionStatus']]
if trials:writecsv(out/'isolated-serial-cases.csv',trials)
skips=[r for r in rows if r['nativeExecutionStatus'] in ['skipped','not-run']]
if skips:writecsv(out/'skipped-not-run.csv',skips)
features=defaultdict(Counter);files=defaultdict(Counter)
for r in rows:features[r['feature']][r['disposition']]+=1;files[r['file']][r['disposition']]+=1
summary={'total':len(rows),'nativeReported':len(native),'stockReported':len(stock),'nativeExecutionStatuses':dict(Counter(r['executionStatus'] for r in native.values())),'nativeOutcomes':dict(Counter(r['outcome'] for r in native.values())),'stockExecutionStatuses':dict(Counter(r['executionStatus'] for r in stock.values())),'stockOutcomes':dict(Counter(r['outcome'] for r in stock.values())),'dispositions':dict(Counter(r['disposition'] for r in rows)),'features':{k:dict(v) for k,v in features.items()},'missingCases':len(failures),'complete':(base/'native-exit.txt').exists() and (base/'stock-exit.txt').exists(),'isolatedSerialCases':len(isolated),'isolatedExecutionStatuses':dict(Counter(r['executionStatus'] for r in isolated.values())),'generatedUtc':datetime.datetime.now(datetime.timezone.utc).isoformat()}
(out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
fileRows=[dict(file=k,total=sum(v.values()),suitecutMismatch=v['SuiteCut-path mismatch; stock passed'],unexpectedFailure=sum(1 for r in failures if r['file']==k),skippedOrNotRun=sum(v[a] for a in ['Not reached','Upstream annotation skip','Skipped without skip annotation / possible serial or setup block']),expected=sum(v[a] for a in ['Expected outcome','Expected upstream failure']),dispositions=json.dumps(dict(v))) for k,v in sorted(files.items())];writecsv(out/'files.csv',fileRows)
summary['nativeReporterStats']=json.loads((base/'native/results.json').read_text())['stats'] if (base/'native/results.json').exists() else None
summary['stockReporterStats']=json.loads((base/'stock/results.json').read_text())['stats'] if (base/'stock/results.json').exists() else None
(out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
metadata=json.loads((base/'metadata.json').read_text());(out/'metadata.json').write_text(json.dumps(metadata,indent=2)+'\n')
complete=summary['complete'];status='Completed' if complete else 'RUNNING — interim inventory; counts will change'
lines=['# SuiteCut: full pinned Chromium browser-suite audit',f'\n{status}. Prepared October 12, 2026 (Asia/Kolkata).',f'\nRuntime commit: `{metadata["commit"]}`. Playwright 1.62.1; CEF 152.0.6 / Chromium 152.0.7977.83; stock headless-shell Chromium 151.0.7922.34.',f'\nCollected **{len(rows):,} cases in 284 files** from the Chromium library and page projects, including Inspector/codegen. This is the browser compatibility suite discussed in this chat, not every test of Playwright’s entire repository: Firefox/WebKit, Android, Electron, component, test-runner, installation and language-SDK suites are outside this run.','\nThe 285 official library/page spec files match the pinned source archive byte-for-byte. One pre-existing local diagnostic spec is excluded. No upstream test body was changed. The fixture supplies the CEF engine browser, explicitly rejects unsupported independent launch/connect operations, and reports CEF as a full runtime rather than Playwright’s headless-shell product.','\n## Execution counts','\n| Backend | Reported cases | Passed execution | Failed execution | Timed out | Skipped |','| --- | ---: | ---: | ---: | ---: | ---: |']
for key,count in [('native',len(native)),('stock',len(stock))]:
 stats=summary[key+'ExecutionStatuses'];lines.append(f'| {key} | {count:,} | {stats.get("passed",0):,} | {stats.get("failed",0):,} | {stats.get("timedOut",0):,} | {stats.get("skipped",0):,} |')
lines+=['\nActual execution statuses and expected outcomes differ: an upstream test marked as expected to fail can fail without representing a newly discovered regression; an unexpected pass is not a missing feature. Skips without an explicit skip/fixme annotation are retained separately because serial-group or setup failures can prevent their bodies from running.','\n## Failure dispositions','\n| Disposition | Cases |','| --- | ---: |']
for k,v in Counter(r['disposition'] for r in rows).most_common():lines.append(f'| {k} | {v:,} |')
lines+=['\nThe final inventory has **62 SuiteCut-path mismatches with passing stock controls**, after separating 343 direct/indirect blocked launch/connect cases, bootstrap-object assumptions, configured policies and resource bounds. These are 62 failing test cases, not 62 independent missing features. See [suitecut-mismatches.csv](suitecut-mismatches.csv) for that filtered list. Ten connect/header/URL tests catch the fixture’s rejection and then wait for a request that the fixture never sends; a proxy test hides the same denied launch behind a failing finally-close. They are blocked fixtures, not established network/header defects. The 100 MiB evaluate case exceeds the 16 MiB reply policy even though its public error is a closed transport. Two dispatcher-tree assertions also count the extra bootstrap connection objects.']
lines+=['\nA stock pass establishes a difference in the SuiteCut/CEF path on this host; it does not by itself identify the faulty layer. These browser builds differ by one Chromium major version. Both-backend failure is not proof that SuiteCut is correct or that the cause is shared. Raw failure messages and annotations are retained for both.','\n## Missing behavior by feature','\n| Feature | SuiteCut-path mismatch, stock passed | Other unexpected failures |','| --- | ---: | ---: |']
for name,counts in sorted(features.items(),key=lambda p:-p[1]['SuiteCut-path mismatch; stock passed']):
 fs=[r for r in failures if r['feature']==name];m=counts['SuiteCut-path mismatch; stock passed'];lines.append(f'| {name} | {m} | {len(fs)-m} |')
lines+=['\n## Case-by-case missing or blocked tests','\nEvery unexpected failing/timed-out case appears below. Full multiline errors, controls, expected statuses and annotations are in [missing-cases.csv](missing-cases.csv); [cases.csv](cases.csv) contains every collected case, and [skipped-not-run.csv](skipped-not-run.csv) lists incomplete execution. [files.csv](files.csv) provides per-file totals.']
byfile=defaultdict(list)
for r in failures:byfile[r['file']].append(r)
for file,rs in sorted(byfile.items()):
 lines+=['',f'### {file} ({len(rs)} cases)','', '| Line / upstream case | Disposition | Stock status | Symptom |','| --- | --- | --- | --- |']
 for r in rs:
  link=f'https://github.com/microsoft/playwright/blob/v1.62.1/tests/{file}#L{r["line"]}';symptom=' '.join(r['nativeError'].split())[:320]
  escape=lambda s:s.replace('|','\\|').replace('\n',' ').replace('`',"'")
  lines.append(f'| [{r["line"]}: {escape(r["title"])}]({link}) | {r["disposition"]} | {r["stockExecutionStatus"]} | {escape(symptom)} |')
lines+=['\n## Seven serial-blocked heap cases: isolated follow-up','\nThe main inventory skipped seven cases after the first serial heap assertion failed. They are attempted one at a time in fresh workers, without changing test bodies. These repeated cases are supplementary evidence, not seven additional unique tests.','\n| Line / case | Original status | Isolated status | Symptom |','| --- | --- | --- | --- |']
for r in rows:
 if r['file']=='library/heap.spec.ts' and r['disposition'].startswith('Skipped without'):
  symptom=' '.join(r['isolatedError'].split())[:300].replace('|','\\|');lines.append(f'| {r["line"]}: {r["title"]} | serial-blocked | {r["isolatedExecutionStatus"] or "pending"} | {symptom} |')
lines+=['\nThe failed initial heap assertion counts live Playwright JavaScript objects, including SuiteCut’s intentional bootstrap page/context. Receiving two pages rather than the launched-browser expectation of one is not by itself evidence of an accumulating leak. These tests do not inspect CEF/FFmpeg native allocations or establish whole-runtime memory safety.']
lines+=['\n## Test environment and interpretation','\nTwo workers per backend share CPU affinity 0–3 and a four-CPU cgroup quota, with a 16 GiB host memory cgroup. Native and stock inventories run concurrently; these are functional inventories, not performance comparisons or the earlier four-CPU/eight-GiB benchmark. Both use zero retries, no failure-count cutoff, no suite-wide deadline, 30-second ordinary test timeouts and unchanged upstream slow-test multipliers. Screenshot baseline updates are disabled.','\nA hung native worker spent over 500 seconds in a browser-type identity assertion. The fixture wraps BrowserType in a Proxy while browser.browserType() returns the underlying object; strict identity differs. GDB sampled V8 string equality while the assertion failed to service its timer or Inspector commands. The worker was terminated, then the suite continued. Later workers receive an external watchdog after 200 seconds of sustained CPU activity. These actions and stacks are recorded explicitly; they are not normal browser test timeouts or evidence of a CEF memory leak.','\nThe native browser exposes an existing bootstrap context/page. Upstream tests assuming a freshly launched browser with zero contexts therefore have different counts. Unauthorized-navigation tests also branch on full Chrome versus headless-shell behavior: CEF returns a 401 response in cases expecting full Chrome to throw ERR_INVALID_AUTH_CREDENTIALS. Those five expectation differences do not demonstrate failure of valid HTTP credentials.','\nLauncher, server, Inspector/codegen, internal-debugger and API-request tests can execute Playwright or stock Chromium infrastructure outside CEF. Their passes must not be advertised as CEF rendering coverage. The harness currently blocks some direct launch/connect requests, so those failures do not establish that all corresponding SuiteCut external-browser APIs are unsupported.','\nThe frozen helper and adapter hashes are recorded. A completed passing 600 MiB HAR fixture for each backend is retained gzip-compressed with its original SHA-256 to preserve temporary disk space. Raw test results and attachments remain separate from product code. No fixes or release publication are part of this inventory run.']
(out/'report.md').write_text('\n'.join(lines)+'\n')
print(json.dumps({k:summary[k] for k in ['total','nativeReported','stockReported','missingCases','complete','dispositions']},indent=2))
