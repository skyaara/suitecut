import pathlib,os,json,subprocess,time,urllib.request,sys
base=pathlib.Path('/tmp/suitecut-deep-audit');out=base/('standalone-control' if len(sys.argv)==1 else 'standalone-'+sys.argv[1]+os.environ.get('SC_CONTROL_SUFFIX',''));out.mkdir(exist_ok=True)
env=dict(os.environ,**json.load(open(base/'runtime-env.json')))
profile=base/'standalone-profile';profile.mkdir(exist_ok=True)
with (out/'browser.log').open('w') as log:
 p=subprocess.Popen(['/tmp/suitecut-cef-full-regression-build/cef-control','--no-sandbox','--disable-gpu','--disable-popup-blocking','--profile='+str(profile),'--remote-debugging-port=19523'],env=env,stdout=log,stderr=subprocess.STDOUT)
 try:
  for i in range(100):
   try:
    urllib.request.urlopen('http://127.0.0.1:19523/json/version',timeout=.5);break
   except Exception:time.sleep(.1)
  with (out/'results.json').open('w') as result:
   r=subprocess.run(['node',str(base/('cef-control.mjs' if len(sys.argv)==1 else {'raw':'raw-network.mjs','frame-race':'raw-frame-race.mjs','input':'oopif-input.mjs'}[sys.argv[1]]))],env=env,stdout=result,stderr=(out/'node.log').open('w'),timeout=40)
  print('Control',r.returncode)
 finally:
  p.terminate()
  try:p.wait(5)
  except subprocess.TimeoutExpired:p.kill();p.wait()
