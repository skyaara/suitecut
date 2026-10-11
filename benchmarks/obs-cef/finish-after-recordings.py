"""Wait for the recording driver, reload OBS settings, then run feature tests.

This keeps benchmark workloads serial and ensures the stream encoder settings
are loaded before streaming begins. Usage: python3 finish-after-recordings.py PID
"""
import json, os, subprocess, sys, time
from pathlib import Path
import psutil
import obsws_python

root=Path.cwd()
logs=root/'benchmarks/obs-cef/2026-10-09/logs'
try: psutil.Process(int(sys.argv[1])).wait(timeout=5400)
except psutil.NoSuchProcess: pass
rows=json.loads((root/'benchmarks/obs-cef/2026-10-09/trials.json').read_text())
if any(row['status'] in ('running','interrupted') for row in rows):
    # Incomplete pilot attempts are archived separately. Resume their trial IDs
    # once, retaining every completed pass/fail measurement.
    with (logs/'benchmark-complete-missing.log').open('w') as output:
        subprocess.run(['node','benchmarks/obs-cef/run.mjs'],env=dict(os.environ,BENCHMARK_RESUME='1'),stdout=output,stderr=subprocess.STDOUT,check=True)
old_obs=psutil.Process(int(os.environ['OBS_PID']))
old_obs.terminate()
try: old_obs.wait(timeout=15)
except psutil.TimeoutExpired: old_obs.kill()
profile=root/'.suitecut/obs-profile/obs-studio/basic/profiles/Benchmark'
stream={'rate_control':'CBR','bitrate':4500,'preset':'veryfast','profile':'high','keyint_sec':2}
(profile/'streamEncoder.json').write_text(json.dumps(stream))
(root/'benchmarks/obs-cef/2026-10-09/obs-stream-encoder.json').write_text(json.dumps(stream,indent=2)+'\n')
deps=root/'.suitecut/obs-deps/root'
env=dict(os.environ, XDG_CONFIG_HOME=str(root/'.suitecut/obs-profile'),
         QT_QPA_PLATFORM_PLUGIN_PATH=str(deps/'usr/lib/x86_64-linux-gnu/qt6/plugins'), QT_QPA_PLATFORM='xcb')
with (logs/'obs-console-features.log').open('w') as log:
    obs=subprocess.Popen([str(deps/'usr/bin/obs'),'--disable-missing-files-check','--disable-updater'],env=env,stdout=log,stderr=subprocess.STDOUT)
    try:
        connected=False
        for attempt in range(120):
            if obs.poll() is not None: raise RuntimeError('OBS exited during feature setup')
            try:
                client=obsws_python.ReqClient(host='127.0.0.1',port=4455,timeout=2)
                client.get_version()
                client.disconnect()
                connected=True
                break
            except Exception: time.sleep(.25)
        if not connected: raise RuntimeError('OBS websocket startup timed out')
        env['OBS_PID']=str(obs.pid)
        with (logs/'features-run.log').open('w') as output:
            subprocess.run(['node','benchmarks/obs-cef/features.mjs'],env=env,stdout=output,stderr=subprocess.STDOUT,check=True)
    finally:
        obs.terminate()
        try: obs.wait(timeout=15)
        except subprocess.TimeoutExpired: obs.kill()
