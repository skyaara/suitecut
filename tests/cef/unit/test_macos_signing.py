"""Signing policy tests; never use a private key or alter the user's Keychain."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location('native_macos_signing', ROOT/'scripts/native_macos_signing.py')
signing = importlib.util.module_from_spec(spec)
spec.loader.exec_module(signing)
PIN = '1234567890ABCDEF1234567890ABCDEF12345678'


class SigningPolicy(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.config = Path(self.directory.name)/'policy.json'

    def resolve(self, available='', explicit=None):
        with patch.object(signing, 'CONFIG', self.config), \
                patch.object(signing, 'USER_CONFIG', Path(self.directory.name)/'user.json'), \
                patch.dict(signing.os.environ, {}, clear=True), \
                patch.object(signing.subprocess, 'check_output', return_value=available):
            return signing.resolve_identity(explicit)

    def test_pinned_certificate_is_reused(self):
        self.config.write_text(json.dumps({'identity': PIN.lower()}))
        self.assertEqual(self.resolve(f'1) {PIN} "Local signing identity"'), PIN)

    def test_user_policy_survives_a_missing_project_cache(self):
        user = Path(self.directory.name)/'user.json'
        user.write_text(json.dumps({'identity': PIN}))
        self.assertEqual(self.resolve(f'1) {PIN} "Stable user identity"'), PIN)

    def test_missing_pinned_certificate_never_falls_back(self):
        self.config.write_text(json.dumps({'identity': PIN}))
        with self.assertRaisesRegex(ValueError, 'refusing ad-hoc fallback'):
            self.resolve('0 valid identities found')

    def test_malformed_policy_is_rejected(self):
        self.config.write_text(json.dumps({'identity': PIN, 'fallback': '-'}))
        with self.assertRaisesRegex(ValueError, 'Invalid signing policy'):
            self.resolve()

    def test_unpinned_other_hosts_keep_development_default(self):
        self.assertEqual(self.resolve(), '-')

    def test_explicit_identity_overrides_local_policy(self):
        self.config.write_text(json.dumps({'identity': 'F'*40}))
        self.assertEqual(self.resolve(f'1) {PIN} "Chosen certificate"', PIN), PIN)

    def test_certificate_names_are_not_ambiguous_identity_pins(self):
        with self.assertRaisesRegex(ValueError, 'fingerprint'):
            self.resolve(explicit='Developer ID Application')


if __name__ == '__main__':
    unittest.main()
