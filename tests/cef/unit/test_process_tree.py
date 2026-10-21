# SPDX-License-Identifier: MIT
"""The C++ transport uses a different process group; exercise that exact hazard."""
import importlib.util
import subprocess
import sys
import time
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'tests/cef'))
from harness import processes



class ProcessTreeTests(unittest.TestCase):
    def test_escaped_child_remains_owned_after_parent_exits(self):
        source = ('import subprocess,sys,time;'
                  'p=subprocess.Popen([sys.executable,"-c","import time;time.sleep(30)"],start_new_session=True);'
                  'print(p.pid,flush=True);time.sleep(.5)')
        parent = subprocess.Popen([sys.executable,'-c',source],start_new_session=True,
                                  stdout=subprocess.PIPE,text=True)
        tree = processes.Descendants(parent.pid)
        escaped = int(parent.stdout.readline().strip())
        try:
            rows=tree.observe()
            actual=next(row for row in rows if row['pid']==escaped)
            self.assertNotEqual(actual['group'],parent.pid)
            parent.wait(timeout=3)
            self.assertTrue(tree.survivors())
            cleanup=tree.retire()
            self.assertTrue(cleanup['before'])
            self.assertFalse(tree.survivors(),cleanup)
        finally:
            tree.retire()
            if parent.poll() is None:
                parent.kill()
            parent.wait(timeout=3)
            parent.stdout.close()

    def test_unrelated_process_and_mismatched_birth_are_not_signaled(self):
        outsider=subprocess.Popen([sys.executable,'-c','import time;time.sleep(30)'],start_new_session=True)
        parent=subprocess.Popen([sys.executable,'-c','import time;time.sleep(30)'],start_new_session=True)
        tree=processes.Descendants(parent.pid)
        try:
            rows=processes.snapshot()
            tree.observed[outsider.pid]=dict(rows[outsider.pid],started='different process identity')
            tree.observe()
            cleanup=tree.retire()
            self.assertEqual(outsider.poll(),None,cleanup)
            self.assertNotIn(outsider.pid,[r['pid'] for r in tree.survivors()])
        finally:
            for child in (outsider,parent):
                if child.poll() is None:
                    child.kill()
                child.wait(timeout=3)


if __name__ == '__main__':
    unittest.main()
