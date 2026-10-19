# SPDX-License-Identifier: MIT
"""Read a completed genuine native filesystem-fault observer record."""

def read_native_file_marker(path):
    # The producer opens with O_CREAT before its one real newline-terminated
    # write. Existence alone does not mean the observed record is available.
    try:
        raw = path.read_bytes()
    except FileNotFoundError:
        return None
    if not raw.endswith(b'\n'):
        return None
    parts = raw.decode('ascii').split()
    assert len(parts) == 5, 'malformed complete native file marker'
    assert all(value.isdecimal() for value in parts[:4]), 'invalid native marker identity'
    assert parts[3] in ('0', '1'), 'invalid native marker file kind'
    assert bytes.fromhex(parts[4]), 'empty native marker path'
    return parts
