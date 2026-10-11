"""Post-capture diagnostic: remove H264 filler NALs without re-encoding frames."""
import hashlib,json,pathlib,subprocess,sys
root=pathlib.Path(__file__).resolve().parents[3];out=pathlib.Path(sys.argv[1]).resolve();rows=[]
for product,base in [('OBS CEF',out),('SuiteCut',root/'benchmarks/native-sync/2026-10-11-shared-clock')]:
 d=base/'runs/soak-1080p30-stream';media=d/'receiver.flv';a=json.loads((d/'analysis.json').read_text());duration=a['mediaDurationSeconds']
 cmd=['ffmpeg','-v','error','-threads','1','-i',str(media),'-map','0:v:0','-map','0:a:0','-c','copy','-bsf:v','filter_units=remove_types=12','-f','flv','pipe:1']
 child=subprocess.Popen(cmd,stdout=subprocess.PIPE,stderr=subprocess.PIPE);count=0;sha=hashlib.sha256()
 while chunk:=child.stdout.read(1024*1024):count+=len(chunk);sha.update(chunk)
 errors=child.stderr.read().decode();assert child.wait()==0,errors
 rows.append({'product':product,'case':'soak-1080p30-stream','originalBytes':media.stat().st_size,'remuxedBytesWithoutH264FillerNal':count,'remuxedSha256':sha.hexdigest(),'durationSeconds':duration,'originalAverageMuxKbps':media.stat().st_size*8/duration/1000,'withoutFillerAverageMuxKbps':count*8/duration/1000,'originalPayloadRetained':True,'diagnosticOnly':True,'command':cmd,'notes':'Post-capture filter removes H264 filler NAL type 12 and remuxes; no video/audio re-encoding. Byte difference includes remux metadata/header changes, so is not an exact count of padding bytes.'})
(out/'padding-controls.json').write_text(json.dumps(rows,indent=2)+'\n');print(json.dumps(rows),flush=True)
