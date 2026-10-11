"""Save a separate immutable OBS control bundle and verify all payload hashes."""
import hashlib,json,subprocess,tarfile
from pathlib import Path
from datetime import datetime,timezone
ROOT=Path.cwd();OUT=ROOT/'benchmarks/obs-cef/2026-10-09-obs-long-control';MEDIA=ROOT/'.suitecut/obs-long-control/2026-10-09/media';ARCHIVE=Path('/workspace/shared/suitecut-obs-long-control-evidence-2026-10-09.tar.gz')
def sha(path):
 h=hashlib.sha256()
 with path.open('rb') as f:
  for block in iter(lambda:f.read(1048576),b''):h.update(block)
 return h.hexdigest()
result=json.loads((OUT/'result.json').read_text());driver=json.loads((OUT/'driver.json').read_text());validation=json.loads((OUT/'media-validation.json').read_text())
assert result.get('completedAt') and result['activeMs']>=result['requestedMs']*.99
assert driver['runtimeStopped'] and driver['exclusiveRuntimeLock']
assert 'obsStatus' in validation and (OUT/'report.md').exists();assert not ARCHIVE.exists(),'Preserve previous saved archives'
paths=[OUT,MEDIA,ROOT/'benchmarks/obs-cef/obs-long-soak.mjs',ROOT/'benchmarks/obs-cef/run-obs-long-control.py',ROOT/'benchmarks/obs-cef/profile-obs-long.py',ROOT/'benchmarks/obs-cef/analyze-obs-long.py',ROOT/'benchmarks/obs-cef/report-obs-long.py',ROOT/'benchmarks/obs-cef/save-obs-long.py',ROOT/'benchmarks/obs-cef/start-runtime.sh',ROOT/'benchmarks/obs-cef/fixture.html',ROOT/'benchmarks/obs-cef/obs-client.mjs',ROOT/'benchmarks/obs-cef/analyze-sync.py',ROOT/'benchmarks/obs-cef/README.md',ROOT/'benchmarks/obs-cef/2026-10-09-optimization/soak/result.json',ROOT/'benchmarks/obs-cef/2026-10-09-optimization/environment.json',ROOT/'package.json',ROOT/'pnpm-lock.yaml',ROOT/'LICENSE',ROOT/'THIRD_PARTY_NOTICES.md']
paths += [ROOT/'benchmarks/obs-cef/setup.sh',ROOT/'benchmarks/obs-cef/setup-obs.py']
files={}
for path in paths:
 for file in sorted(path.rglob('*')) if path.is_dir() else [path]:
  if not file.is_file() or '__pycache__' in file.parts or file.name in ['bundle.json','evidence-files.sha256']:continue
  files[str(file.relative_to(ROOT))]={'bytes':file.stat().st_size,'sha256':sha(file)}
(OUT/'bundle.json').write_text(json.dumps({'savedAtUtc':datetime.now(timezone.utc).isoformat(),'baseCommit':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'fileCount':len(files),'obsMediaStatus':validation['obsStatus'],'suitecutReferenceMediaStatus':validation['suitecutStatus'],'dependencies':'CEF/browser/OBS/FFmpeg libraries are excluded; versions/setup recorded. SuiteCut reference media/PCM are preserved in the prior optimization archive.','files':files},indent=2))
(OUT/'evidence-files.sha256').write_text(''.join(f"{row['sha256']}  {name}\n" for name,row in sorted(files.items())))
with tarfile.open(ARCHIVE,'w:gz',compresslevel=5) as tar:
 for name in sorted(files):tar.add(ROOT/name,arcname='suitecut/'+name,recursive=False)
 for name in ['bundle.json','evidence-files.sha256']:tar.add(OUT/name,arcname='suitecut/'+str((OUT/name).relative_to(ROOT)),recursive=False)
# Stream order avoids repeated backward gzip seeks while validating every file.
with tarfile.open(ARCHIVE,'r|gz') as tar:
 found=set()
 for member in tar:
  name=member.name.removeprefix('suitecut/')
  if name not in files:continue
  stream=tar.extractfile(member);assert stream is not None;h=hashlib.sha256()
  for block in iter(lambda:stream.read(1048576),b''):h.update(block)
  assert h.hexdigest()==files[name]['sha256'],name;found.add(name)
 assert found==set(files)
checksum=sha(ARCHIVE);ARCHIVE.with_suffix(ARCHIVE.suffix+'.sha256').write_text(f'{checksum}  {ARCHIVE.name}\n')
print(json.dumps({'archive':str(ARCHIVE),'bytes':ARCHIVE.stat().st_size,'sha256':checksum,'verifiedFiles':len(files)}))
