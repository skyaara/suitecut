import csv,json,re,pathlib,collections,hashlib
repo=pathlib.Path('/workspace/suitecut');base=pathlib.Path('/tmp/suitecut-deep-audit');old=repo/'benchmarks/playwright-compatibility/2026-10-12-upstream-audit';out=repo/'benchmarks/playwright-compatibility/2026-10-12-deep-diagnosis';out.mkdir(exist_ok=True)
rows=list(csv.DictReader(open(old/'missing-cases.csv')))+list(csv.DictReader(open(old/'isolated-serial-cases.csv')))
assert len(rows)==438 and len({r['id'] for r in rows})==438
fixed={
 'library/browsercontext-page-event.spec.ts:160':('SuiteCut tab ownership','Windowless modifier navigations had no owner. Native creates an independent tab in the same request context, holds its URL until automation initialization, and tracks pending creation across shutdown.','tabs-v1; final-confirmed'),
 'library/browsercontext-page-event.spec.ts:173':('SuiteCut tab ownership','Same native modifier-tab ownership and initialization correction as Shift-click.','tabs-v1; final-confirmed'),
 'library/inspector/cli-codegen-2.spec.ts:339':('SuiteCut tab ownership','Codegen missed the modifier-created tab; native tab creation now exposes its target and URL.','codegen-confirmed (5/5)'),
 'library/chromium/oopif.spec.ts:353':('CEF detach ordering / driver integration','CEF may reply with a missing child session before Playwright retires that session. The explicit CEF driver patch treats that precise detached-child exception as closed; main-frame and other errors still propagate.','oopif-repeated (19/20 before); oopif-patched (30/30); final-confirmed'),
 'library/download.spec.ts:316':('SuiteCut download ownership','Initial popup download events used an unknown target/frame before Page.getFrameTree arrived. Native tracks the main frame and attributes an early download to its live opener.','download-baseline-repeat (11/20 before); download-delay-before; download-delay-after (5/5); final-confirmed'),
 'library/trace-viewer.spec.ts:2435':('SuiteCut JSON serialization','CefDictionary/CefWriteJSON sorted user properties and DOM attribute arrays reconstructed by Playwright. Forward original CDP JSON, rewriting routing metadata only.','trace-raw; final-confirmed'),
 'library/tracing.spec.ts:716':('SuiteCut harness artifact configuration','Upstream internal tracing expects the worker artifact directory. Expose artifactsDir on launchEngine/connectCEF and configure that worker directory; tracing itself was not absent.','trace-raw; final-confirmed'),
 'page/page-request-continue.spec.ts:590':('CEF 152 / stock Chromium 151 network difference','Raw standalone CEF retained body headers on POST-to-GET redirects after interception. CEF-scoped driver correction strips body headers only when redirect method changes to GET/HEAD. Same-method and 307 tests remain intact.','standalone-raw-network; stock-raw-network.json; redirect-fixed (39/39); final-confirmed'),
 'library/heap.spec.ts:304':('SuiteCut command admission','2,000 matched elements submit several thousand small adoption commands. The old 1,024 command cap closed transport. Raise the bounded count to 8,192; retain 16 concurrent commands and 64 MiB byte limit.','bulk-diagnostic; bulk-admission; final-heap-304'),
}
for n in [42,49,74,82,92,101]: fixed[f'library/page-event-crash.spec.ts:{n}']=('SuiteCut crash failure boundary','An automation renderer crash terminated the whole helper and lost the crash event. Emit one page crash, fail its recording/pending commands, and preserve sibling targets. Primary control-source crashes remain fatal.','crash-bulk-v2; final-confirmed (3 repetitions); engine-final')
framekeys={f'library/hit-target.spec.ts:{n}' for n in [366,398,422]}|{'library/inspector/pause.spec.ts:207'}|{f'page/expect-boolean.spec.ts:{n}' for n in [379,391,680]}|{'page/locator-misc-2.spec.ts:171','page/locator-query.spec.ts:243','page/page-mouse.spec.ts:341'}|{f'page/page-aria-snapshot-ai.spec.ts:{n}' for n in [70,85,175]}|{f'page/page-aria-snapshot.spec.ts:{n}' for n in [699,737]}|{f'page/page-click.spec.ts:{n}' for n in [986,1008,1030,1090,1112]}
assert len(framekeys)==20
runs={}
valid=['baseline','final-selected','final-confirmed','codegen-confirmed','redirect-fixed','bulk-admission','oopif-repeated','oopif-patched','download-baseline-repeat','download-delay-before','download-delay-after','browser-bootstrap-final','browser-client-built-cef','local-certs-cef','local-certs-chromium','stdin-isolated-cef','stdin-isolated-stock','shared-followups-stock']+[f'final-heap-{n}' for n in [172,178,184,191,223,244,270,304]]
ansi=re.compile(r'\x1b\[[0-9;]*m')
def flatten(s):
 for sp in s.get('specs',[]):
  for t in sp['tests']:
   for rr in t.get('results',[]):yield sp,t,rr
 for ch in s.get('suites',[]):yield from flatten(ch)
observations=collections.defaultdict(list)
for name in valid:
 p=base/name/'results.json'
 if not p.exists():continue
 data=json.load(open(p));counts=collections.Counter()
 for s in data['suites']:
  for sp,t,rr in flatten(s):
   counts[rr['status']]+=1
   if name=='shared-followups-stock' and sp['file']!='library/playwright-client.spec.ts':continue
   observations[(sp['file'],str(sp['line']),sp['title'])].append({'run':name,'backend':'stock' if name in ['local-certs-chromium','stdin-isolated-stock','shared-followups-stock'] else 'native','executionStatus':rr['status'],'outcome':t['status'],'durationMs':rr['duration'],'error':ansi.sub('',rr.get('error',{}).get('message',''))})
 runs[name]={'executionCounts':dict(counts),'exit':(base/name/'exit.txt').read_text().strip() if (base/name/'exit.txt').exists() else None,'durationMs':data['stats']['duration']}
for r in rows:
 key=f"{r['file']}:{r['line']}";d=r['disposition'];decision='Unresolved';confidence='Source/fixture evidence';evidence='original full audit; baseline/final-selected where listed';nextstep=''
 if key in fixed:
  cause,diagnosis,evidence=fixed[key];decision='Resolved by runtime/adapter/configuration fix';confidence='Targeted reproduction and passing regression';nextstep='Use the updated native helper and explicitly installed pinned driver patch.'
 elif key in framekeys:
  cause='CEF blank-origin srcdoc renderer crash family';diagnosis='These bodies construct srcdoc frame trees on an initial blank-origin document. Minimal standalone CEF reproduced the origin-calculation renderer crash in windowless and windowed modes. Individual cases share the trigger; not every case has an independently captured crash stack.';confidence='Independent CEF reproduction; family attribution';evidence='previous final-bridge-fixes/standalone-cef; final-selected';nextstep='Validate a CEF/Chromium runtime fix or a new runtime build. Do not rewrite document origins or silently replace srcdoc content.'
 elif key=='page/page-event-request.spec.ts:198':
  cause='CEF iframe-abort renderer crash';diagnosis='Raw CDP writes HTTP iframes on an initial blank document, pauses requests, then changes frames to about:blank. Standalone CEF reports renderer status=2 exit=5 without SuiteCut or Playwright. A missing requestfailed assertion is secondary to that crash.';confidence='Independent raw-CDP standalone CEF reproduction';evidence='standalone-frame-race';nextstep='Reduce further against matching stock Chromium 152 / source-debug CEF; fix runtime crash before requestfailed semantics.'
 elif key in ['library/permissions.spec.ts:222','library/permissions.spec.ts:317']:
  cause='SuiteCut–CEF permission-controller integration';diagnosis=('Adding the CEF storage-access bit/content setting left permissions.query on the initial blank page at prompt. That experiment was reverted.' if key.endswith(':222') else 'Applying the public screen-wake-lock content setting terminated the helper during navigation. That experiment was reverted; no exact native assertion was established.')+' CEF exposes relevant APIs, but their presence is not complete Playwright override support. Synthetic query answers or granting the wrong browser profile would hide the defect.';confidence='Failed bounded experiments; architecture requirement established, complete controller solution not established';evidence='storage-setting; wake-setting; saved experimental sources';nextstep='Implement and validate context-scoped permission query/request/reset integration, including blank and cross-origin pages; consider a narrowly scoped CEF hook if public APIs cannot supply it.'
 elif key=='page/page-evaluate.spec.ts:894':
  cause='CEF/V8 DevTools promise-collection difference';diagnosis='The never-resolving evaluate promise stays pending after 20 GC requests in standalone CEF, while stock Chromium 151 reports promise collection. SuiteCut has no safe evidence to invent that error or cancel an otherwise valid pending promise. Builds differ by one Chromium major.';confidence='Independent CEF and stock controls; version/runtime attribution not isolated';evidence='standalone-control; stock-control; final-selected';nextstep='Compare matching Chromium 152 and instrument DevTools awaitPromise ownership. This observation alone does not establish a memory leak.'
 elif key=='page/page-set-input-files.spec.ts:38':
  cause='CEF directory-upload embedding path';diagnosis='webkitdirectory setInputFiles times out in standalone CEF using direct browser CDP attachment and the same Playwright client; stock accepts the real folder and relative paths. Ordinary file upload works. Exact directory enumeration/confirmation failure branch is not established.';confidence='Independent CEF reproduction; exact completion path unresolved';evidence='standalone-control; stock-control; implementation source excerpts; final-selected';nextstep='Trace directory selection/confirmation through Chromium FileSelectHelper and CEF dialog handling; provide supported native completion rather than fabricating FileList values.'
 elif d=='SuiteCut-path mismatch; stock passed':
  cause='SuiteCut–CEF early-popup lifecycle architecture';diagnosis='Popup first-script, first request, init script, binding, emulation or event behavior can run before the page-agent attachment completes. Some reruns pass; intermittent success is not a fix. Modifiers and early download ownership are separately corrected. Standalone browser-root popup attempts timed out and are inconclusive.';confidence='SuiteCut event/request timing evidence; inherent CEF limitation not proven';evidence='baseline; final-selected; preceding bridge investigation';nextstep='Establish a paused target before its first script/request via an earlier native/browser-level attachment hook. If unavailable publicly, validate a narrow CEF integration change. Do not replay scripts/events or add arbitrary delays just to pass.'
 elif d=='Blocked: independent launch/connect fixture':
  cause='Upstream independent-launch/connection fixture unavailable';decision='Outside this harness; feature body not established';diagnosis='The fixture explicitly rejects browserType launch, launchPersistentContext, launchServer, connect and connectOverCDP. This test directly or indirectly depends on those operations; its failure/timeout is not proof of the named web-platform defect.';nextstep='Add a distinct native launcher/persistent/server fixture only when that product capability is intended. External connectBrowser exists but does not replace Playwright launchServer or a CEF multi-client endpoint.'
 elif 'bootstrap' in d.lower() or 'dispatcher-tree' in d or (r['file']=='library/heap.spec.ts' and r['line'] in ['178','184','191']):
  cause='Existing CDP attachment/bootstrap semantics';decision='Documented architecture difference';diagnosis='The engine exposes an existing primary page/default context and associated dispatcher objects. The assertion expects an independently launched empty browser or a different connection tree. Static extra objects are not a measured growing leak.';nextstep='Keep the public primary-page behavior; use lifecycle growth/cleanup tests for leak investigations. Redesign bootstrap exposure only as an explicit API change.'
 elif d.startswith('Full-Chrome'):
  cause='CEF full-runtime / stock headless-shell HTTP 401 policy';decision='Documented behavior difference';diagnosis='A rejected or missing HTTP credential returns a real 401 response with native prompts denied; upstream full-Chrome expectations seek an ERR_INVALID_AUTH_CREDENTIALS navigation error. Successful and rejected credential behavior is independently checked.';nextstep='Document real response semantics; do not turn every 401 into a fake protocol failure.'
 elif d=='Documented native resource limit':
  cause='Intentional bounded capture/IPC policy';decision='Retained documented limit';diagnosis='This body exceeds the existing 8,294,400 physical screenshot pixels / 16,384 axis or 16 MiB encoded DevTools reply cap (100 MiB evaluate result). Capacity failure is deliberate.';nextstep='Use bounded screenshots/response sizes. Retain the limit; do not remove memory bounds to match this case.'
 elif d.startswith('Default graphics'):
  cause='Host default graphics availability';decision='Documented host/configuration difference';diagnosis='Default rendering does not provide the expected WebGL pixels/capability on this CPU-only host. Explicit SwiftShader has separate real rendering tests; unsafe software graphics is deliberately opt-in.';nextstep='Test with a working GPU or explicitly selected SwiftShader; preserve explicit graphics configuration.'
 elif d.startswith('Configured autoplay'):
  cause='Intentional automatic audio capture autoplay policy';decision='Retained documented policy';diagnosis='Upstream expects a suspended AudioContext; SuiteCut configures automatic playback for browser-source recording, so the context runs.';nextstep='Retain and document automatic playback; do not weaken audio readiness to pass a default-Chrome policy assertion.'
 elif d.startswith('Blocked: proxy fixture'):
  cause='Proxy fixture identity / assertion inspection watchdog';decision='Harness identity failure';diagnosis='browser.browserType returns the real Chromium BrowserType, while the fixture variable is a Proxy wrapping it. Strict identity cannot match. The assertion inspection caused a worker watchdog; this does not establish a browser-engine leak.';nextstep='Avoid that proxy-identity comparison in adapted harness reporting; do not change browserType identity in the SDK.'
 elif key in ['library/client-certificates.spec.ts:450','library/client-certificates.spec.ts:802']:
  cause='Inherited HTTP proxy / NO_PROXY test environment';decision='Resolved harness environment';confidence='Unchanged upstream bodies pass both backends in isolated local environment';diagnosis='Inherited proxy configuration bypassed the fixture proxy for loopback or routed local.playwright through the cloud proxy. Removing inherited proxy variables only for these local fixture controls makes both upstream bodies pass.';evidence='local-certs-cef (2/2); local-certs-chromium (2/2)';nextstep='Isolate local certificate-test environment; retain deployment proxy configuration for real network tests.'
 elif key=='library/playwright-client.spec.ts:26':
  cause='Missing generated upstream browser-client bundle';decision='Resolved harness build artifact';confidence='Unchanged body passes native and stock after official bundle build';diagnosis='The pinned source archive does not contain packages/playwright-client/lib/index.mjs. Build that bundle with the upstream browser esbuild recipe. Native and stock then both drive the same browser from an in-page client. One concurrent attempt hit a fixed-port collision and is excluded.';evidence='build-client.cjs; browser-client-built-cef; shared-followups-stock';nextstep='Build the browser client in future full-audit staging; no SuiteCut runtime workaround needed.'
 elif key=='library/browsertype-connect.spec.ts:1132':
  cause='Remote launchServer/connect fixture / shared control timeout';decision='Outside native harness; stock timeout unresolved';diagnosis='The body reconnects 12 times to an independently launched remote browser, which the native fixture denies. Stock also timed out in the full run; that stock-side cause is not established.';nextstep='Treat native outcome as unexercised remote-server product workflow. Investigate the stock remote fixture separately; do not change native web-page semantics.'
 elif key=='library/trace-viewer.spec.ts:2539':
  cause='Shared upstream trace-viewer stdin workflow';diagnosis='The trace viewer receives a path over stdin but shows no action titles; stock and native runs fail, including independent reruns. The exact cause in the published Playwright CLI/assets or upstream fixture is not yet established. It is not evidence of a CEF-only tracing defect.';confidence='Both-backend reproduction, root cause unresolved';evidence='stdin-isolated-cef; stdin-isolated-stock; shared followups';nextstep='Debug the trace viewer stdin transport/CLI and matching generated assets separately; retain other passing trace capture/viewer checks.'
 elif r['file']=='library/heap.spec.ts' and r['line'] in ['223','244','270']:
  cause='Originally blocked serial heap case';decision='Passed isolated follow-up; no new fix claimed';diagnosis='This body was skipped after a preceding serial heap assertion. Isolated execution checks worker/handle/frame cleanup and passes. Preserve the original full-run skip.';confidence='Independent isolated execution';evidence=f"final-heap-{r['line']}";nextstep='Retain the passing isolated lifecycle regression; do not convert the historical skip into a historical pass.'
 else:raise RuntimeError((key,d))
 r.update(deepCause=cause,deepDecision=decision,deepConfidence=confidence,deepDiagnosis=diagnosis,deepNextStep=nextstep,deepEvidence=evidence)
 r['observations']=observations.get((r['file'],r['line'],r['title']),[])
 nativeObs=[o for o in r['observations'] if o['backend']=='native'];stockObs=[o for o in r['observations'] if o['backend']=='stock']
 r['latestTargetedStatus']=nativeObs[-1]['executionStatus'] if nativeObs else 'not rerun in this session'
 r['latestStockTargetedStatus']=stockObs[-1]['executionStatus'] if stockObs else 'not rerun in this session'
 src=pathlib.Path('/tmp/suitecut-playwright-upstream/playwright-1.62.1/tests')/r['file'];lines=src.read_text().splitlines();start=int(r['line'])-1;end=min(start+35,len(lines));indent=len(lines[start])-len(lines[start].lstrip())
 for j in range(start+1,end):
  if re.match(r'^\s*(?:it|test)(?:\.only|\.skip)?\(',lines[j]) and len(lines[j])-len(lines[j].lstrip())<=indent:end=j;break
 r['upstreamExcerpt']='\n'.join(f'{i+1}: {lines[i]}' for i in range(start,end))
 r['upstreamUrl']=f"https://github.com/microsoft/playwright/blob/v1.62.1/tests/{r['file']}#L{r['line']}"
 r['sourceAPIs']=','.join(sorted(set(re.findall(r'\b(?:browserType|browser|context|page)\.(launchPersistentContext|launchServer|launch|connectOverCDP|connect|newContext|newPage|setInputFiles|screenshot|evaluate|route|exposeFunction|grantPermissions)\b',r['upstreamExcerpt']))))
json.dump(rows,open(out/'cases.json','w'),indent=2)
cols=[k for k in rows[0] if k not in ['observations','upstreamExcerpt']]
with open(out/'cases.csv','w',newline='') as f:
 w=csv.DictWriter(f,cols,extrasaction='ignore',lineterminator='\n');w.writeheader();w.writerows(rows)
counts=collections.Counter(r['deepDecision'] for r in rows);causes=collections.Counter(r['deepCause'] for r in rows)
json.dump({'originalUnexpected':431,'originalSerialBlocked':7,'reviewedUniqueCases':438,'decisionCounts':dict(counts),'causeCounts':dict(causes),'runs':runs},open(out/'summary.json','w'),indent=2)
md=['# Detailed case decisions','', 'Every original unexpected case and every serial-blocked heap case is retained below. Original failure and stock-control details are in cases.csv/cases.json. A diagnosis applies to the failing condition reached; launcher-blocked tests have not established their later feature assertions.','']
for cause in sorted(causes):
 md+=['## '+cause,'','| Upstream case | Decision | Latest targeted execution | Evidence |','| --- | --- | --- | --- |']
 for r in rows:
  if r['deepCause']==cause:
   label=f"{r['file']}:{r['line']} — {r['title']}".replace('|','\\|')
   md.append(f"| [{label}]({r['upstreamUrl']}) | {r['deepDecision']} | {r['latestTargetedStatus']} | {r['deepEvidence']} |")
 first=next(r for r in rows if r['deepCause']==cause)
 md+=['',first['deepDiagnosis'],'', '**Next step:** '+first['deepNextStep'],'']
(out/'case-decisions.md').write_text('\n'.join(md))
print(json.dumps({'decisions':dict(counts),'causes':dict(causes)},indent=2))
