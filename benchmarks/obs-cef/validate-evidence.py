"""Validate completeness and local report references; benchmark failures are data."""
import json, re
from collections import Counter
from pathlib import Path
from urllib.parse import urlparse, unquote

root=Path('benchmarks/obs-cef/2026-10-09')
rows=json.loads((root/'trials.json').read_text())
assert len(rows)==90 and len({r['id'] for r in rows})==90
assert all(r['status'] in ('passed','failed') and r.get('completedAt') for r in rows)
counts=Counter((r['case']['name'],r['backend']) for r in rows)
assert len(counts)==18 and set(counts.values())=={5}
for row in rows:
    directory=root/'trials'/row['id']
    saved=json.loads((directory/'result.json').read_text())
    assert saved['status']==row['status']
    assert (directory/'settings.json').is_file()
    assert (directory/'resources.jsonl').is_file()
    if row['status']=='passed':
        assert Path(row['mediaPath']).is_file()
        assert (directory/'probe.json').is_file() and (directory/'frame.png').is_file()
        assert row['decoded']['width']==row['case']['width'] and row['decoded']['height']==row['case']['height']
    else:assert row.get('error')
features=json.loads((root/'features/results.json').read_text())
expected={f'{backend}-record-audio-{audio}' for backend in ['obs','native'] for audio in ['false','true']}|{f'{backend}-stream-{duration}' for backend in ['obs','native'] for duration in [20000,300000]}
assert {r['id'] for r in features}==expected and len(features)==8
assert all(r.get('completedAt') for r in features)
for row in features:
    if 'record-audio' not in row['id']:continue
    directory=root/'features'/row['id']
    states=json.loads((directory/'recorded-states.json').read_text())['states']
    identities=json.loads((directory/'identity.json').read_text())['changes']
    selected=[];index=0
    for frame,state in enumerate(states):
        while index+1<len(identities) and identities[index+1]['frame']<=frame:index+=1
        if identities[index]['identity']=='primary' and (not selected or selected[-1]['state']!=state):selected.append({'frame':frame,'state':state})
    expected=[0,1,3,7,15];cursor=0
    for observation in selected:
        if cursor<len(expected) and observation['state']==expected[cursor]:cursor+=1
    assert cursor==len(expected),'Recorded input sequence is missing: '+row['id']
    (directory/'recorded-states-validation.json').write_text(json.dumps({'status':'passed','expectedPrimaryStateSequence':expected,'observedPrimaryStates':selected,'blankOrOtherSourceFramesExcluded':True},indent=2)+'\n')
for path in root.rglob('*.json'):json.loads(path.read_text())
report=(root/'report.html').read_text()
links=re.findall(r'(?:href|src)=[\"\']([^\"\']+)[\"\']',report)
for link in links:
    if urlparse(link).scheme or link.startswith('#'):continue
    assert (root/unquote(link.split('#')[0])).exists(),'Broken report link: '+link
result={'status':'passed','recordingAttempts':90,'groupsWithFiveAttempts':18,'functionalAndStreamRuns':8,'localReportReferencesChecked':len(links),'benchmarkFailuresAreRetained':True,'recordedStateSequencesVerifiedWithPrimarySourceIdentity':4}
(root/'evidence-validation.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result))
