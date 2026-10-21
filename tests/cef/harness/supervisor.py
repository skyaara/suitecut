# SPDX-License-Identifier: MIT
import os
import signal
import subprocess
import threading
import time

from .processes import Descendants
from .reporting import atomic_json

STOP = threading.Event()
LOG_BOUND = 16 * 1024 * 1024


def group_members(group):
    """Never select by a browser name, substring, or somebody else's profile."""
    output = subprocess.check_output(
        ['ps', '-A', '-o', 'pid=,ppid=,pgid=,stat=,command='], text=True, timeout=5)
    members = []
    for line in output.splitlines():
        parts = line.split(None, 4)
        if len(parts) == 5 and int(parts[2]) == group:
            members.append(dict(pid=int(parts[0]), parent=int(parts[1]),
                                group=int(parts[2]), state=parts[3], command=parts[4]))
    return members


def retire_group(group):
    """Bound cleanup of just the session created for this attempt."""
    before = group_members(group)
    if not before:
        return {'before': [], 'after': [], 'signals': []}
    sent = []
    for sig in (signal.SIGTERM, signal.SIGKILL):
        # A dead, unreaped session leader cannot be signaled on macOS (EPERM).
        # Its owning Popen.wait below is the physical retirement receipt.
        if not any(not row['state'].startswith('Z') for row in group_members(group)):
            break
        try:
            os.killpg(group, sig)
            sent.append(sig.name)
        except ProcessLookupError:
            break
        except PermissionError:
            if not any(not row['state'].startswith('Z') for row in group_members(group)):
                break
            raise
        deadline = time.monotonic() + 2
        while time.monotonic() < deadline:
            if not any(not row['state'].startswith('Z') for row in group_members(group)):
                return {'before': before, 'after': [], 'signals': sent}
            time.sleep(.05)
    return {'before': before, 'after': group_members(group), 'signals': sent}


def supervised(command, cwd, directory, env, timeout):
    """Real subprocesses in a private session, also used by supervisor self-tests."""
    directory.mkdir(parents=True, exist_ok=True)
    started = time.monotonic()
    result = {'command': [str(x) for x in command], 'timeoutSeconds': timeout,
              'passed': False, 'errors': []}
    with (directory / 'stdout.log').open('wb') as stdout, (directory / 'stderr.log').open('wb') as stderr:
        child = subprocess.Popen(command, cwd=cwd, env=env, stdout=stdout, stderr=stderr,
                                 start_new_session=True)
        tree=Descendants(child.pid)
        result['pid'] = child.pid
        try:
            while child.poll() is None:
                tree.observe()
                if STOP.is_set():
                    result['errors'].append('Interrupted; attempt did not complete')
                    break
                if time.monotonic() - started > timeout:
                    result['errors'].append('Hard attempt deadline exceeded')
                    break
                if stdout.tell() > LOG_BOUND or stderr.tell() > LOG_BOUND:
                    result['errors'].append('Attempt log exceeded 16 MiB bound')
                    break
                time.sleep(.1)
        except Exception as error:
            result['errors'].append('Process observation failed: '+str(error))
        finally:
            # Reap a normal parent before looking for orphaned helper processes.
            try:
                if child.poll() is None:
                    result['cleanup'] = retire_group(child.pid)
                else:
                    child.wait()
                    deadline = time.monotonic() + 1
                    while (group_members(child.pid) or tree.survivors()) and time.monotonic() < deadline:
                        time.sleep(.05)
                    result['cleanup'] = retire_group(child.pid)
                    if result['cleanup']['before'] or tree.survivors():
                        result['errors'].append('Process descendants remained after the test exited')
            except Exception as error:
                result['errors'].append('Process cleanup observation failed: '+str(error))
            finally:
                # Even when ps fails, reap the Popen child and retire its own
                # freshly created group. Observation failure is never success.
                if child.poll() is None:
                    try:
                        os.killpg(child.pid,signal.SIGTERM)
                    except ProcessLookupError:
                        pass
                    try:
                        child.wait(timeout=2)
                    except subprocess.TimeoutExpired:
                        try:
                            os.killpg(child.pid,signal.SIGKILL)
                        except ProcessLookupError:
                            pass
                        child.kill();child.wait(timeout=3)
                else:
                    child.wait()
            try:
                result['descendantCleanup']=tree.retire()
                result['observedDescendants']=list(tree.observed.values())
            except Exception as error:
                result['errors'].append('Escaped descendant cleanup failed: '+str(error))
            result['exitCode'] = child.returncode
            if child.returncode != 0:
                result['errors'].append(f'Process exited with status {child.returncode}')
            # A killed parent may have been a zombie during cleanup. Recheck after wait.
            try:
                result['remainingProcesses'] = group_members(child.pid)+tree.survivors()
            except Exception as error:
                result['remainingProcesses']=None
                result['errors'].append('Final process retirement is unverified: '+str(error))
            if result['remainingProcesses']:
                result['errors'].append('Owned process group did not fully retire')
    if any((directory / name).stat().st_size > LOG_BOUND for name in ('stdout.log', 'stderr.log')):
        result['errors'].append('Attempt log exceeded 16 MiB bound')
    result['durationSeconds'] = round(time.monotonic() - started, 3)
    result['passed'] = not result['errors']
    atomic_json(directory / 'process.json', result)
    return result
