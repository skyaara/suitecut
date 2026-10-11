"""Read-only verification of earlier benchmark payload manifests."""
import datetime,hashlib,json,pathlib
root=pathlib.Path(__file__).resolve().parents[2];out=root/'benchmarks/publication/2026-10-10';rows=[];cache={}
def digest(path):
 stat=path.stat();key=(str(path),stat.st_size,stat.st_mtime_ns)
 if key not in cache:
  h=hashlib.sha256()
  with path.open('rb') as f:
   while chunk:=f.read(1024*1024):h.update(chunk)
  cache[key]=h.hexdigest()
 return cache[key]
for manifest in sorted((root/'benchmarks').rglob('*.sha256')):
 if out in manifest.parents:continue
 if not (manifest.name.endswith('checksums.sha256') or manifest.name=='evidence-files.sha256'):continue
 failures=[];count=0
 for line in manifest.read_text().splitlines():
  if not line:continue
  expected,name=line.split('  ',1);count+=1;path=root/name
  if not path.exists():path=manifest.parent/name
  if not path.exists():failures.append({'path':name,'error':'missing'});continue
  actual=digest(path)
  if actual!=expected:failures.append({'path':name,'expected':expected,'actual':actual})
 rows.append({'manifest':str(manifest.relative_to(root)),'payloadEntries':count,'passed':not failures,'failures':failures})
result={'verifiedAt':datetime.datetime.now(datetime.timezone(datetime.timedelta(hours=5,minutes=30))).isoformat(),'manifestCount':len(rows),'payloadEntries':sum(x['payloadEntries'] for x in rows),'uniqueVerifiedPaths':len(cache),'passed':all(x['passed'] for x in rows),'manifests':rows}
(out/'prior-evidence-verification.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:v for k,v in result.items() if k!='manifests'}))
