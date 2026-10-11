"""Per-process CPU/RSS/thread sampling for media bottleneck attribution."""
import json, sys, time, os
from pathlib import Path
import psutil

output=Path(sys.argv[1]);root=psutil.Process(int(sys.argv[2]));known={};iteration=0
private_every=int(os.environ.get('PROFILE_PRIVATE_EVERY','20'))
with output.open('w') as stream:
    while root.is_running():
        started=time.monotonic();rows=[]
        try: processes=[root,*root.children(recursive=True)]
        except psutil.Error:break
        for process in processes:
            try:
                identity=(process.pid,process.create_time())
                previous=known.get(identity)
                cpu=process.cpu_times();total=cpu.user+cpu.system
                percent=0 if previous is None else max(0,(total-previous[0])/(started-previous[1])*100)
                known[identity]=(total,started)
                args=process.cmdline()
                role='node' if process.pid==root.pid else 'sampler' if 'profile-processes.py' in ' '.join(args) else 'encoder' if process.name()=='ffmpeg' and ('rawvideo' in args or ('matroska' in args and 'pipe:0' in args)) else 'receiver' if process.name()=='ffmpeg' and '-listen' in args else 'audio-encoder' if process.name()=='ffmpeg' and 's16le' in args else 'publisher' if process.name()=='ffmpeg' else 'cef-renderer' if '--type=renderer' in args else 'cef-gpu' if '--type=gpu-process' in args else 'cef-browser' if process.name()=='suitecut-browser' else process.name()
                row={'pid':process.pid,'role':role,'cpuPercent':percent,'rssBytes':process.memory_info().rss,'threads':process.num_threads()}
                try:row['fdCount']=process.num_fds()
                except psutil.Error:row['fdCount']=None
                if private_every>0 and iteration%private_every==0:
                    detailed_at=time.monotonic()
                    try:
                        memory=process.memory_full_info();row.update({'pssBytes':memory.pss,'ussBytes':memory.uss})
                    except psutil.Error:row['privateMemoryUnavailable']=True
                    row['privateMemorySampleMs']=(time.monotonic()-detailed_at)*1000
                rows.append(row)
            except psutil.Error:continue
        stream.write(json.dumps({'monotonic':started,'sampleDurationMs':(time.monotonic()-started)*1000,'privateMemoryEveryIterations':private_every,'processes':rows})+'\n');stream.flush()
        iteration+=1
        time.sleep(max(0,.5-(time.monotonic()-started)))
