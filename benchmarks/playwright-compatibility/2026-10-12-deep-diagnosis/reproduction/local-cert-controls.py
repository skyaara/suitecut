import os,subprocess,pathlib
base=pathlib.Path('/tmp/suitecut-deep-audit')
for backend in ['cef','chromium']:
 env={k:v for k,v in os.environ.items() if k.lower() not in ['http_proxy','https_proxy','all_proxy','no_proxy','npm_config_proxy','npm_config_https_proxy']}
 env['COMPAT_BACKEND']=backend
 p=subprocess.run(['python3',str(base/'direct.py'),'local-certs-'+backend,'library/client-certificates.spec.ts:450','library/client-certificates.spec.ts:802'],env=env,stdout=subprocess.PIPE,text=True)
 print(p.stdout,flush=True)
