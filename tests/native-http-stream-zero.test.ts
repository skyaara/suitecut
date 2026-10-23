// SPDX-License-Identifier: MIT
import { execFile } from 'node:child_process'
import { mkdtemp, readFile, rm, writeFile } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import process from 'node:process'
import { pathToFileURL } from 'node:url'
import { promisify } from 'node:util'

import * as ts from 'typescript'
import { expect, it } from 'vitest'
import * as z from 'zod'

const execute = promisify(execFile)

async function actualZeroDescriptor(mode: 'close' | 'reused') {
  const directory = await mkdtemp(join(tmpdir(), 'suitecut-readstream-zero-'))
  try {
    const path = join(directory, 'original.bin'),
      replacement = join(directory, 'caller.bin')
    await writeFile(path, 'actual descriptor zero file bytes')
    await writeFile(replacement, 'distinct caller-owned descriptor')
    const source = await readFile(new URL('../src/native-http-stream.ts', import.meta.url), 'utf8')
    const helper = join(directory, 'native-http-stream.mjs')
    await writeFile(
      helper,
      ts.transpileModule(source, {
        compilerOptions: { target: ts.ScriptTarget.ES2023, module: ts.ModuleKind.ESNext },
      }).outputText,
    )
    const program = `
import assert from 'node:assert/strict';
import { Buffer } from 'node:buffer';
import { close, closeSync, createReadStream, fstatSync, openSync, ReadStream } from 'node:fs';
import { NativeMultipartStreamJob } from ${JSON.stringify(pathToFileURL(helper).href)};
closeSync(0);
const stream=createReadStream(${JSON.stringify(path)},{emitClose:false,autoClose:false});
let opened=-1, closeEvents=0, closeCalls=0, releaseClose;
stream.on('open',fd=>opened=fd);
stream.on('close',()=>++closeEvents);
let notifyClose;
const actualClose=new Promise(resolve=>notifyClose=resolve);
let callerFD=-1;
if (${JSON.stringify(mode)}==='reused') stream.on('end',()=>{
  closeSync(opened);
  callerFD=openSync(${JSON.stringify(replacement)},'r');
  assert.equal(callerFD,0);
});
const job=new NativeMultipartStreamJob({stream},(fd,done)=>{
  ++closeCalls;
  close(fd,error=>{releaseClose=()=>done(error);notifyClose()});
});
const chunks=[];
const pending=job.pump(chunk=>{chunks.push(Buffer.from(chunk));return Promise.resolve()}).then(
  parts=>({parts}),error=>({error:error.message})
);
if (${JSON.stringify(mode)}==='close') {
  await actualClose;
  assert.equal(opened,0);
  assert.equal(stream.closed,false);
  assert.deepEqual(NativeMultipartStreamJob.physical(),{jobs:1,streams:1});
  assert.throws(()=>fstatSync(0),{code:'EBADF'});
  releaseClose();
  const outcome=await pending;
  assert.ok('parts' in outcome);
  await job.closed;
  assert.equal(closeCalls,1);
  assert.equal(stream.closed,true);
  assert.equal(closeEvents,0);
  assert.deepEqual(NativeMultipartStreamJob.physical(),{jobs:0,streams:0});
  assert.equal(stream._destroy,ReadStream.prototype._destroy);
  assert.equal(Buffer.concat(chunks).toString(),'actual descriptor zero file bytes');
  console.log(JSON.stringify({mode:'close',opened,closeCalls,closed:stream.closed,closeEvents,physical:NativeMultipartStreamJob.physical()}));
} else {
  const outcome=await pending;
  assert.ok('error' in outcome);
  assert.match(outcome.error,/descriptor ownership changed/);
  await job.closed;
  assert.equal(closeCalls,0);
  assert.ok(fstatSync(callerFD).isFile());
  assert.deepEqual(NativeMultipartStreamJob.physical(),{jobs:0,streams:0});
  closeSync(callerFD);
  console.log(JSON.stringify({mode:'reused',opened,closeCalls,callerFD,physical:NativeMultipartStreamJob.physical()}));
}
`
    const { stdout, stderr } = await execute(
      process.execPath,
      ['--input-type=module', '--eval', program],
      { encoding: 'utf8' },
    )
    expect(stderr).toBe('')
    return z
      .object({
        mode: z.string(),
        opened: z.number(),
        closeCalls: z.number(),
        physical: z.object({ jobs: z.number(), streams: z.number() }),
      })
      .parse(JSON.parse(stdout))
  } finally {
    await rm(directory, { recursive: true, force: true })
  }
}

it('retains real descriptor0 closure until its actual OS-close callback settles', async () => {
  expect(await actualZeroDescriptor('close')).toEqual({
    mode: 'close',
    opened: 0,
    closeCalls: 1,
    physical: { jobs: 0, streams: 0 },
  })
})

it('preserves a descriptor0 the caller genuinely closed and reused for another file', async () => {
  expect(await actualZeroDescriptor('reused')).toEqual({
    mode: 'reused',
    opened: 0,
    closeCalls: 0,
    physical: { jobs: 0, streams: 0 },
  })
})
