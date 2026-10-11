import { writeFile } from 'node:fs/promises';
import { setTimeout } from 'node:timers/promises';
import { encodePcm16Wav } from "file:///workspace/suitecut/dist/audio-plugin.js";
export default { async synthesize({outputPath}) {
  await setTimeout(1800);
  const samples=Float32Array.from({length:24000},(_,i)=>.2*Math.sin(2*Math.PI*440*i/24000));
  await writeFile(outputPath,encodePcm16Wav(samples,24000));
} }