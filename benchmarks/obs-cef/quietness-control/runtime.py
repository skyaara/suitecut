"""Isolated, owned display/Pulse/OBS processes; no changes to existing profiles."""
import configparser,fcntl,hashlib,json,os,pathlib,select,shutil,signal,subprocess,sys,time
root=pathlib.Path(__file__).resolve().parents[3];os.chdir(root)
out=pathlib.Path(os.environ['BENCH_DIRECTORY']).resolve();out.mkdir(parents=True,exist_ok=True)
state=out/'runtime';state.mkdir(exist_ok=True)
lock=(root/'.suitecut/thread-control.lock').open('a');fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
cpus=sorted(os.sched_getaffinity(0))[:4];os.sched_setaffinity(0,cpus)
deps=root/'.suitecut/obs-deps/root';vendor=root/'.suitecut/obs-cef-profile/root'
libs=[vendor/'usr/lib/x86_64-linux-gnu',deps/'usr/lib/x86_64-linux-gnu',deps/'usr/lib/x86_64-linux-gnu/pulseaudio',deps/'usr/lib/pulse-17.0+dfsg1/modules']
env=dict(os.environ,LD_LIBRARY_PATH=':'.join(map(str,libs)),PULSE_SERVER='unix:'+str(state/'pulse.sock'),PULSE_SINK='publication',QT_QPA_PLATFORM='xcb',QT_QPA_PLATFORM_PLUGIN_PATH=str(deps/'usr/lib/x86_64-linux-gnu/qt6/plugins'))
for key in ['OBS_PLUGINS_PATH','OBS_PLUGINS_DATA_PATH','OBS_DATA_PATH','LD_PRELOAD']:env.pop(key,None)
children=[]
width=int(os.environ.get('BENCH_WIDTH','1920'));height=int(os.environ.get('BENCH_HEIGHT','1080'));fps=int(os.environ.get('BENCH_FPS','30'))
def interrupted(signum,frame): raise SystemExit(128+signum)
signal.signal(signal.SIGTERM,interrupted)
try:
 display=subprocess.Popen([str(deps/'usr/bin/Xvfb'),'-displayfd','1','-screen','0',f'{width}x{height}x24','-nolisten','tcp','-ac'],env=env,stdout=subprocess.PIPE,stderr=(out/'xvfb.log').open('w'));children.append(display)
 if not select.select([display.stdout],[],[],10)[0]:raise RuntimeError('Display readiness timeout')
 number=display.stdout.readline().decode().strip();assert number.isdecimal();env['DISPLAY']=':'+number
 pulse=subprocess.Popen([str(deps/'usr/bin/pulseaudio'),'--daemonize=no','--exit-idle-time=-1','--use-pid-file=no','--dl-search-path='+str(deps/'usr/lib/pulse-17.0+dfsg1/modules'),'-n','--load=module-native-protocol-unix socket='+str(state/'pulse.sock')+' auth-anonymous=1','--load=module-null-sink sink_name=publication rate=48000 channels=2'],env=env,stdout=(out/'pulse.log').open('w'),stderr=subprocess.STDOUT);children.append(pulse)
 time.sleep(1)
 if any(p.poll() is not None for p in children):raise RuntimeError('Display/audio runtime exited')
 env.update(XVFB_PID=str(display.pid),PULSE_PID=str(pulse.pid))
 if os.environ['BENCH_BACKEND'].startswith('obs-'):
  config=state/'config';shutil.copytree(root/'.suitecut/obs-cef-profile/config',config)
  for marker in (config/'obs-studio/.sentinel').glob('*'):marker.unlink()
  profile=config/'obs-studio/basic/profiles/AudioProfile'
  basic=configparser.ConfigParser();basic.optionxform=str;basic.read(profile/'basic.ini')
  for key,value in dict(BaseCX=width,BaseCY=height,OutputCX=width,OutputCY=height,FPSType=2,FPSNum=fps,FPSDen=1,FPSCommon=fps,FPSInt=fps).items():basic['Video'][key]=str(value)
  with (profile/'basic.ini').open('w') as f:basic.write(f,space_around_delimiters=False)
  for name in ['recordEncoder.json','streamEncoder.json']:
   encoder=json.loads((profile/name).read_text());encoder.update(preset='ultrafast' if width>=3840 else 'veryfast',bitrate=12000 if width>=3840 else 4500 if width>=1920 else 3500,x264opts='threads='+str(3 if width>=3840 else 1)+' bframes=0')
   (profile/name).write_text(json.dumps(encoder,indent=2))
  env['XDG_CONFIG_HOME']=str(config)
  scenes=config/'obs-studio/basic/scenes/Untitled.json'
  data=json.loads(scenes.read_text());scene=next(item for item in data['sources'] if item['id']=='scene');scene['name']='Publication benchmark';scene['settings']['items']=[]
  data.update(current_scene='Publication benchmark',current_program_scene='Publication benchmark',scene_order=[{'name':'Publication benchmark'}],sources=[scene],groups=[])
  scenes.write_text(json.dumps(data,indent=2))
  ini=config/'obs-studio/global.ini';text=ini.read_text();text=text.replace(str(root/'.suitecut/obs-cef-profile/config'),str(config));ini.write_text(text)
  obs=subprocess.Popen([str(vendor/'usr/bin/obs'),'--disable-missing-files-check','--disable-updater','--profile','AudioProfile','--remote-debugging-port=9222'],env=env,cwd=vendor/'usr/bin',stdout=(out/'obs.log').open('w'),stderr=subprocess.STDOUT);children.append(obs);env['OBS_PID']=str(obs.pid)
 runtime={'cpus':cpus,'cpuQuota':(pathlib.Path('/sys/fs/cgroup/cpu.max')).read_text().strip(),'hostMemoryLimitBytes':int(pathlib.Path('/sys/fs/cgroup/memory.max').read_text()),'memoryGuardBytes':8*1024**3,'memoryGuardKind':'sampled summed-RSS guard, NOT a kernel 8 GiB cgroup limit','display':env['DISPLAY'],'displaySize':[width,height],'infrastructurePids':[display.pid,pulse.pid],'obsPid':env.get('OBS_PID'),'instrumentation':'no preload, no audio trace, no sanitizer'}
 (out/'runtime.json').write_text(json.dumps(runtime,indent=2)+'\n')
 command=['node','--expose-gc',str(root/'benchmarks/obs-cef/quietness-control/capture.mjs')]
 controller=subprocess.Popen(command,env=env,cwd=root,start_new_session=True);children.append(controller)
 exit_code=controller.wait(timeout=int(os.environ.get('BENCH_DURATION_MS','120000'))/1000+180)
finally:
 for child in reversed(children):
  if child is locals().get("controller"):
   try:os.killpg(child.pid,signal.SIGTERM)
   except ProcessLookupError:pass
   try:child.wait(8)
   except subprocess.TimeoutExpired:os.killpg(child.pid,signal.SIGKILL);child.wait()
   try:os.killpg(child.pid,signal.SIGKILL)
   except ProcessLookupError:pass
  elif child.poll() is None:
   child.terminate()
   try:child.wait(8)
   except subprocess.TimeoutExpired:child.kill();child.wait()

# Analyze completed captures only after OBS/display/audio have exited; parent
# waits here before starting the next capture, so decoding never overlaps.
pending=[]
for p in sorted(out.parent.glob('*/result.json')):
 try:
  r=json.loads(p.read_text())
  if r['status']=='completed; media validation pending' and not (p.parent/'analysis.json').exists():pending.append(str(p.parent))
 except (OSError,ValueError):pass
if pending:
 analysis=subprocess.run(['python3',str(root/'benchmarks/obs-cef/quietness-control/analyze.py'),*pending],cwd=root)
 if analysis.returncode:exit_code=analysis.returncode
sys.exit(exit_code)
