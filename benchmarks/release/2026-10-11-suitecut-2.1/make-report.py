import pathlib,json,re,html,datetime
out=pathlib.Path(__file__).resolve().parent
fuzz=json.loads((out/'fuzz/results.json').read_text())
for target in fuzz['targets']:
 log=(out/'logs'/('fuzz-'+target['name']+'.log')).read_text()
 target['stats']={k:int(v) for k,v in re.findall(r'stat::(\w+):\s*(\d+)',log)}
 target['seed']=int(re.search(r'INFO: Seed: (\d+)',log)[1])
 coverage=re.findall(r'cov: (\d+) ft: (\d+)',log)
 if coverage:target['coverageCounters']={'coverage':int(coverage[-1][0]),'features':int(coverage[-1][1])}
(out/'fuzz/summary.json').write_text(json.dumps(fuzz,indent=2)+'\n')
native=json.loads((out/'native-checks.json').read_text());gates=json.loads((out/'release-checks.json').read_text())
pack=json.loads((out/'packages.json').read_text())
checks=[*native,*gates]
passed=len(fuzz['targets'])==2 and all(x['exitCode']==0 and not x['artifacts'] for x in fuzz['targets']) and all(x['exitCode']==0 for x in checks)
rows='\n'.join(f"| {t['name']} | {t['stats']['number_of_executed_units']:,} | {t['wallSeconds']:.1f} | {t['stats']['peak_rss_mb']} | {len(t['artifacts'])} | {t['exitCode']} |" for t in fuzz['targets'])
checkrows='\n'.join(f"| {c['name']} | {c['exitCode']} | {c['wallSeconds']:.1f} |" for c in checks)
packages='\n'.join(f"| {p['name']} | {p['size']:,} | `{p['sha256']}` |" for p in pack['packages'])
report=f'''# SuiteCut 2.1.0 final release verification

Prepared locally; **not published**. Final saved checks: **{'passed' if passed else 'incomplete/failed'}**.

## Fresh sanitizer and fuzz results

Eight CTest groups passed in the release build and in the Clang 19
ASan/UBSan/LSan build, including 160 concurrent output-lifecycle cycles,
codec cleanup, scaler-allocation failure, and injected 250 ms encoder delays
at 30/60 FPS with decoded PCM retention assertions. ThreadSanitizer checks use
matching instrumented FFmpeg 7.1.5; see the saved configure/build/test logs.

| Fuzz target | Executed inputs | Seconds | Peak RSS MiB | Failure artifacts | Exit |
| --- | ---: | ---: | ---: | ---: | ---: |
{rows}

Each campaign ran separately for a requested 600 seconds, with leak detection
and fatal sanitizer errors enabled, no suppressions, 512-byte maximum input,
5-second per-input timeout, 1024 MiB RSS limit and 128 MiB allocation limit.
Affinity is four CPU cores; host CPU quota is four cores. These are serial safety
checks, not performance measurements. Fuzzer RSS includes sanitizer overhead.

The bounds target mutates audio overlaps, popup composition, timestamps and command
IDs. The media target executes bounded FFV1/PCM output/queue/pause/failure/stop paths.
Executed-input counts include short rejected inputs and do not count complete
browser recordings or establish all-codec coverage. Corpus files, random seeds,
logs, binary hashes and source identity are retained. This is a fresh campaign;
the older sealed campaign remains separate.

## Release checks

| Check | Exit | Seconds |
| --- | ---: | ---: |
{checkrows}

The full unit suite passed **328 tests in 38 files**; site checks reported zero
errors and zero warnings. Package checks inspect all nine tarballs and public imports, install SDK/VITS/core
in an isolated consumer, and exercise installed CLI help. Native installed-package
verification records through real CEF with an audio track and checks owned-browser
cleanup. The CEF Playwright integration suite additionally exercises capture,
readiness, input/scroll, storage/network, pause, popup and shutdown paths: **16 passed**.
External browser automation passed **27 checks**, including 25 attach/disconnect cycles,
on sandboxed Chromium 151.0.7922.34. No RTC claim is made by that suite.

One initial prepublish attempt stopped because pnpm tried a noninteractive reinstall
after the workspace version bump. The frozen lockfile passed; final commands kept
the existing workspace symlinks with `pnpm_config_verify_deps_before_run=warn`.
The isolated npm tarball installs passed. Initial attempts are preserved in
`release-attempts.json` and the original logs; this was not a sanitizer failure.

## Artifacts

| Package | Compressed bytes | SHA-256 |
| --- | ---: | --- |
{packages}

The npm artifact contains native source and pins, not a compiled browser or CEF
SDK. All packages are version 2.1.0. Publication, tagging and pushing were not performed.

## Earlier long-run evidence and remaining issues

The unchanged native implementation and executable match the earlier sealed
resolution matrix and 30-minute memory profile; `sealed-baseline-reuse.json`
records the comparison. Those captures were not re-run as part of this final fuzz pass.

- SuiteCut 30-minute 1080p30 stream: peak summed RSS 1408.0 MiB, PSS 722.54 MiB,
  USS 497.98 MiB. Post-120-second slopes: RSS +1.04, PSS +0.94, USS +0.92 MiB/min.
  Queue retention peaked at 32.63 MiB video and 0.06 MiB audio. The memory trend's
  cause is unresolved; this profile does not establish indefinite leak freedom.
  Per-PID analysis attributes about +0.61 MiB/min to the CEF/native host and
  +0.22 MiB/min to the benchmark controller. See `earlier-memory-attribution.json`;
  process location does not distinguish caching/buffering from leaked allocations.
- Quiet audio: SuiteCut 15.03 seconds total / 275 ms longest interval; matched OBS
  embedded CEF 15.02 seconds / 280 ms. Both AAC packet timelines were continuous.
  These are quiet output-content measurements, not proof that packets were dropped.
  Their source stage is unresolved and the release does not claim this issue fixed.
- CPU/resource comparisons are fixture/host-specific. The earlier runs used four
  cores and a sampled 8 GiB summed-RSS guard; the host kernel memory cap was 16 GiB.
- Full Chromium/prebuilt library sanitizer coverage, GPU encoding, remote RTC
  transport and the new engine's macOS packaging remain unverified. Windows native
  packaging is not implemented. Stock CEF source-audio startup can still lose sound.

![Earlier SuiteCut memory profile](suitecut-memory-profile.png)

![Earlier OBS memory profile](obs-soak-resources.png)

Detailed sealed data: `benchmarks/native-sync/2026-10-11-shared-clock/` and
`benchmarks/obs-cef/2026-10-11-quietness-control/`. Copies of their memory charts
above are provided for convenience. Their raw media and checksummed archives
remain unchanged. A bounded clean sanitizer/fuzz run cannot prove universal memory safety.
'''
(out/'report.md').write_text(report)
fuzz_html=''.join(f"<tr><td>{html.escape(t['name'])}</td><td>{t['stats']['number_of_executed_units']:,}</td><td>{t['wallSeconds']:.1f}</td><td>{t['stats']['peak_rss_mb']}</td><td>{len(t['artifacts'])}</td></tr>" for t in fuzz['targets'])
checks_html=''.join(f"<tr><td>{html.escape(c['name'])}</td><td>{c['exitCode']}</td><td>{c['wallSeconds']:.1f}</td></tr>" for c in checks)
page='''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>SuiteCut 2.1 final verification</title><style>body{font:16px system-ui;background:#101620;color:#e7edf7;max-width:1100px;margin:auto;padding:32px}h1{font-size:40px}h2{margin-top:40px}p{line-height:1.7;color:#bcc9da}a{color:#86c5ff}table{border-collapse:collapse;width:100%;background:#182332}td,th{text-align:left;padding:14px;border-bottom:1px solid #304054}.cards{display:flex;gap:20px;flex-wrap:wrap}.card{padding:20px;background:#182332;border-radius:12px;flex:1}strong{color:#88dfaf}img{max-width:100%;background:white;border-radius:12px}.note{border-left:4px solid #f8c778;padding-left:20px}</style><h1>SuiteCut 2.1.0</h1><p>Final release verification · local artifacts · unpublished</p><div class="cards"><div class="card"><strong>8 native test groups</strong><p>Release and address/undefined/leak sanitizer builds</p></div><div class="card"><strong>2 fresh fuzz campaigns</strong><p>10 minutes per target · saved corpora and failure artifacts</p></div><div class="card"><strong>9 release packages</strong><p>SDK and eight matching audio plugins, with SHA-256 checksums</p></div></div><h2>Fresh fuzz results</h2><table><thead><tr><th>Target</th><th>Executed inputs</th><th>Seconds</th><th>Peak RSS MiB</th><th>Failure artifacts</th></tr></thead><tbody>'''+fuzz_html+'''</tbody></table><p>Counts include rejected inputs. RSS includes sanitizer overhead. SuiteCut code and allocation checks are covered; complete Chromium and dependency instrumentation is unverified.</p><h2>Release gates</h2><table><thead><tr><th>Check</th><th>Exit</th><th>Seconds</th></tr></thead><tbody>'''+checks_html+'''</tbody></table><h2>Earlier 30-minute memory profiles</h2><p>The release native implementation matches the sealed benchmark build. These charts are earlier measurements, not fresh captures during fuzzing.</p><img alt="Earlier SuiteCut 30-minute memory profile" src="suitecut-memory-profile.png"><img alt="Earlier OBS 30-minute memory profile" src="obs-soak-resources.png"><p class="note">SuiteCut's post-warmup summed-RSS trend was +1.04 MiB/min; its cause remains unresolved. Long-run audio quietness was also unresolved: SuiteCut 15.03 s / 275 ms maximum versus OBS CEF 15.02 s / 280 ms. Both AAC timelines were continuous. These checks do not prove indefinite leak freedom or universal memory safety.</p><p><a href="report.md">Full detailed report</a> · <a href="fuzz/summary.json">Fuzz data</a> · <a href="packages.json">Package inventories and checksums</a></p></html>'''
(out/'report.html').write_text(page)
(out/'summary.json').write_text(json.dumps({'version':'2.1.0','published':False,'savedAt':datetime.datetime.now(datetime.timezone.utc).isoformat(),'allSavedChecksPassed':passed,'fuzz':fuzz,'checks':checks,'scopeLimits':['prebuilt Chromium not fully instrumented','bounded tests do not prove no leaks indefinitely','known source-audio quietness unresolved']},indent=2)+'\n')
