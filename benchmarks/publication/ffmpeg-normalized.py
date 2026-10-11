#!/usr/bin/env python3
"""Encoder-setting adapter only; does not change historical capture or queues."""
import os,sys,json,pathlib
args=sys.argv[1:]
if '-c:v' in args and args[args.index('-c:v')+1]=='libvpx-vp9':
 args[args.index('-c:v')+1]='libx264'
 for option in ['-deadline','-cpu-used','-row-mt']:
  while option in args:
   index=args.index(option);del args[index:index+2]
 if '-b:v' in args:
  index=args.index('-b:v');del args[index:index+2]
 if '-threads' in args:args[args.index('-threads')+1]='1'
 args[-1:-1]=['-preset','veryfast','-tune','zerolatency','-g','60','-bf','0','-f','matroska']
if '-c:v' in args and args[args.index('-c:v')+1]=='libx264':
 args[-1:-1]=['-threads:v','1','-threads:a','2','-filter_threads','1','-x264-params','bframes=0']
directory=os.environ.get('BENCH_DIRECTORY')
if directory:
 pathlib.Path(directory,f'encoder-command-{os.getpid()}.json').write_text(json.dumps({'original':sys.argv[1:],'normalized':args,'cpus':sorted(os.sched_getaffinity(0))},indent=2))
os.execv('/usr/bin/ffmpeg',['/usr/bin/ffmpeg',*args])
