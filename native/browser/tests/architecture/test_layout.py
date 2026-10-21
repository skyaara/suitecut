# SPDX-License-Identifier: MIT
"""False-green controls for the module inventory and PCH-independent checker."""
from contextlib import redirect_stdout
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


layout = load('cef_layout', ROOT / 'tools/check-layout.py')
pch = load('cef_pch', ROOT / 'tools/check-without-pch.py')


class ModuleContracts(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        layout.ROOT = Path(self.temp.name)
        for folder in ('cmake', 'page', 'runtime', 'process', 'protocol', 'controllers/dom'):
            (layout.ROOT / folder).mkdir(parents=True)
        self.manifest = layout.ROOT / 'cmake/RuntimeModules.cmake'
        self.manifest.write_text('\n'.join(
            f'suitecut_browser_module({name} {name}/impl.cc)'
            for name in ('page', 'runtime', 'process', 'protocol'))
            + '\n  suitecut_browser_module(pch runtime/precompiled.cc)\n')
        for folder in ('page', 'runtime', 'process', 'protocol'):
            (layout.ROOT / folder / 'impl.cc').write_text('// Source\n')
        (layout.ROOT / 'runtime/precompiled.cc').write_text('// PCH\n')
        (layout.ROOT / 'controllers/dom/query.inc').write_text('// Private adapter\n')

    def check(self):
        with redirect_stdout(io.StringIO()):
            layout.check()

    def test_valid_inventory(self):
        self.check()

    def test_orphan_source_is_rejected(self):
        (layout.ROOT / 'page/orphan.cc').write_text('// Missing inventory entry\n')
        with self.assertRaisesRegex(ValueError, 'exactly one module'):
            self.check()

    def test_duplicate_source_is_rejected(self):
        self.manifest.write_text(self.manifest.read_text().replace(
            'page/impl.cc)', 'page/impl.cc page/impl.cc)'))
        with self.assertRaisesRegex(ValueError, 'exactly one module'):
            self.check()

    def test_textual_cc_include_is_rejected(self):
        (layout.ROOT / 'page/impl.cc').write_text('#include "runtime/impl.cc"\n')
        with self.assertRaisesRegex(ValueError, 'Textual implementation include'):
            self.check()

    def test_protocol_cannot_depend_on_page_owner(self):
        (layout.ROOT / 'protocol/impl.cc').write_text('#include "page/source.h"\n')
        with self.assertRaisesRegex(ValueError, 'higher layer'):
            self.check()

    def test_missing_controller_is_rejected(self):
        (layout.ROOT / 'page/impl.cc').write_text('#include "controllers/dom/missing.inc"\n')
        with self.assertRaisesRegex(ValueError, 'Missing or unowned'):
            self.check()

    def test_monolith_reintroduction_is_rejected(self):
        (layout.ROOT / 'browser.cc').write_text('// Accidental old entry point\n')
        with self.assertRaisesRegex(ValueError, 'monolith'):
            self.check()

    def test_missing_macro_fixture_include_is_rejected(self):
        (layout.ROOT / 'page/impl.cc').write_text(
            '#define SUITECUT_TEST_INCLUDE "missing.inc"\n#include SUITECUT_TEST_INCLUDE\n')
        with self.assertRaisesRegex(ValueError, 'Missing or unowned'):
            self.check()

    def test_pch_filter_removes_forced_includes_and_outputs(self):
        command = ['clang++', '-c', 'page/capture.cc', '-Xclang', '-include-pch', '-Xclang',
                   '/build/cmake_pch.hxx.pch', '-Xclang', '-include', '-Xclang',
                   '/build/cmake_pch.hxx', '-MD', '-MT', 'object', '-MF', 'deps', '-o', 'object',
                   '-fexceptions', '-std=c++20']
        self.assertEqual(pch.without_pch(command),
                         ['clang++', 'page/capture.cc', '-fexceptions', '-std=c++20', '-fsyntax-only'])


if __name__ == '__main__':
    unittest.main()
