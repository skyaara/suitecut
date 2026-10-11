"""Recover generated sample identities independently of CEF timestamps."""
import csv
import json
import pathlib
import subprocess
import sys
import numpy as np
from scipy.signal import correlate


def reference(count):
    n=np.arange(count//24+2,dtype=np.uint32)
    x=n+np.uint32(0x9e3779b9)
    x=(x^(x>>16))*np.uint32(0x7feb352d)
    x=(x^(x>>15))*np.uint32(0x846ca68b)
    x=x^(x>>16)
    codes=(x.astype(np.float64)/4294967296*2-1)*.15
    frames=np.arange(count)
    return codes[frames//24]*(1-(frames%24)/24)+codes[frames//24+1]*(frames%24)/24


def locate(values,expected,predicted=None,radius=4800,global_search=False):
    if len(values)<480 or np.sqrt(np.mean(values**2))<.001:return None
    start=0 if predicted is None else max(0,int(predicted)-radius)
    stop=min(len(expected),len(expected) if global_search else 480000 if predicted is None else int(predicted)+radius+len(values))
    if stop-start<len(values):return None
    signal=expected[start:stop:6]
    needle=values[::6]
    dots=correlate(signal,needle,mode='valid',method='fft')
    sums=np.concatenate(([0],np.cumsum(signal**2)))
    energies=sums[len(needle):]-sums[:-len(needle)]
    scores=dots/np.sqrt(np.maximum(energies*np.dot(needle,needle),1e-30))
    peak=int(np.argmax(scores));position=start+peak*6
    # Recover the exact sample phase after coarse decimation.
    candidates=range(max(0,position-7),min(len(expected)-len(values),position+7)+1)
    norm=np.sqrt(np.dot(values,values))
    best=(-1,None)
    for p in candidates:
        segment=expected[p:p+len(values)]
        score=float(np.dot(segment,values)/(np.sqrt(np.dot(segment,segment))*norm))
        if score>best[0]:best=(score,p)
    if predicted is not None and best[0]<.85 and radius<48000:
        return locate(values,expected,predicted,48000)
    return {'frame':best[1],'correlation':best[0]}


def silence(values):
    window=240 # 5 ms, independent of packet/timestamp boundaries.
    bins=len(values)//window
    rms=np.sqrt(np.mean(values[:bins*window].reshape(bins,window)**2,axis=1))
    quiet=rms<.001
    active=np.flatnonzero(~quiet)
    bounds=[]
    begin=None
    for i,q in enumerate(np.append(quiet,False)):
        if q and begin is None:begin=i
        if not q and begin is not None:
            bounds.append({'fromMs':begin*5,'durationMs':(i-begin)*5,
                           'interior':bool(len(active) and begin>active[0] and i<=active[-1])})
            begin=None
    interior=[b for b in bounds if b['interior']]
    return {'resolutionMs':5,'segments':bounds,'interiorTotalMs':sum(b['durationMs'] for b in interior),
            'interiorMaximumMs':max((b['durationMs'] for b in interior),default=0)}


def sequences(values,expected,counts=None,timestamps=None):
    counts=counts if counts is not None else [1920]*(len(values)//1920)
    cursor=0;previous=None;events=[];rows=[];low=[];misses=0;reacquisitions=0
    for index,count in enumerate(counts):
        count=int(count)
        segment=values[cursor:cursor+count]
        prediction=None if previous is None else previous['frame']+cursor-previous['offset']
        hit=locate(segment,expected,prediction)
        if hit and hit['correlation']<.95:
            misses+=1
            if misses%8==0:
                alternative=locate(segment,expected,global_search=True)
                if alternative and alternative['correlation']>hit['correlation']:
                    hit=alternative;reacquisitions+=1
        elif hit:misses=0
        row={'index':index,'offset':cursor,'frames':int(count),'rms':float(np.sqrt(np.mean(segment**2)))}
        if timestamps is not None:row['ptsMs']=timestamps[index]
        if hit:row.update(hit)
        if hit and hit['correlation']>=.95:
            if previous:
                jump=hit['frame']-(previous['frame']+cursor-previous['offset'])
                if abs(jump)>48:
                    event={'atMs':cursor/48,'jumpFrames':int(jump),'jumpMs':jump/48,
                           'previousIndex':previous['index'],'index':index,
                           'correlation':hit['correlation'],'previousCorrelation':previous['correlation']}
                    if timestamps is not None:
                        event['ptsDiscontinuityMs']=timestamps[index]-previous['ptsMs']-(cursor-previous['offset'])/48
                    events.append(event)
            previous=row
        elif hit:low.append({'index':index,'offsetMs':cursor/48,'correlation':hit['correlation']})
        rows.append(row);cursor+=count
    active=np.flatnonzero(np.abs(values)>.001)
    beginning={}
    if len(active):
        offset=int(active[0])+240
        first=locate(values[offset:offset+960],expected)
        if first and first['correlation']>=.95:
            beginning={'firstAudibleOffsetMs':int(active[0])/48,
                       'firstPreservedFrameEstimate':first['frame']-240,
                       'correlation':first['correlation'],
                       'method':'960-sample correlation starting 5 ms after first audible sample; back-extrapolated 5 ms'}
    return {'sampleFrames':len(values),'matchedWindows':sum(r.get('correlation',0)>=.95 for r in rows),
            'windowCount':len(rows),'lowConfidence':low,'sequenceDiscontinuities':events,
            'positiveJumpTotalMs':sum(max(0,e['jumpMs']) for e in events),
            'negativeJumpTotalMs':sum(max(0,-e['jumpMs']) for e in events),
            'firstMatchedFrame':next((r['frame'] for r in rows if r.get('correlation',0)>=.95),None),
            'firstSound':beginning,
            'globalReacquisitions':reacquisitions,
            'silence':silence(values)},rows


def analyze(directory):
    result=json.loads((directory/'result.json').read_text())
    expected=reference(int((result['durationRequestedMs']/1000+20)*48000))
    report={'method':'Pure generated sample identities, normalized waveform correlation >= 0.95; timestamp-independent',
            'backend':result['backend'],'traces':{},'media':{},'captureStatus':result['status']}
    sources=list(directory.glob('cef-*.csv')) if result['backend']=='suitecut' else [directory/'obs-cef-input.csv']
    traces=sources+([directory/'obs-mixed-output.csv'] if result['backend']=='obs' else [])
    for path in traces:
        if not path.exists():continue
        packets=list(csv.DictReader(path.open()))
        counts=np.array([int(p['frames']) for p in packets])
        values=np.fromfile(path.with_suffix('.s16'),dtype='<i2').reshape(-1,2)[:,0].astype(np.float64)/32768
        if len(values)!=sum(counts):raise ValueError(f'Incomplete PCM/CSV trace: {path}')
        times=[float(p['pts_ms']) for p in packets]
        data,rows=sequences(values,expected,counts,times)
        diffs=np.diff(times)-counts[:-1]/48
        data['timestampHoles']=[{'index':i+1,'durationMs':float(delta)} for i,delta in enumerate(diffs) if delta>2]
        for gap in data['timestampHoles']:
            i=gap['index']
            before,after=rows[i-1],rows[i]
            if min(before.get('correlation',0),after.get('correlation',0))>=.95:
                gap['generatedSampleGapMs']=(after['frame']-before['frame']-before['frames'])/48
                gap['beforeCorrelation']=before['correlation'];gap['afterCorrelation']=after['correlation']
        arrivals=np.array([float(p['received_ms']) for p in packets])
        data['maximumCallbackIntervalMs']=float(max(np.diff(arrivals),default=0))
        effective=np.array([float(p['mapped_ms']) for p in packets])
        data['effectiveTimestampHoles']=[{'index':i+1,'durationMs':float(delta)} for i,delta in enumerate(np.diff(effective)-counts[:-1]/48) if delta>2]
        if packets and 'raw_mapped_ms' in packets[0]:
            raw=np.array([float(p['raw_mapped_ms']) for p in packets])
            corrections=effective-raw
            data['timestampCorrection']={'packetsCorrected':int(sum(abs(corrections)>.01)),
                                         'maximumAbsoluteMs':float(max(abs(corrections),default=0))}
        report['traces'][path.stem]=data
        (directory/(path.stem+'-sequence.jsonl')).write_text(''.join(json.dumps(r)+'\n' for r in rows))
    for name in ['recording.mkv','receiver.flv']:
        path=directory/name
        decode=subprocess.run(['ffmpeg','-v','error','-i',str(path),'-map','0:a:0','-ar','48000','-ac','2','-f','s16le','-'],capture_output=True)
        path.with_suffix(path.suffix+'.audio-decode.log').write_bytes(decode.stderr)
        if decode.returncode:raise ValueError(f'Failed audio decode: {path}')
        path.with_suffix(path.suffix+'.decoded.s16').write_bytes(decode.stdout)
        values=np.frombuffer(decode.stdout,dtype='<i2').reshape(-1,2)[:,0].astype(np.float64)/32768
        data,rows=sequences(values,expected)
        data['decodeExitCode']=decode.returncode;data['decodeErrors']=bool(decode.stderr)
        data['durationMs']=len(values)/48
        report['media'][name]=data
        (directory/(name+'-sequence.jsonl')).write_text(''.join(json.dumps(r)+'\n' for r in rows))
    if result['backend']=='suitecut':
        for path in [directory/'recording.mkv.s16',directory/'profile.s16']:
            if path.exists():
                values=np.fromfile(path,dtype='<i2').reshape(-1,2)[:,0].astype(np.float64)/32768
                data,rows=sequences(values,expected)
                report['traces'][path.stem]=data
                (directory/(path.stem+'-sequence.jsonl')).write_text(''.join(json.dumps(r)+'\n' for r in rows))
        if sources:report['nativeSourceCopy']=source_copy(directory,sources[0])
    samples=directory/'processes.jsonl'
    if samples.exists():
        rows=[json.loads(l) for l in samples.read_text().splitlines()]
        def stat(r):return {k:int(v) for k,v in (l.split() for l in r['host']['cpu.stat'].splitlines())}
        hosts=[r for r in rows if 'host' in r and 'cpu.stat' in r['host']]
        first,last=stat(hosts[0]),stat(hosts[-1])
        report['hostCPUStatDelta']={k:last[k]-first[k] for k in first}
        report['profileSamples']=len(rows)
        report['cpuQuota']=(pathlib.Path('/sys/fs/cgroup/cpu.max').read_text().strip())
        report['processTreeRSSPeakMiB']=max(sum(p['rssBytes'] for p in r['processes'])/1048576 for r in rows)
        report['processTreeCPUMeanCores']=float(np.mean([sum(p['cpuPercent'] for p in r['processes'])/100 for r in rows[1:]]))
    (directory/'analysis.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({'directory':str(directory),'summary':{kind:{name:{key:d[key] for key in ['positiveJumpTotalMs','negativeJumpTotalMs','matchedWindows','windowCount','silence']} for name,d in report[kind].items()} for kind in ['traces','media']}},indent=2))


def source_copy(directory,source_path):
    packets=list(csv.DictReader(source_path.open()))
    starts=np.array([float(p['mapped_ms']) for p in packets])
    arrivals=np.array([float(p['received_ms']) for p in packets])
    frames=np.array([int(p['frames']) for p in packets])
    offsets=np.concatenate(([0],np.cumsum(frames)))
    source=np.fromfile(source_path.with_suffix('.s16'),dtype='<i2').reshape(-1,2)
    results={}
    for path in [directory/'recording.mkv.csv',directory/'profile.csv']:
        blocks=list(csv.DictReader(path.open()))
        actual=np.fromfile(path.with_suffix('.s16'),dtype='<i2').reshape(-1,2)
        origin=float(blocks[0]['at_ms']);finish=float(blocks[-1]['at_ms'])+int(blocks[-1]['frames'])/48
        mismatch=0;interior_missing=0;eventual_missing=0
        for b in blocks:
            at=float(b['at_ms']);count=int(b['frames']);index=int(b['sample_index']);encoded=float(b['encoded_ms'])
            if at<max(origin+20,starts[0]+20) or at+count/48>finish-150:continue
            expected=np.zeros((count,2),dtype='<i2');covered=np.zeros(count,dtype=bool)
            first=max(0,np.searchsorted(starts,at-100));last=np.searchsorted(starts,at+count/48+1)
            for j in range(first,last):
                offset=round((starts[j]-at)*48);a=max(0,offset);z=min(count,offset+int(frames[j]))
                if z<=a:continue
                covered[a:z]=True
                if arrivals[j]<=encoded:expected[a:z]=source[offsets[j]+a-offset:offsets[j]+z-offset]
            mismatch+=int(np.count_nonzero(np.any(actual[index:index+count]!=expected,axis=1)))
            eventual_missing+=int(sum(~covered));interior_missing+=int(b['missing_frames'])
        results[path.stem]={'interiorMismatchedFrames':mismatch,'interiorUncoveredMs':eventual_missing/48,
                            'interiorMissingAtEncodeMs':interior_missing/48}
    return results


if __name__=='__main__':analyze(pathlib.Path(sys.argv[1]))
