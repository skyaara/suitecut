"""Snapshot source/provenance, seal files and verify the compressed evidence archive."""
import datetime,hashlib,importlib.metadata,json,os,pathlib,platform,shutil,subprocess,sys,tarfile
root=pathlib.Path(__file__).resolve().parents[3];out=pathlib.Path(sys.argv[1]).resolve();archive=pathlib.Path(sys.argv[2]).resolve();assert not archive.exists(),'Never overwrite an existing sealed archive'
def digest(path):
 h=hashlib.sha256()
 with pathlib.Path(path).open('rb') as f:
  while b:=f.read(1024*1024):h.update(b)
 return h.hexdigest()
sources=[*sorted((root/'benchmarks/obs-cef/quietness-control').glob('*.py')),*sorted((root/'benchmarks/obs-cef/quietness-control').glob('*.mjs')),root/'benchmarks/publication/analyze.py',root/'benchmarks/publication/test_analyze.py',root/'benchmarks/publication/sample.py',root/'benchmarks/publication/fixture.html',root/'benchmarks/obs-cef/obs-client.mjs',root/'benchmarks/obs-cef/native-audio-profile/cdp.mjs',root/'benchmarks/obs-cef/native-audio-profile/fixture.mjs']
sources.extend(p for p in [root/'package.json',root/'package-lock.json',root/'pnpm-lock.yaml'] if p.is_file())
for src in sources:
 dest=out/'source'/src.relative_to(root);dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,dest)
# First two successful runtime processes were loaded before post-capture analysis
# was appended. The capture script itself was identical across all primary runs.
src=root/'benchmarks/obs-cef/quietness-control/runtime.py';text=src.read_text().split('\n# Analyze completed captures only after')[0];text=text.replace(" exit_code=controller.wait(timeout=int(os.environ.get('BENCH_DURATION_MS','120000'))/1000+180)"," sys.exit(controller.wait(timeout=int(os.environ.get('BENCH_DURATION_MS','120000'))/1000+180))")
(out/'source/runtime-first-two-successful-captures.py').write_text(text+'\n')
(out/'source/runtime-version-note.txt').write_text('The first two successful controls used the runtime before serial post-capture analysis was appended. The reconstructed version is saved next to this note. Both versions have identical capture setup, and analysis runs only after all capture processes exit. capture.mjs was identical across all seven primary cases. Rejected startup attempts used earlier setup/readiness code as explained in startup-attempts.\n')
base=root/'benchmarks/native-sync/2026-10-11-shared-clock';names=['720p60-record','720p60-stream','1080p30-record','1080p30-stream','4k30-record','4k30-stream','soak-1080p30-stream'];baseline=[]
for name in names:
 for filename in ['analysis.json','result.json','fixture.html']:
  src=base/'runs'/name/filename;dest=out/'baseline'/name/filename;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,dest);baseline.append({'source':str(src),'saved':str(dest.relative_to(out)),'sha256':digest(src)})
assets=[root/'.suitecut/obs-cef-profile/root/usr/bin/obs',root/'.suitecut/obs-cef-profile/root/usr/lib/x86_64-linux-gnu/obs-plugins/obs-browser.so',root/'.suitecut/obs-cef-profile/root/usr/lib/x86_64-linux-gnu/obs-plugins/obs-browser-page',root/'.suitecut/obs-cef-profile/root/usr/lib/x86_64-linux-gnu/obs-plugins/libcef.so',root/'.suitecut/native-build/suitecut-browser',pathlib.Path('/usr/bin/ffmpeg'),pathlib.Path('/usr/bin/ffprobe')]
prov={'createdAt':datetime.datetime.now(datetime.timezone.utc).isoformat(),'platform':platform.platform(),'pythonDependencies':{name:importlib.metadata.version(name) for name in ['numpy','scipy','matplotlib','psutil']},'python':sys.version,'cpuQuota':pathlib.Path('/sys/fs/cgroup/cpu.max').read_text().strip(),'hostMemoryLimit':pathlib.Path('/sys/fs/cgroup/memory.max').read_text().strip(),'requestedMemoryGiB':8,'enforcement':'sampled summed-RSS guard, not a kernel 8 GiB cap','binaryAssets':[{'path':str(p),'bytes':p.stat().st_size,'sha256':digest(p)} for p in assets if p.is_file()],'baselineCopies':baseline,'ffmpegVersion':subprocess.check_output(['/usr/bin/ffmpeg','-version'],text=True),'nodeVersion':subprocess.check_output(['node','--version'],text=True).strip(),'obsVersion':'32.2.0','obsCEF':'127.0.0-6533-fix-stutter-and-osr-extra-info.3042+g176b09c+chromium-127.0.6533.120','suiteCutChromium':'152.0.7977.83','transport':'Local RTMP receiver, no WAN/network-loss claim','experimentalDesign':'Serial separate record and stream; one fresh OBS trial per case versus prior sealed SuiteCut trial; not randomized/repeated; no trace or sanitizer in primary OBS captures'}
(out/'provenance.json').write_text(json.dumps(prov,indent=2)+'\n')
(out/'EVIDENCE-INDEX.md').write_text('# OBS CEF quiet-audio comparison evidence\n\nOpen report.html for plots and report.md for the method. comparison.csv contains both products. Each runs/<case> folder contains real media and decoded/measurement evidence. startup-attempts contains rejected setup attempts. baseline includes copies of SuiteCut analyses/results/fixtures, while its complete media/traces remain in suitecut-shared-clock-2026-10-11.tar.gz (SHA256 c5602868e63866a6e2adb61c8c31a5cb98f5b2398604efc4aa0588d5e24d4871). packet-controls independently checks AAC packet continuity for both products. source includes harness/fixture/decoder sources and runtime-version note. provenance.json pins binaries, versions and limitations. MANIFEST.sha256 covers regular payload files; sockets and unrelated symlinks are omitted; relocated raw media symlinks are dereferenced into full regular media payloads.\n')
files=[p for p in sorted(out.rglob('*')) if p.is_file() and (not p.is_symlink() or p.name=='receiver.flv') and p.name!='MANIFEST.sha256'];manifest=''.join(digest(p)+'  '+str(p.relative_to(out))+'\n' for p in files);(out/'MANIFEST.sha256').write_text(manifest)
archive.parent.mkdir(parents=True,exist_ok=True)
with tarfile.open(archive,'w:gz',compresslevel=6,dereference=True) as tar:
 for p in [*files,out/'MANIFEST.sha256']:tar.add(p,arcname=str(pathlib.Path(out.name)/p.relative_to(out)),recursive=False)
expected={str(pathlib.Path(out.name)/p.relative_to(out)):digest(p) for p in files};actual={}
with tarfile.open(archive,'r:gz') as tar:
 for member in tar:
  if not member.isfile() or member.name.endswith('/MANIFEST.sha256'):continue
  h=hashlib.sha256();stream=tar.extractfile(member)
  while b:=stream.read(1024*1024):h.update(b)
  actual[member.name]=h.hexdigest()
assert actual==expected,'Archive payload verification failed'
sha=digest(archive);(archive.with_name(archive.name+'.sha256')).write_text(sha+'  '+archive.name+'\n');metadata={'archive':str(archive),'bytes':archive.stat().st_size,'sha256':sha,'payloadFiles':len(files),'payloadBytes':sum(p.stat().st_size for p in files),'diskAndArchivePayloadHashesVerified':True,'createdAt':datetime.datetime.now(datetime.timezone.utc).isoformat()};archive.with_name(archive.name+'.verification.json').write_text(json.dumps(metadata,indent=2)+'\n');print(json.dumps(metadata),flush=True)
