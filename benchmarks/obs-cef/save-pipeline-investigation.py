"""Package the completed investigation without editing either earlier evidence bundle."""
import hashlib,json,tarfile
from datetime import datetime,timezone
from pathlib import Path
ROOT=Path('/workspace/suitecut');OUT=ROOT/'benchmarks/obs-cef/2026-10-09-dropout-investigation';SHARED=Path('/workspace/shared')
assert (OUT/'clean-30-minute/media-validation.json').exists(),'Finish full media validation before packaging'
assert json.loads((OUT/'clean-30-minute/driver.json').read_text())['runtimeStopped']
sources=OUT/'tested-sources';sources.mkdir(exist_ok=False)
paths=[ROOT/'native/browser/browser.cc',ROOT/'native/browser/README.md',ROOT/'native/browser/CMakeLists.txt',ROOT/'package.json',ROOT/'pnpm-lock.yaml',ROOT/'LICENSE',*sorted((ROOT/'src').glob('native*.ts')),ROOT/'src/live-stream.ts',ROOT/'src/live-audio.ts',ROOT/'src/media.ts']
paths.extend(p for p in (ROOT/'native/browser').iterdir() if p.is_file())
paths.append(ROOT/'benchmarks/obs-cef/README.md')
paths.extend(ROOT/'tests'/name for name in ['native-protocol.test.ts','native-browser.test.ts','native-recorder.test.ts','live-audio.test.ts'])
for name in ['investigate-soak.mjs','trace-stdio.mjs','profile-pipeline.py','run-pipeline-investigation.py','analyze-pipeline-investigation.py','summarize-pipeline-investigation.py','pipeline-progress.py','save-pipeline-investigation.py','report-pipeline-investigation.py','fixture.html','analyze-sync.py']:
 paths.append(ROOT/'benchmarks/obs-cef'/name)
for path in paths:
 dest=sources/path.relative_to(ROOT);dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(path.read_bytes())
reference=[('suitecut-optimization-evidence-2026-10-09.tar.gz','4cf71211ad969e81555fca5c7a67ddf8ca24b734dc79f11777f0d6d67fc7779b'),('suitecut-obs-long-control-evidence-2026-10-09.tar.gz','d8efc3831b85d5bd5279f7eab771f37cd9de9c728e72ad0deddf589a34db55f1')]
preserved=[]
for name,expected in reference:
 path=SHARED/name;actual=hashlib.file_digest(path.open('rb'),'sha256').hexdigest();assert actual==expected;preserved.append({'path':str(path),'sha256':actual,'unchanged':True})
(OUT/'reference-preservation.json').write_text(json.dumps(preserved,indent=2))
files=[p for p in OUT.rglob('*') if p.is_file()]
for label in ['controlled-node-stall','clean-30-minute']:
 r=json.loads((OUT/label/'result.json').read_text())
 for key in ['recordingPath','receiverPath']:files.append(Path(r[key]))
checks=[]
for p in sorted(set(files)):
 checks.append((str(p.relative_to(ROOT)),hashlib.file_digest(p.open('rb'),'sha256').hexdigest()))
manifest=OUT/'evidence-files.sha256';manifest.write_text(''.join(f'{digest}  {name}\n' for name,digest in checks));files.append(manifest)
archive=SHARED/'suitecut-dropout-investigation-evidence-2026-10-09.tar.gz';assert not archive.exists(),'Preserve existing evidence: choose a new archive name'
with tarfile.open(archive,'w:gz',compresslevel=3) as tar:
 for p in sorted(set(files)):tar.add(p,arcname=str(p.relative_to(ROOT)),recursive=False)
digest=hashlib.file_digest(archive.open('rb'),'sha256').hexdigest();archive.with_suffix(archive.suffix+'.sha256').write_text(f'{digest}  {archive.name}\n')
expected=dict(checks);verified=0
with tarfile.open(archive,'r|gz') as tar:
 for entry in tar:
  if entry.name in expected:
   assert hashlib.file_digest(tar.extractfile(entry),'sha256').hexdigest()==expected[entry.name],entry.name;verified+=1
assert verified==len(expected)
metadata={'completedAt':datetime.now(timezone.utc).isoformat(),'archivePath':str(archive),'archiveSha256':digest,'bytes':archive.stat().st_size,'verifiedPayloadFiles':verified,'reportPath':str(OUT/'report.md'),'referenceArchivesUnchanged':True,'verified':True}
archive.with_suffix('.metadata.json').write_text(json.dumps(metadata,indent=2));print(json.dumps(metadata))
