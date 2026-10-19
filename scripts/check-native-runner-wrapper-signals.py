# SPDX-License-Identifier: MIT
"""Real OS signal metadata through the thin Node CLI, without fake subprocess replies."""
import json
import argparse
import os
from pathlib import Path
import shlex
import shutil
import signal
import subprocess
import time

ROOT = Path(__file__).resolve().parent
parser=argparse.ArgumentParser()
parser.add_argument('--cli-script',type=Path,default=ROOT/'dist/native-runner-cli.js')
parser.add_argument('--directory',type=Path,default=ROOT/'wrapper-evidence')
parser.add_argument('--node',default=shutil.which('node'))
args=parser.parse_args()
rows = []
for signum in [signal.SIGINT, signal.SIGTERM, signal.SIGKILL]:
    folder = args.directory.resolve() / signal.Signals(signum).name
    folder.mkdir(parents=True, exist_ok=True)
    marker = folder / 'actual-native-child'
    marker.unlink(missing_ok=True)
    fault = folder / 'native-fault'
    fault.write_text("#!/bin/sh\nprintf '%s' \"$$\" > " + shlex.quote(str(marker)) + "\nexec /bin/sleep 30\n")
    fault.chmod(0o700)
    child = subprocess.Popen([args.node, str(args.cli_script.resolve()), '--executable', str(fault), '--suite', str(folder / 'unused-suite.mjs'), '--report', str(folder / 'unused-report.json')], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    until = time.monotonic() + 5
    native_pid = None
    while time.monotonic() < until:
        assert child.poll() is None, 'Actual thin wrapper exited before native signal probe'
        if marker.exists():
            native_pid = int(marker.read_text())
            if Path('/proc/' + str(native_pid) + '/exe').resolve() == Path('/bin/sleep').resolve():
                break
        time.sleep(0.005)
    else:
        raise AssertionError('Actual native sleep executable was not reached')
    os.kill(native_pid, signum)
    stdout, stderr = child.communicate(timeout=5)
    assert child.returncode == 128 + signum, (child.returncode, stderr)
    assert signal.Signals(signum).name.encode() in stderr
    (folder / 'stdout').write_bytes(stdout)
    (folder / 'stderr').write_bytes(stderr)
    try:
        os.kill(native_pid, 0)
    except ProcessLookupError:
        pass
    else:
        raise AssertionError('Actual native child remains after thin wrapper exit')
    rows.append({'passed': True, 'actualNativePid': native_pid, 'actualSignal': signum, 'actualWrapperExitCode': child.returncode})
(args.directory/'report.json').write_text(json.dumps({'passed': len(rows), 'groups': rows, 'scope': 'Actual OS child signal forwarding convention only; no scheduler/browser parity claim'}, indent=2) + '\n')
print(json.dumps({'passed': len(rows), 'report': str(args.directory/'report.json')}))
