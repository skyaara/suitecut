"""Save file hashes and create a verified, portable evidence archive.

Run after every benchmark process and report generator has finished. SDKs,
executables, caches, and browser profiles are intentionally outside this bundle.
"""
import hashlib, json, subprocess, tarfile
from datetime import datetime, timezone
from pathlib import Path

checkout=Path.cwd()
root=checkout/'benchmarks/obs-cef'
run=root/'2026-10-09'
media=checkout/'.suitecut/obs-cef-evidence/2026-10-09'
output=Path('/workspace/shared/suitecut-obs-evidence-2026-10-09.tar.gz')
output.parent.mkdir(parents=True,exist_ok=True)
for cache in root.rglob('__pycache__'):
    import shutil
    shutil.rmtree(cache)
source_paths=[checkout/'native/browser/browser.cc',checkout/'native/browser/README.md',checkout/'benchmarks/README.md']
patch=subprocess.run(['git','diff','--','native/browser/browser.cc','native/browser/README.md','benchmarks/README.md'],check=True,capture_output=True,text=True).stdout
(run/'source-changes.patch').write_text(patch)
tracked={}
for path in sorted([*root.rglob('*'),*media.rglob('*'),*source_paths]):
    if not path.is_file() or path.name in ('evidence-files.sha256','bundle.json'):continue
    relative=str(path.relative_to(checkout))
    tracked[relative]={'bytes':path.stat().st_size,'sha256':hashlib.sha256(path.read_bytes()).hexdigest()}
(run/'evidence-files.sha256').write_text(''.join(f"{v['sha256']}  {k}\n" for k,v in tracked.items()))
(run/'bundle.json').write_text(json.dumps({'savedAtUtc':datetime.now(timezone.utc).isoformat(),'checkoutBaseCommit':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'fileCount':len(tracked),'files':tracked},indent=2)+'\n')
with tarfile.open(output,'w:gz',compresslevel=6) as archive:
    for path in [root,media,*source_paths]:archive.add(path,arcname='suitecut/'+str(path.relative_to(checkout)))
with tarfile.open(output,'r:gz') as archive:
    names=set(archive.getnames())
    for path,details in tracked.items():
        member=archive.extractfile('suitecut/'+path)
        if member is None or hashlib.sha256(member.read()).hexdigest()!=details['sha256']:raise RuntimeError('Archive verification failed: '+path)
    assert 'suitecut/benchmarks/obs-cef/2026-10-09/report.html' in names
checksum=hashlib.sha256(output.read_bytes()).hexdigest()
output.with_suffix(output.suffix+'.sha256').write_text(f'{checksum}  {output.name}\n')
print(json.dumps({'archive':str(output),'bytes':output.stat().st_size,'sha256':checksum,'verifiedFiles':len(tracked)}))
