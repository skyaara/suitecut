#!/usr/bin/env python3
"""Bounded native-session IPC fuzzing with an explicitly supplied test engine.

Run under the repository proof supervisor; it owns/reaps the persistent worker.
The engine must speak the SuiteCut protocol. This is not CEF execution evidence.
"""
import argparse
import json
import os
from pathlib import Path
import random
import socket
import struct
import subprocess
import tempfile
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cli', required=True, type=Path)
    parser.add_argument('--engine', required=True, type=Path)
    parser.add_argument('--iterations', type=int, default=10000)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    if not 1 <= args.iterations <= 1000000:
        parser.error('iterations must be 1..1000000')
    rng = random.Random(0x53455353494F4E)
    started = time.monotonic()
    with tempfile.TemporaryDirectory(prefix='sc-fuzz-') as directory:
        os.chmod(directory, 0o700)
        command = [str(args.cli.resolve()), '--session', 'fuzz', '--session-dir', directory]

        def cli(*words):
            result = subprocess.run(command + list(words), text=True, capture_output=True, timeout=45)
            if result.returncode:
                raise RuntimeError(result.stdout + result.stderr)
            value = json.loads(result.stdout)
            if not value['ok']:
                raise RuntimeError(str(value))
            return value['result']

        def memory(pid):
            status = Path(f'/proc/{pid}/status').read_text().splitlines()
            rss = next(int(line.split()[1])*1024 for line in status if line.startswith('VmRSS:'))
            return {'rss': rss, 'fds': len(list(Path(f'/proc/{pid}/fd').iterdir())),
                    'threads': len(list(Path(f'/proc/{pid}/task').iterdir()))}

        def exchange(wire):
            with socket.socket(socket.AF_UNIX) as client:
                client.settimeout(3)
                client.connect(directory+'/fuzz.sock')
                client.sendall(wire)
                client.shutdown(socket.SHUT_WR)
                # Drain every response so a successful parse exercises write framing.
                data = bytearray()
                while True:
                    part = client.recv(65536)
                    if not part:
                        break
                    data.extend(part)
                    if len(data) > 8*1024*1024+4:
                        raise AssertionError('Response exceeded byte budget')
                if data:
                    assert len(data) >= 4
                    size, = struct.unpack('<I', data[:4])
                    assert size == len(data)-4
                    assert isinstance(json.loads(data[4:]), dict)

        cli('--backend', 'cef', '--browser', str(args.engine.resolve()),
            '--browser-arg', '--simple-test-engine', 'open')
        pid = cli('status')['pid']
        samples = []
        try:
            seeds = [None, [], {}, {'request': None}, {'request': {'tool': 42}},
                     {'request': {'tool': 'observe', 'arguments': {'format': 'compact'}}, 'profiles': 'core'},
                     {'request': {'tool': 'act', 'arguments': {'actions': []}}, 'profiles': 'core'},
                     {'request': {'tool': 'profiles'}, 'profiles': 'all'},
                     {'request': {'tool': 'evaluate', 'arguments': {'expression': 'forbidden'}}, 'profiles': 'core'},
                     {'request': {'tool': '_session_status'}}]
            for iteration in range(args.iterations):
                payload = json.dumps(rng.choice(seeds), separators=(',', ':')).encode()
                mode = iteration % 7
                if mode == 0:
                    wire = struct.pack('<I', rng.choice([0, 0xffffffff, 4194305]))
                elif mode == 1:
                    wire = struct.pack('<I', len(payload))+payload[:rng.randrange(len(payload)+1)]
                elif mode == 2:
                    payload = bytearray(payload)
                    for _ in range(rng.randrange(1, 7)):
                        payload[rng.randrange(len(payload))] = rng.randrange(256)
                    wire = struct.pack('<I', len(payload))+payload
                elif mode == 3:
                    wire = bytes(rng.randrange(256) for _ in range(rng.randrange(4)))
                else:
                    wire = struct.pack('<I', len(payload))+payload
                try:
                    exchange(wire)
                except ConnectionResetError:
                    # Rejecting an oversized prefix may leave unread caller bytes.
                    pass
                if iteration in {min(499, args.iterations//3), args.iterations-1}:
                    assert cli('status')['pid'] == pid
                    samples.append(memory(pid))
            assert samples[-1]['fds'] == samples[0]['fds'], samples
            assert samples[-1]['threads'] == samples[0]['threads'], samples
            assert samples[-1]['rss']-samples[0]['rss'] < 32*1024*1024, samples
            cli('close')
            assert not cli('status')['running']
        finally:
            try:
                cli('close')
            except Exception:
                pass
    report = {'ok': True, 'seed': '53455353494f4e', 'iterations': args.iterations,
              'seconds': round(time.monotonic()-started, 3), 'workerSamples': samples,
              'scope': 'Controlled native engine/IPC; bounded retention samples, not whole-browser leak proof'}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
