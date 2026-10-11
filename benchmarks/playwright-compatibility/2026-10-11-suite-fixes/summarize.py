import csv, hashlib, json, pathlib, re
root=pathlib.Path('/tmp/suitecut-suite-pass')
out=pathlib.Path('/workspace/suitecut/benchmarks/playwright-compatibility/2026-10-11-suite-fixes')
rows=[];runs=[]
ansi=re.compile(r'\x1b\[[0-9;]*m')
for path in sorted([*root.glob('*/results.json'),*root.glob('*/playwright-results.json')]):
 d=json.loads(path.read_text()); run=path.parent.name
 runs.append({'run':run,'stats':d.get('stats'),'errors':d.get('errors',[])})
 def visit(s):
  for spec in s.get('specs',[]):
   for t in spec['tests']:
    results=t.get('results',[]);last=results[-1] if results else {}
    error=ansi.sub('',last.get('error',{}).get('message',''))
    category=''
    if t['status']=='unexpected':
     category='harness' if any(x in error for x in ['outside this harness','playwright-client/lib/index.mjs']) else 'unresolved'
    rows.append(dict(run=run,file=spec['file'],line=spec['line'],title=spec['title'],status=t['status'],execution=last.get('status','not-run'),durationMs=last.get('duration',0),category=category,error=error))
  for child in s.get('suites',[]):visit(child)
 visit(d)
with (out/'cases.csv').open('w') as f:
 w=csv.DictWriter(f,fieldnames=list(rows[0]),lineterminator='\n');w.writeheader();w.writerows(rows)
(out/'summary.json').write_text(json.dumps(runs,indent=2)+'\n')
verified={(r['file'],r['line'],r['title']) for r in rows if r['run']=='final-upstream' and r['status']=='expected'}
issues=[]
for row in rows:
 if row['run'] not in ['broad','pages','popup-trace'] or not row['category']:continue
 row=dict(row)
 if (row['file'],row['line'],row['title']) in verified: row['resolution']='fixed; final-upstream passed'
 elif row['category']=='harness':row['resolution']='outside adapter coverage'
 elif row['title']=='promise collected':row['resolution']='global budget interrupted this case; unverified'
 else:row['resolution']='unresolved or needs classification; see report'
 issues.append(row)
(out/'unresolved-cases.json').write_text(json.dumps([r for r in issues if not r['resolution'].startswith('fixed;')],indent=2)+'\n')
(out/'inventory-resolution.json').write_text(json.dumps(issues,indent=2)+'\n')
print(json.dumps([{ 'run':r['run'],**r['stats']} for r in runs],indent=2))
