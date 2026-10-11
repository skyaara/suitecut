"""Own display/audio dependencies; preserve sanitizer logs from every CEF process."""
import fcntl
import os
import pathlib
import select
import shlex
import subprocess
import sys
import time

root = pathlib.Path(__file__).resolve().parents[2]
out = pathlib.Path(os.environ['MEMORY_DIRECTORY']).resolve()
out.mkdir(parents=True, exist_ok=True)
state = root / '.suitecut/twitch-runtime'
state.mkdir(exist_ok=True)
deps = root / '.suitecut/obs-deps/root'
children = []
lock = (root / '.suitecut/thread-control.lock').open('a')
fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
env = dict(os.environ, PULSE_SERVER='unix:' + str(state / 'pulse.sock'),
           LD_LIBRARY_PATH=':'.join(str(deps / p) for p in [
               'usr/lib/x86_64-linux-gnu', 'usr/lib/x86_64-linux-gnu/pulseaudio',
               'usr/lib/pulse-17.0+dfsg1/modules']))
try:
    display = subprocess.Popen([str(deps / 'usr/bin/Xvfb'), '-displayfd', '1',
                                '-screen', '0', '1280x720x24', '-nolisten', 'tcp', '-ac'],
                               env=env, stdout=subprocess.PIPE, stderr=(out / 'xvfb.log').open('w'))
    children.append(display)
    if not select.select([display.stdout], [], [], 10)[0]:
        raise RuntimeError('Display startup timed out')
    number = display.stdout.readline().decode().strip()
    if not number.isdecimal():
        raise RuntimeError('Invalid display number')
    env['DISPLAY'] = ':' + number
    pulse = subprocess.Popen([str(deps / 'usr/bin/pulseaudio'), '--daemonize=no',
                              '--exit-idle-time=-1', '--use-pid-file=no',
                              '--dl-search-path=' + str(deps / 'usr/lib/pulse-17.0+dfsg1/modules'),
                              '-n', '--load=module-native-protocol-unix socket=' + str(state / 'pulse.sock') + ' auth-anonymous=1',
                              '--load=module-null-sink sink_name=memory rate=48000 channels=2'],
                             env=env, stdout=(out / 'pulse.log').open('w'), stderr=subprocess.STDOUT)
    children.append(pulse)
    time.sleep(2)
    if any(p.poll() is not None for p in children):
        raise RuntimeError('Display/audio dependency exited')
    wrapper = state / 'browser-wrapper'
    preload = os.environ.get('SUITECUT_BROWSER_PRELOAD')
    prefix = ('export LD_PRELOAD=' + shlex.quote(preload) + '\n') if preload else ''
    wrapper.write_text('#!/bin/sh\n' + prefix + 'exec ' + shlex.quote(os.environ['SUITECUT_RUNTIME_EXECUTABLE']) +
                       ' "$@" 2>>' + shlex.quote(str(out / 'browser-stderr.log')) + '\n')
    wrapper.chmod(0o700)
    env['SUITECUT_NATIVE_EXECUTABLE'] = str(wrapper)
    env['ASAN_OPTIONS'] = os.environ.get('ASAN_OPTIONS', 'detect_leaks=1:halt_on_error=1') + ':log_path=' + str(out / 'asan')
    env['UBSAN_OPTIONS'] = 'halt_on_error=1:print_stacktrace=1:log_path=' + str(out / 'ubsan')
    if sys.argv[1] == 'playwright':
        env['SUITECUT_ENGINE_E2E_DIRECTORY'] = str(out)
        command = ['pnpm', 'exec', 'playwright', 'test', '--config',
                   'benchmarks/native-engine/playwright.config.ts', *sys.argv[2:]]
    else:
        command = ['node', '--expose-gc', sys.argv[1], *sys.argv[2:]]
    result = subprocess.run(command, env=env, cwd=root, timeout=2400)
    sys.exit(result.returncode)
finally:
    for child in reversed(children):
        if child.poll() is None:
            child.terminate()
            try:
                child.wait(5)
            except subprocess.TimeoutExpired:
                child.kill()
                child.wait()
