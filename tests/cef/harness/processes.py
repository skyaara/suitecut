# SPDX-License-Identifier: MIT
"""Observe owned descendant identities across POSIX process-group changes.

CEF's C++ transport deliberately creates its own group. A supervisor must keep
that ownership after a child reparents, without matching somebody else's app by
name. ps lstart plus PID is used only for test-process lifetime, never as a lease
for an arbitrary saved process. Ownership observations are not persisted/reused
to signal processes in a later invocation.
"""
import os
import signal
import subprocess
import time


def snapshot():
    result = subprocess.check_output(
        ['ps', '-A', '-o', 'pid=,ppid=,pgid=,stat=,lstart=,command='],
        text=True, timeout=5)
    rows = {}
    for line in result.splitlines():
        fields = line.split(None, 9)
        if len(fields) != 10:
            continue
        pid, parent, group = map(int, fields[:3])
        rows[pid] = dict(pid=pid, parent=parent, group=group, state=fields[3],
                         started=' '.join(fields[4:9]), command=fields[9])
    return rows


class Descendants:
    """A live observer scoped to exactly one newly launched supervisor child."""
    def __init__(self, root):
        self.root = root
        self.observed = {}
        self.groups = {root}
        self.root_started = None

    def observe(self):
        rows = snapshot()
        if self.root_started is None and self.root in rows:
            self.root_started = rows[self.root]['started']
        root_current = self.root in rows and rows[self.root]['started'] == self.root_started
        parents = {self.root} if root_current else set()
        current = [row for pid,row in rows.items() if pid in self.observed and
                   row['started'] == self.observed[pid]['started']]
        parents.update(row['pid'] for row in current)
        active_groups = {row['group'] for row in current if row['group'] in self.groups}
        if root_current:
            active_groups.add(self.root)
        changed = True
        while changed:
            changed = False
            for pid, row in rows.items():
                if pid != self.root and (row['parent'] in parents or row['group'] in active_groups):
                    if pid not in parents:
                        parents.add(pid)
                        changed = True
                    self.observed[pid] = row
                    # Own a new group only after observing its actual leader as
                    # a descendant. Never signal a preexisting external group.
                    if row['group'] == pid:
                        self.groups.add(pid)
                        active_groups.add(pid)
        return [row for pid, row in rows.items() if pid in self.observed and
                self.observed[pid]['started'] == row['started']]

    def survivors(self):
        # observe also discovers newly born helpers in a previously owned group.
        return self.observe()

    def retire(self):
        before = self.survivors()
        signals = []
        errors = []
        for sig in (signal.SIGTERM, signal.SIGKILL):
            live = [row for row in self.survivors() if not row['state'].startswith('Z')]
            if not live:
                break
            for old in live:
                # Fresh identity and group check immediately before each signal.
                actual = snapshot().get(old['pid'])
                if not actual or (actual['started'], actual['group']) != (old['started'], old['group']):
                    continue
                try:
                    os.kill(actual['pid'], sig)
                    signals.append(dict(pid=actual['pid'], signal=sig.name))
                except ProcessLookupError:
                    pass
                except PermissionError as error:
                    errors.append(str(error))
            deadline = time.monotonic() + 2
            while time.monotonic() < deadline:
                if not self.survivors():
                    return dict(before=before, after=[], signals=signals, errors=errors)
                time.sleep(.05)
        return dict(before=before, after=self.survivors(), signals=signals, errors=errors)
