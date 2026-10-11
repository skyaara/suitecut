"""Sample process-tree CPU and summed RSS. Shared pages may be counted twice.

Usage: python3 sample-processes.py output.jsonl rootpid [rootpid ...]
CPU uses deltas for surviving processes; short-lived children between samples
are not observed. One fully busy CPU is 100%, four CPUs are 400%.
"""
import json, sys, time
import psutil

roots = [int(v) for v in sys.argv[2:]]
previous = {}
last = time.monotonic()
with open(sys.argv[1], 'w', buffering=1) as output:
    while True:
        processes = {}
        for root in roots:
            try:
                parent = psutil.Process(root)
                for proc in [parent, *parent.children(recursive=True)]:
                    processes[proc.pid] = proc
            except psutil.Error:
                pass
        now = time.monotonic()
        cpu = 0
        rss = 0
        current = {}
        for pid, proc in processes.items():
            try:
                times = proc.cpu_times()
                current[pid] = times.user + times.system
                if pid in previous:
                    cpu += max(0, current[pid] - previous[pid])
                rss += proc.memory_info().rss
            except psutil.Error:
                pass
        output.write(json.dumps({'monotonic': now, 'cpuPercent': cpu / max(.001, now - last) * 100,
                                 'rssBytes': rss, 'processCount': len(current)}) + '\n')
        previous = current
        last = now
        time.sleep(.5)
