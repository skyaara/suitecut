"""Own an isolated display/audio server for native validation on this host."""
import fcntl
import os
import pathlib
import select
import shlex
import subprocess
import sys
import time

root = pathlib.Path(__file__).resolve().parents[2]
deps = root / '.suitecut/obs-deps/root'
runtime = root / '.suitecut/native-engine-runtime'
runtime.mkdir(parents=True, exist_ok=True)
lock = open(root / '.suitecut/thread-control.lock', 'w')
fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
env = {
    **os.environ,
    'PULSE_SERVER': 'unix:' + str(runtime / 'pulse.sock'),
    'LD_LIBRARY_PATH': ':'.join(str(deps / path) for path in [
        'usr/lib/x86_64-linux-gnu', 'usr/lib/x86_64-linux-gnu/pulseaudio',
        'usr/lib/pulse-17.0+dfsg1/modules',
    ]),
}
children = []
try:
    # Let Xvfb choose a free display; never delete another process's lock/socket.
    display = subprocess.Popen([
        str(deps / 'usr/bin/Xvfb'), '-displayfd', '1', '-screen', '0',
        '3840x2300x24', '-nolisten', 'tcp', '-ac',
    ], env=env, stdout=subprocess.PIPE, stderr=open(runtime / 'xvfb.log', 'w'))
    children.append(display)
    if not select.select([display.stdout], [], [], 10)[0]:
        raise RuntimeError('Xvfb readiness timed out')
    number = display.stdout.readline().decode().strip()
    if not number.isdecimal():
        raise RuntimeError('Xvfb failed; inspect its saved log')
    env['DISPLAY'] = ':' + number
    pulse = subprocess.Popen([
        str(deps / 'usr/bin/pulseaudio'), '--daemonize=no', '--exit-idle-time=-1',
        '--use-pid-file=no', '--dl-search-path=' + str(deps / 'usr/lib/pulse-17.0+dfsg1/modules'),
        '-n', '--load=module-native-protocol-unix socket=' + str(runtime / 'pulse.sock') + ' auth-anonymous=1',
        '--load=module-null-sink sink_name=benchmark rate=48000 channels=2',
    ], env=env, stdout=open(runtime / 'pulse.log', 'w'), stderr=subprocess.STDOUT)
    children.append(pulse)
    time.sleep(2)
    if any(child.poll() is not None for child in children):
        raise RuntimeError('A native validation dependency exited during startup')
    wrapper = runtime / 'browser-wrapper'
    executable = os.environ.get('SUITECUT_RUNTIME_EXECUTABLE', str(root / '.suitecut/native-build/suitecut-browser'))
    wrapper.write_text('#!/bin/sh\nexec ' + shlex.quote(executable)
                       + ' "$@" 2>>' + shlex.quote(str(runtime / 'browser-stderr.log')) + '\n')
    wrapper.chmod(0o700)
    env['SUITECUT_NATIVE_EXECUTABLE'] = str(wrapper)
    if len(sys.argv) > 1 and sys.argv[1] == 'playwright':
        args = ['pnpm', 'exec', 'playwright', 'test', '--config',
                'benchmarks/native-engine/playwright.config.ts', *sys.argv[2:]]
    else:
        args = ['node', '--expose-gc', str(root / sys.argv[1]), *sys.argv[2:]]
    result = subprocess.run(args, env=env, cwd=root, timeout=2400)
    sys.exit(result.returncode)
finally:
    for child in children:
        child.terminate()
    for child in children:
        try:
            child.wait(5)
        except subprocess.TimeoutExpired:
            child.kill()
            child.wait()
