"""Sample owned-process private/PSS memory without logging command contents."""
import json
import pathlib
import sys
import time
import psutil

root = psutil.Process(int(sys.argv[1]))
out = pathlib.Path(sys.argv[2])
with out.open('w') as stream:
    while root.is_running():
        began = time.monotonic()
        try:
            processes = [root, *root.children(recursive=True)]
        except psutil.Error:
            break
        rows = []
        for process in processes:
            try:
                args = process.cmdline()
                role = 'node' if process.pid == root.pid else 'sampler' if 'profile-pipeline.py' in ' '.join(args) else 'receiver' if process.name() == 'ffmpeg' else 'cef-renderer' if '--type=renderer' in args else 'cef-gpu' if '--type=gpu-process' in args else 'cef-browser/utility'
                fields = {}
                for line in (pathlib.Path('/proc') / str(process.pid) / 'smaps_rollup').read_text().splitlines():
                    key, separator, value = line.partition(':')
                    if separator:
                        fields[key] = int(value.split()[0]) * 1024
                rows.append({'pid': process.pid, 'created': process.create_time(), 'role': role,
                             'rss': fields.get('Rss', 0), 'pss': fields.get('Pss', 0),
                             'private': fields.get('Private_Clean', 0) + fields.get('Private_Dirty', 0),
                             'threads': process.num_threads(), 'fds': process.num_fds()})
            except (psutil.Error, OSError):
                pass
        stream.write(json.dumps({'monotonic': began, 'samplingMs': (time.monotonic() - began) * 1000,
                                 'processes': rows}) + '\n')
        stream.flush()
        time.sleep(max(0, 17 - (time.monotonic() - began)))
