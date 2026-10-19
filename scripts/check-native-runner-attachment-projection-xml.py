# SPDX-License-Identifier: MIT
"""Independent roundtrip checks for bounded native projection report XML."""
import argparse
import json
import pathlib
import subprocess
import tempfile
import xml.etree.ElementTree as ET

p = argparse.ArgumentParser()
p.add_argument('--executable', type=pathlib.Path)
p.add_argument('--directory', required=True, type=pathlib.Path)
p.add_argument('--existing', action='store_true')
a = p.parse_args()
root = a.directory.resolve()
root.mkdir(parents=True, exist_ok=True)
if not a.existing:
    assert a.executable is not None
    root = pathlib.Path(tempfile.mkdtemp(prefix='actual-', dir=root))
    actual = subprocess.run([str(a.executable), str(root)], capture_output=True, timeout=20)
    (root / 'stdout.bin').write_bytes(actual.stdout)
    (root / 'stderr.bin').write_bytes(actual.stderr)
    assert actual.returncode == 0 and not actual.stderr, (actual.returncode, actual.stdout, actual.stderr)
for name in ['large', 'noncharacter']:
    report = json.loads((root / (name + '.json')).read_bytes())
    props = {row.attrib['name']: row.attrib['value'] for row in ET.parse(root / (name + '.xml')).findall('.//property')}
    current = props['native.attachmentViewsJSON']
    assert len(current.encode()) > 8192
    if name == 'large':
        assert props['native.attachmentViewsXmlSanitizedUnits'] == '0'
        assert json.loads(current) == report['attachmentViews']
        assert 'native.attachmentViewsJSONUtf8Hex' not in props
    else:
        assert props['native.attachmentViewsXmlSanitizedUnits'] == '1'
        exact = bytes.fromhex(props['native.attachmentViewsJSONUtf8Hex']).decode('utf8')
        assert '\ufffe' in exact and '\ufffe' not in current
        assert exact.replace('\ufffe', '\ufffd') == current
        assert json.loads(exact) == report['attachmentViews']
        assert report['attachmentViews'][0]['rejections'][0]['error'] == 'original \ud800 <& \x00 \ufffe'
assert 'Accepted native attachment views' in (root / 'large.html').read_text()
print('PASS2 independent native projection XML roundtrips, exact isolated UTF16/UTF8 and XML-invalid original fallback', root)
