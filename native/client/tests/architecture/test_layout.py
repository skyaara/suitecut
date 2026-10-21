# SPDX-License-Identifier: MIT
"""Negative controls: architecture checks must reject forbidden dependencies."""
from contextlib import redirect_stdout
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest

SDK = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('layout', SDK / 'tools/check-layout.py')
layout = importlib.util.module_from_spec(spec)
spec.loader.exec_module(layout)


class LayoutContracts(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        layout.SDK = Path(self.temp.name) / 'client'
        layout.SHARED = Path(self.temp.name) / 'shared'
        for path in (layout.SDK / 'src/core', layout.SDK / 'src/agent',
                     layout.SDK / 'include/suitecut', layout.SHARED):
            path.mkdir(parents=True)
        self.write('include/suitecut/json.hpp', '#pragma once\n')
        self.write('src/core/json.cpp', '#include "suitecut/json.hpp"\n')

    def write(self, path, value):
        target = layout.SDK / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(value)

    def check(self):
        with redirect_stdout(io.StringIO()):
            layout.check()

    def test_valid_isolated_core(self):
        self.check()

    def test_core_cannot_use_agent_public_api(self):
        self.write('include/suitecut/agent.hpp', '#pragma once\n')
        self.write('src/core/json.cpp', '#include "suitecut/agent.hpp"\n')
        with self.assertRaisesRegex(ValueError, 'Invalid public dependency'):
            self.check()

    def test_core_cannot_reach_private_sibling(self):
        self.write('src/agent/private.hpp', '#pragma once\n')
        self.write('src/core/json.cpp', '#include "../agent/private.hpp"\n')
        with self.assertRaisesRegex(ValueError, 'Invalid private dependency'):
            self.check()

    def test_public_api_cannot_include_private_source(self):
        self.write('include/suitecut/json.hpp', '#include "../../src/agent/private.hpp"\n')
        with self.assertRaisesRegex(ValueError, 'Public header depends on a private header'):
            self.check()

    def test_shared_helpers_cannot_reach_cef_embedder(self):
        (layout.SHARED / 'dom_driver.h').write_text('#include "../browser/cef_private.h"\n')
        with self.assertRaisesRegex(ValueError, 'Shared helper reaches outside'):
            self.check()

    def test_public_angle_include_cannot_import_cef(self):
        self.write('include/suitecut/json.hpp', '#include <cef_browser.h>\n')
        with self.assertRaisesRegex(ValueError, 'Public header depends on a private header'):
            self.check()

    def test_unowned_source_cannot_go_unchecked(self):
        self.write('src/misc.cpp', '')
        with self.assertRaisesRegex(ValueError, 'Unowned SDK source'):
            self.check()


if __name__ == '__main__':
    unittest.main()
