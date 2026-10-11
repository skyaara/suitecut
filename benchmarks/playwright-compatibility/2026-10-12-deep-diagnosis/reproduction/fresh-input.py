import os,subprocess,json,pathlib
base=pathlib.Path('/tmp/suitecut-deep-audit');results=[]
for i in range(20):
 env=dict(os.environ,SC_INPUT_ITERATIONS='1',SC_CONTROL_SUFFIX='-fresh-'+str(i))
 p=subprocess.run(['python3',str(base/'run-cef-control.py'),'input'],env=env,stdout=subprocess.PIPE,text=True)
 rows=[json.loads(line) for line in (base/('standalone-input-fresh-'+str(i))/'results.json').read_text().splitlines() if line.startswith('{')]
 results.append({'run':i,'rows':rows,'runnerExit':p.returncode});print(i,rows[-1] if rows else p.stdout,flush=True)
(base/'fresh-input-results.json').write_text(json.dumps(results,indent=2))
