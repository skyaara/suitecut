import json,os,pathlib,subprocess
root=pathlib.Path(__file__).resolve().parents[2];out=root/'benchmarks/publication/2026-10-10';summary=[]
for backend,mode in [('obs-cef','record'),('obs-external','record'),('ts-external','record'),('cpp-cef','record'),('ts-cef','stream'),('obs-cef','stream'),('obs-external','stream')]:
 directory=out/'smoke'/f'{backend}-{mode}'
 env=dict(os.environ,BENCH_BACKEND=backend,BENCH_MODE=mode,BENCH_DURATION_MS='6000',BENCH_DIRECTORY=str(directory))
 with (out/'logs'/f'smoke-{backend}-{mode}.log').open('w') as log:p=subprocess.run(['python3','benchmarks/publication/runtime.py'],env=env,cwd=root,stdout=log,stderr=subprocess.STDOUT,timeout=180)
 row={'backend':backend,'mode':mode,'returncode':p.returncode}
 if (directory/'result.json').exists():row['result']=json.loads((directory/'result.json').read_text())
 summary.append(row);(out/'smoke-summary.json').write_text(json.dumps(summary,indent=2))
 print(json.dumps({k:row[k] for k in ['backend','mode','returncode']}),flush=True)
