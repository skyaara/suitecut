# SPDX-License-Identifier: MIT
"""Independent decoding of controlled native annotation report fixtures."""
import argparse,json,pathlib,subprocess,xml.etree.ElementTree as ET
p=argparse.ArgumentParser();p.add_argument('--executable',required=True);p.add_argument('--directory',required=True,type=pathlib.Path);a=p.parse_args();a.directory.mkdir(parents=True,exist_ok=True);subprocess.run([a.executable,str(a.directory)],check=True)
r=json.loads((a.directory/'annotations.json').read_text());annotation=r['attempts'][0]['annotations'][0];assert annotation=={'type':'fail','description':'\ud800\x00','location':{'file':'/a','line':7,'column':14}}
x=ET.parse(a.directory/'annotations.xml');assert x.find(".//property[@name='native.attempt0.annotation0.descriptionXmlSanitizedUnits']").attrib['value']=='2';assert x.find(".//property[@name='native.attempt0.annotation0.description']").attrib['value']=='\ufffd\ufffd';assert 'native-annotation' in (a.directory/'annotations.html').read_text()
s=json.loads((a.directory/'beforeall-annotations.json').read_text());assert s['attempts'][0]['annotations']==s['skipCauses'][0]['annotations']==s['runtimeSkipped'][0]['annotations'];assert s['runtimeSkipped'][0]['annotations'][0]['location']=={'file':'/a','line':5,'column':14};assert not {'pid','generation','durationMs'}&s['runtimeSkipped'][0].keys()
x=ET.parse(a.directory/'beforeall-annotations.xml');case=x.findall('.//testcase')[1];assert 'time' not in case.attrib;assert case.find("./properties/property[@name='native.noAttempt']").attrib['value']=='true';assert case.find("./properties/property[@name='native.annotation0.location.line']").attrib['value']=='5'
print('PASS independent native annotation JSON/XML/HTML decode and no-attempt cause projection')
