"""Archive immutable completed optimization evidence and verify each file hash."""
import hashlib,json,subprocess,tarfile
from pathlib import Path
from datetime import datetime,timezone
root=Path.cwd();out=root/'benchmarks/obs-cef/2026-10-09-optimization';media=root/'.suitecut/obs-cef-optimization';archive=Path('/workspace/shared/suitecut-optimization-evidence-2026-10-09.tar.gz')
def sha(path):
 digest=hashlib.sha256()
 with path.open('rb') as stream:
  for part in iter(lambda:stream.read(1024**2),b''):digest.update(part)
 return digest.hexdigest()
soak=json.loads((out/'soak/result.json').read_text())
assert soak.get('completedAt') and soak.get('activeMs',0)>=soak['requestedMs']*.99,'Do not archive an incomplete workload; preserve failed media results explicitly'
assert (out/'validation.json').exists(),'Evidence validation must record all observed outcomes'
patch=subprocess.check_output(['git','diff','--','src','tests','native/browser','benchmarks/README.md'],text=True);(out/'source-changes.patch').write_text(patch)
source_files=[root/p for p in subprocess.check_output(['git','diff','--name-only'],text=True).splitlines() if not p.startswith('benchmarks/obs-cef/')]+[root/'src/native-video-container.ts',root/'tests/native-page.test.ts']
source_files=list(dict.fromkeys(source_files))
paths=[out,media,root/'dist',root/'.suitecut/profile-baseline-dist',root/'.suitecut/profile-before-lifecycle-fix-dist',root/'.suitecut/native-build/suitecut-browser',root/'.suitecut/native-recording-verification',root/'.suitecut/native-stream-verification',root/'.suitecut/native-verification',root/'.suitecut/native-build/CEF-LICENSE.txt',root/'.suitecut/native-build/CEF-CREDITS.html',root/'.suitecut/native-build/libyuv-LICENSE.txt',root/'LICENSE',root/'THIRD_PARTY_NOTICES.md',*source_files,*sorted((root/'benchmarks/obs-cef').glob('*.mjs')),*sorted((root/'benchmarks/obs-cef').glob('*.py')),*sorted((root/'benchmarks/obs-cef').glob('*.sh')),root/'benchmarks/obs-cef/fixture.html',root/'benchmarks/obs-cef/README.md',root/'package.json',root/'pnpm-lock.yaml']
paths += [root/'native/browser',root/'README.md']
files={}
for path in paths:
 for file in sorted(path.rglob('*')) if path.is_dir() else [path]:
  if not file.is_file() or '__pycache__' in file.parts or file.name in ['bundle.json','evidence-files.sha256']:continue
  relative=str(file.relative_to(root));files[relative]={'bytes':file.stat().st_size,'sha256':sha(file)}
(out/'evidence-files.sha256').write_text(''.join(f"{row['sha256']}  {path}\n" for path,row in sorted(files.items())))
(out/'bundle.json').write_text(json.dumps({'savedAtUtc':datetime.now(timezone.utc).isoformat(),'baseCommit':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'fileCount':len(files),'files':files,'nativeRuntimeDependencies':'CEF/libyuv SDK and system/OBS dependencies are excluded; pinned versions/checksums and setup are documented.'},indent=2))
archive.parent.mkdir(parents=True,exist_ok=True)
with tarfile.open(archive,'w:gz',compresslevel=5) as tar:
 for relative in sorted(files):tar.add(root/relative,arcname='suitecut/'+relative,recursive=False)
 for name in ['bundle.json','evidence-files.sha256']:tar.add(out/name,arcname='suitecut/'+str((out/name).relative_to(root)))
with tarfile.open(archive,'r:gz') as tar:
 for relative,row in files.items():
  stream=tar.extractfile('suitecut/'+relative);digest=hashlib.sha256()
  assert stream is not None
  for chunk in iter(lambda:stream.read(1024**2),b''):digest.update(chunk)
  assert digest.hexdigest()==row['sha256'],relative
checksum=sha(archive);archive.with_suffix(archive.suffix+'.sha256').write_text(f'{checksum}  {archive.name}\n')
print(json.dumps({'archive':str(archive),'bytes':archive.stat().st_size,'sha256':checksum,'verifiedFiles':len(files)}))
