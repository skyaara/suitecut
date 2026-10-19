// SPDX-License-Identifier: MIT
// Execute the actual embedded snapshot/query sources in Chromium, without an
// engine rebuild. The full engine verification also checks reference transport.
import assert from 'node:assert/strict'
import { spawn } from 'node:child_process'
import console from 'node:console'
import { mkdtemp, readFile, rm } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { resolve } from 'node:path'
import process from 'node:process'
import { Writable } from 'node:stream'
import { clearTimeout, setTimeout } from 'node:timers'

import * as z from 'zod'

const chrome = resolve(
  process.env.SUITECUT_TEST_CHROME ?? '.suitecut/agent-comparison/competitor/chrome-linux64/chrome',
)
const profile = await mkdtemp(tmpdir() + '/suitecut-ai-discovery-')
/**
 * Read the exact embedded function.
 * @param {string} path
 */
const source = async (path) => {
  const header = await readFile(path, 'utf8')
  const match = /R"JS\(([\s\S]*)\)JS";/u.exec(header)
  assert(match, 'embedded JavaScript source')
  return match[1]
}
const query = await source('native/shared/dom_query.h')
const snapshot = await source('native/shared/dom_semantic_snapshot.h')
const child = spawn(
  chrome,
  [
    '--headless=new',
    '--disable-gpu',
    '--remote-debugging-pipe',
    '--no-first-run',
    '--user-data-dir=' + profile,
    'about:blank',
  ],
  {
    stdio: ['ignore', 'ignore', 'pipe', 'pipe', 'pipe'],
  },
)
const commandPipe = /** @type {import('node:stream').Writable} */ (child.stdio[3])
assert(commandPipe instanceof Writable)
const exited = new Promise((resolveExit) => child.once('exit', resolveExit))
let id = 0,
  buffer = '',
  diagnostics = ''
/** @type {Map<number, {resolve: (value: unknown) => void, reject: (reason: Error) => void, timer: ReturnType<typeof setTimeout>}>} */
const pending = new Map()
child.stderr.on('data', (chunk) => {
  if (diagnostics.length < 65536) diagnostics += chunk
})
child.stdio[4].on('data', (chunk) => {
  buffer += chunk
  let end
  while ((end = buffer.indexOf('\0')) >= 0) {
    const value = z
      .object({
        id: z.number().optional(),
        error: z.unknown().optional(),
        result: z.unknown().optional(),
      })
      .parse(JSON.parse(buffer.slice(0, end)))
    buffer = buffer.slice(end + 1)
    const entry = pending.get(value.id)
    if (entry) {
      clearTimeout(entry.timer)
      pending.delete(value.id)
      if (value.error) entry.reject(new Error(JSON.stringify(value.error)))
      else entry.resolve(value.result)
    }
  }
})
/**
 * Send a bounded CDP command through the private browser pipe.
 * @param {string} method
 * @param {Record<string, unknown>} [params]
 * @param {string} [sessionId]
 * @returns {Promise<unknown>}
 */
function send(method, params = {}, sessionId) {
  return new Promise((resolveResult, reject) => {
    const command = ++id
    const timer = setTimeout(() => {
      pending.delete(command)
      reject(new Error(method + ' timed out: ' + diagnostics))
    }, 10000)
    pending.set(command, { resolve: resolveResult, reject, timer })
    commandPipe.write(JSON.stringify({ id: command, method, params, sessionId }) + '\0')
  })
}
try {
  const { targetInfos } = z
    .object({ targetInfos: z.array(z.object({ type: z.string(), targetId: z.string() })) })
    .parse(await send('Target.getTargets'))
  const target = targetInfos.find((t) => t.type === 'page')
  assert(target)
  const { sessionId } = z
    .object({ sessionId: z.string() })
    .parse(await send('Target.attachToTarget', { targetId: target.targetId, flatten: true }))
  const evaluated = await send(
    'Runtime.evaluate',
    {
      returnByValue: true,
      expression: `(() => {
      const query=${query},snapshot=${snapshot};
      document.body.innerHTML='<label>Password<input id=password type=password value=secret-one placeholder="Account password"></label><input id=explicit type=password role=textbox aria-label="Explicit password" value=secret-two><div id=slider tabindex=0 aria-label="Custom slider"></div><span id=programmatic tabindex=-1 aria-label="Programmatic focus"></span><div id=click aria-label="Click handler"></div><div id=editor contenteditable=true aria-label="Notes"><span id=editor-child>Note text</span></div><div id=pointer style="cursor:pointer"><span id=pointer-child><span id=pointer-grandchild>Open</span></span></div><div id=shadow></div><button id=container>Wrapped <input type=password role=textbox value=secret-three></button><div hidden><input id=hidden type=password value=secret-four></div>';
      document.querySelector('#click').onclick=()=>{};
      document.querySelector('#shadow').attachShadow({mode:'open'}).innerHTML='<span id=shadow-control tabindex=0 aria-label="Shadow control"></span>';
      const ai=snapshot(document.body,query,{ai:true,captureReferences:true});
      const plain=snapshot(document.body,query,{});
      const entries=ai.entries.map(([node,index])=>({id:ai.elements[index].id,role:node.role,name:node.name,text:node.text,placeholder:node.properties?.placeholder}));
      const standardPasswordMatches=query([{engine:'role',role:'textbox'}],document).filter(e=>e.id==='password').length;
      document.body.innerHTML='<label>Upload <input id=native-labeled type=file><input type=password role=textbox value=SECRET-labeled-password></label><label><input id=native-empty type=file><input type=password role=textbox value=SECRET-only-password></label>';
      const nativeControls=new Map([...document.querySelectorAll('input[type=file]')].map(e=>[e,[{role:'button',name:'SECRET-native-AX-fallback-'+e.id,children:[]}]]));
      const nativeSnapshot=snapshot(document.body,query,{ai:true,captureReferences:true,nativeControls});
      const nativeFiles=nativeSnapshot.entries.filter(([,index])=>nativeSnapshot.elements[index].type==='file').map(([node,index])=>({id:nativeSnapshot.elements[index].id,role:node.role,name:node.name}));
      let bounded=false;
      document.body.innerHTML='<div tabindex=0></div>'.repeat(4200);
      try{snapshot(document.body,query,{ai:true,captureReferences:true})}catch(error){bounded=/limit exceeded/.test(String(error))}
      return {ai:ai.forest,plain,entries,nativeFiles,nativeForest:nativeSnapshot.forest,standardPasswordMatches,bounded};
    })()`,
    },
    sessionId,
  )
  const { result, exceptionDetails } = z
    .object({
      result: z.object({ value: z.unknown().optional() }),
      exceptionDetails: z.unknown().optional(),
    })
    .parse(evaluated)
  assert.equal(exceptionDetails, undefined, JSON.stringify(exceptionDetails))
  const data = z
    .object({
      ai: z.unknown(),
      plain: z.unknown(),
      standardPasswordMatches: z.number(),
      bounded: z.boolean(),
      nativeForest: z.unknown(),
      nativeFiles: z.array(z.object({ id: z.string(), role: z.string(), name: z.string() })),
      entries: z.array(
        z.object({
          id: z.string(),
          role: z.string(),
          name: z.string().optional(),
          text: z.string().optional(),
          placeholder: z.string().optional(),
        }),
      ),
    })
    .parse(result.value)
  /**
   * Find a genuine captured element reference.
   * @param {string} name
   */
  const find = (name) => data.entries.find((entry) => entry.id === name)
  assert.equal(find('password')?.role, 'textbox')
  assert.equal(find('password')?.name, 'Password')
  assert.equal(find('password')?.placeholder, 'Account password')
  assert.equal(find('password')?.text, undefined)
  assert.equal(find('explicit')?.text, undefined)
  for (const name of ['slider', 'programmatic', 'click', 'editor', 'pointer', 'shadow-control'])
    assert.equal(find(name)?.role, 'generic', name)
  for (const name of ['editor-child', 'pointer-child', 'pointer-grandchild', 'hidden'])
    assert.equal(find(name), undefined, name)
  assert(
    !JSON.stringify(data.ai).includes('secret-'),
    'password values excluded from AI text and containing control names',
  )
  assert(
    !JSON.stringify(data.plain).includes('Account password'),
    'standard snapshot does not synthesize password role',
  )
  assert.equal(data.standardPasswordMatches, 0, 'standard role selector semantics unchanged')
  assert.deepEqual(
    data.nativeFiles,
    [
      { id: 'native-labeled', role: 'button', name: 'Upload' },
      { id: 'native-empty', role: 'button', name: '' },
    ],
    'native controls retain genuine refs and replace names, including sanitized empty labels',
  )
  assert(
    !JSON.stringify(data.nativeForest).includes('SECRET'),
    'native AX fallback and embedded password values excluded from AI names',
  )
  assert.equal(data.bounded, true, 'discovery remains bounded')
  console.log(
    JSON.stringify({
      passed: true,
      checks: 21,
      references: data.entries.length,
      nativeFileReferences: data.nativeFiles.length,
      aiBytes: JSON.stringify(data.ai).length,
    }),
  )
} finally {
  await send('Browser.close').catch(() => child.kill('SIGTERM'))
  const deadline = setTimeout(() => child.kill('SIGKILL'), 5000)
  await exited
  clearTimeout(deadline)
  for (const entry of pending.values()) {
    clearTimeout(entry.timer)
    entry.reject(new Error('Browser closed'))
  }
  await rm(profile, { recursive: true, force: true })
}
