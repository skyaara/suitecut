"""Standalone codec benchmark figures; quality settings are not equivalent."""
import json
import pathlib
import sys

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

directory=pathlib.Path(sys.argv[1] if len(sys.argv)>1 else 'benchmarks/native-codecs/2026-10-10')
data=json.loads((directory/'decoded-validation.json').read_text())
rows=data['cases']
labels=[r['videoCodec'].replace('lib','')+' / '+r['audioCodec'].replace('lib','') for r in rows]
fig,axes=plt.subplots(1,3,figsize=(15,5.5),layout='constrained')
colors=['#288ccc','#288ccc','#288ccc','#288ccc','#d88a31','#d88a31','#438c62','#438c62']
for ax,values,title in zip(axes,[
    [r['resources']['meanNativeCores'] for r in rows],
    [r['capture']['bytes']/1048576 for r in rows],
    [r['capture']['distinctFps'] for r in rows],
],['Mean native CPU cores','20-second file size (MiB)','Decoded distinct frames / second']):
    ax.barh(labels,values,color=colors)
    ax.invert_yaxis();ax.set_title(title,fontsize=11);ax.grid(axis='x',alpha=.2);ax.set_axisbelow(True)
    for i,value in enumerate(values):ax.text(value,i,f' {value:.2f}',va='center',fontsize=8)
    ax.set_xlim(0,max(values)*1.2)
axes[2].axvline(30,color='#888888',linestyle='--',linewidth=1)
fig.suptitle('SuiteCut C++ codec captures — 1920×1080, 30 fps, browser animation + scrolling',fontsize=13)
fig.supxlabel('One host; software rendering/encoding; includes a four-second Node stall. Encoder quality settings differ.',fontsize=9)
fig.savefig(directory/'codec-performance.svg');fig.savefig(directory/'codec-performance.png',dpi=160)
