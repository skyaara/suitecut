"""Launch an isolated official OBS+CEF runtime and one benchmark script."""
import fcntl
import json
import os
import pathlib
import signal
import subprocess
import sys
import time
import select

root = pathlib.Path(__file__).resolve().parents[3]
os.chdir(root)
out = pathlib.Path(os.environ['PROFILE_DIRECTORY']).resolve()
out.mkdir(parents=True, exist_ok=True)
vendor = root / '.suitecut/obs-cef-profile/root'
deps = root / '.suitecut/obs-deps/root'
state = root / '.suitecut/obs-cef-profile/runtime'
state.mkdir(exist_ok=True)
lock = (root / '.suitecut/thread-control.lock').open('a')
fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
libs = [vendor / 'usr/lib/x86_64-linux-gnu', deps / 'usr/lib/x86_64-linux-gnu',
        deps / 'usr/lib/x86_64-linux-gnu/pulseaudio', deps / 'usr/lib/pulse-17.0+dfsg1/modules']
env = dict(os.environ, LD_LIBRARY_PATH=':'.join(map(str, libs)),
           PATH=str(vendor / 'usr/bin') + ':' + os.environ['PATH'],
           PULSE_SERVER='unix:' + str(state / 'pulse.sock'),
           XDG_CONFIG_HOME=str(root / '.suitecut/obs-cef-profile/config'),
           QT_QPA_PLATFORM_PLUGIN_PATH=str(deps / 'usr/lib/x86_64-linux-gnu/qt6/plugins'),
           QT_QPA_PLATFORM='xcb')
# OBS's relative module path already resolves this extracted distribution.
# Adding it again through the environment loads every plugin twice.
env.pop('OBS_PLUGINS_PATH', None)
env.pop('OBS_PLUGINS_DATA_PATH', None)
env.pop('OBS_DATA_PATH', None)
# Clear only stale launch markers in this benchmark's private config.
sentinel = root / '.suitecut/obs-cef-profile/config/obs-studio/.sentinel'
stale = list(sentinel.glob('*'))
(out / 'prior-private-sentinels.json').write_text(json.dumps([{'name': p.name, 'content': p.read_text(errors='replace')} for p in stale], indent=2))
for marker in stale:
    marker.unlink()
children = []
try:
    display = subprocess.Popen([str(deps / 'usr/bin/Xvfb'), '-displayfd', '1', '-screen', '0',
                                '3840x2300x24', '-nolisten', 'tcp', '-ac'], env=env,
                               stdout=subprocess.PIPE, stderr=(out / 'xvfb.log').open('w'))
    children.append(display)
    if not select.select([display.stdout], [], [], 10)[0]:
        raise RuntimeError('Xvfb readiness timed out')
    number = display.stdout.readline().decode().strip()
    assert number.isdecimal(), 'Invalid Xvfb display'
    env['DISPLAY'] = ':' + number
    pulse = subprocess.Popen([str(deps / 'usr/bin/pulseaudio'), '--daemonize=no',
                              '--exit-idle-time=-1', '--use-pid-file=no',
                              '--dl-search-path=' + str(deps / 'usr/lib/pulse-17.0+dfsg1/modules'),
                              '-n', '--load=module-native-protocol-unix socket=' + str(state / 'pulse.sock') + ' auth-anonymous=1',
                              '--load=module-null-sink sink_name=benchmark rate=48000 channels=2'],
                             env=env, stdout=(out / 'pulse.log').open('w'), stderr=subprocess.STDOUT)
    children.append(pulse)
    time.sleep(2)
    obs_env = dict(env, OBS_AUDIO_TRACE_DIRECTORY=str(out))
    if os.environ.get('PROFILE_TAP', '1') == '1':
        obs_env['LD_PRELOAD'] = str(root / 'benchmarks/obs-cef/native-audio-profile/obs-audio-tap.so')
    obs = subprocess.Popen([str(vendor / 'usr/bin/obs'), '--disable-missing-files-check',
                            '--disable-updater', '--profile', 'AudioProfile', '--remote-debugging-port=9222'], env=obs_env,
                           cwd=vendor / 'usr/bin', stdout=(out / 'obs.log').open('w'), stderr=subprocess.STDOUT)
    children.append(obs)
    env.update(OBS_PID=str(obs.pid), XVFB_PID=str(display.pid), PULSE_PID=str(pulse.pid))
    (out / 'runtime.json').write_text(json.dumps({'obsPid': obs.pid, 'displayPid': display.pid,
                                                'pulsePid': pulse.pid, 'display': env['DISPLAY'],
                                                'browserCapture': 'OBS embedded CEF', 'tapEnabled':os.environ.get('PROFILE_TAP','1')=='1'}, indent=2))
    completed = subprocess.run(['node', '--expose-gc', sys.argv[1], *sys.argv[2:]],
                               env=env, cwd=root, timeout=2400)
    sys.exit(completed.returncode)
finally:
    for child in reversed(children):
        if child.poll() is None:
            child.terminate()
            try:
                child.wait(timeout=10)
            except subprocess.TimeoutExpired:
                child.kill()
                child.wait()
