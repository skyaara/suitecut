"""Seal finalized artifacts, then verify every regular payload in the tar archive."""
import datetime,hashlib,json,pathlib,sys,tarfile
out=pathlib.Path(sys.argv[1]).resolve();archive=pathlib.Path(sys.argv[2]).resolve()
def digest(file):
 h=hashlib.sha256()
 with file.open('rb') as f:
  while chunk:=f.read(1024*1024):h.update(chunk)
 return h.hexdigest()
manifest=out/'checksums.sha256';files=sorted(p for p in out.rglob('*') if p.is_file() and p!=manifest and not p.is_symlink())
links=[{'path':str(p.relative_to(out)),'target':str(p.readlink())} for p in out.rglob('*') if p.is_symlink()]
expected={str(p.relative_to(out)):digest(p) for p in files}
manifest.write_text(''.join(f'{value}  {name}\n' for name,value in expected.items()))
expected['checksums.sha256']=digest(manifest)
archive.parent.mkdir(parents=True,exist_ok=True)
with tarfile.open(archive,'w:gz',compresslevel=3) as tar:tar.add(out,arcname='suitecut-publication-'+out.name)
seen={};prefix='suitecut-publication-'+out.name+'/'
with tarfile.open(archive,'r:gz') as tar:
 for member in tar:
  if not member.isfile():continue
  assert member.name.startswith(prefix),member.name
  name=member.name[len(prefix):];h=hashlib.sha256()
  with tar.extractfile(member) as stream:
   while chunk:=stream.read(1024*1024):h.update(chunk)
  seen[name]=h.hexdigest()
assert seen==expected,'Archive inventory or payload digest mismatch'
sha=digest(archive);sidecar=archive.with_suffix(archive.suffix+'.sha256');sidecar.write_text(f'{sha}  {archive.name}\n')
info={'sealedAt':datetime.datetime.now(datetime.timezone(datetime.timedelta(hours=5,minutes=30))).isoformat(),'archive':str(archive),'bytes':archive.stat().st_size,'sha256':sha,'manifestSha256':expected['checksums.sha256'],'regularPayloadFiles':len(files),'verifiedArchiveRegularFilesIncludingManifest':len(seen),'symlinks':links,'verification':'Every regular archive payload matched its finalized local SHA-256; inventory exactly matched, including checksum manifest'}
archive.with_suffix(archive.suffix+'.integrity.json').write_text(json.dumps(info,indent=2)+'\n');print(json.dumps(info))
