# SPDX-License-Identifier: MIT
"""Coupled real CLI/native/host SIGKILL proof with an explicit external subreaper."""
import ctypes
import argparse
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import time

ROOT = Path(__file__).resolve().parent
BASE = ROOT
parser = argparse.ArgumentParser()
parser.add_argument('--executable', type=Path, default=BASE/'suitecut-runner')
parser.add_argument('--cli-script', type=Path, default=ROOT/'dist/native-runner-cli.js')
parser.add_argument('--suite-api', type=Path, default=ROOT/'dist/native-suite.js')
parser.add_argument('--node', default=shutil.which('node'))
parser.add_argument('--directory', type=Path, default=ROOT/'parent-death-evidence')
args = parser.parse_args()
folder = args.directory.resolve()
folder.mkdir(parents=True, exist_ok=True)
marker = folder / 'actual-body-start.json'
report = folder / 'never-final-report.json'
marker.unlink(missing_ok=True)
report.unlink(missing_ok=True)
libc = ctypes.CDLL(None, use_errno=True)
old = ctypes.c_int()
assert libc.prctl(37, ctypes.byref(old), 0, 0, 0) == 0  # PR_GET_CHILD_SUBREAPER
assert libc.prctl(36, 1, 0, 0, 0) == 0  # PR_SET_CHILD_SUBREAPER
try:
    suite = folder / 'suite.mjs'
    mark = 'const marker='+json.dumps(str(marker))+";fs.writeFileSync(marker+'.tmp',JSON.stringify({host:process.pid,native:process.ppid}));fs.renameSync(marker+'.tmp',marker);"
    suite.write_text("import fs from 'node:fs';import process from 'node:process';import {defineNativeSuite} from " + json.dumps(args.suite_api.resolve().as_uri()) + ";export default defineNativeSuite({tests:[{title:'actual native host body held through parent kill',timeout:0,run:async()=>{" + mark + "setInterval(()=>{},1000);await new Promise(()=>{})}}]});")
    wrapper = subprocess.Popen([args.node, str(args.cli_script.resolve()), '--executable', str(args.executable.resolve()), '--suite', str(suite), '--report', str(report)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    until = time.monotonic() + 5
    while not marker.exists():
        assert time.monotonic() < until and wrapper.poll() is None
        time.sleep(0.005)
    owners = json.loads(marker.read_bytes())
    assert Path('/proc/' + str(owners['native']) + '/exe').resolve() == args.executable.resolve()
    host_stat_before = Path('/proc/' + str(owners['host']) + '/stat').read_text()
    assert int(host_stat_before.rsplit(')', 1)[1].split()[1]) == owners['native']
    os.kill(owners['native'], signal.SIGKILL)
    stdout, stderr = wrapper.communicate(timeout=5)
    assert wrapper.returncode == 137 and b'SIGKILL' in stderr
    (folder / 'stdout').write_bytes(stdout)
    (folder / 'stderr').write_bytes(stderr)
    # Native scheduler is dead; only this explicit test subreaper can confirm
    # the orphan's OS exit. Do not attribute this wait to the native runner.
    until = time.monotonic() + 3
    observed_state = None
    actual_wait = None
    while time.monotonic() < until:
        try:
            observed_state = Path('/proc/' + str(owners['host']) + '/stat').read_text().rsplit(')', 1)[1].split()[0]
        except FileNotFoundError:
            observed_state = 'absent'
        pid, status = os.waitpid(owners['host'], os.WNOHANG)
        if pid:
            actual_wait = status
            break
        time.sleep(0.005)
    assert actual_wait is not None and os.WIFSIGNALED(actual_wait) and os.WTERMSIG(actual_wait) == signal.SIGKILL
    assert not report.exists(), 'SIGKILL must not fabricate final native report publication'
    value = {'passed': 1, 'actualWrapperExitCode': wrapper.returncode, 'actualNativeRunnerPid': owners['native'], 'actualHostPid': owners['host'], 'hostObservedProcStateBeforeExternalWait': observed_state, 'externalProofSubreaper': True, 'externalWaitConfirmedHostSignal': os.WTERMSIG(actual_wait), 'nativeRunnerCouldNotObserveItsOwnCleanupAfterSIGKILL': True, 'finalNativeReportPublished': False, 'scope': 'Real thin CLI137 + native parent SIGKILL + actual callback-host PDEATHSIG kill. Kernel host wait was performed only by external proof subreaper.'}
    (folder / 'report.json').write_text(json.dumps(value, indent=2) + '\n')
    print(json.dumps(value))
finally:
    assert libc.prctl(36, old.value, 0, 0, 0) == 0
