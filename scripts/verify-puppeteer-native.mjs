// SPDX-License-Identifier: MIT
// Independent test-only oracle: no Puppeteer browser driver is installed or loaded.
import assert from 'node:assert/strict'
import { execFile } from 'node:child_process'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { createReadStream } from 'node:fs'
import { chmod, cp, mkdir, readFile, readdir, symlink, writeFile } from 'node:fs/promises'
import { isAbsolute, resolve } from 'node:path'
import process from 'node:process'
import { pathToFileURL } from 'node:url'
import { promisify } from 'node:util'

import * as ts from 'typescript'
import * as z from 'zod'

const revision = 'd1b9a2162a0113356db5093f055b4a2e1979c6a8'
const root = resolve('.suitecut/puppeteer-native')
const runDirectory = `${root}/runs/${new Date().toISOString().replaceAll(':', '-')}-${process.pid}`
const sourceExecutable = z.string().refine(isAbsolute).parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const sourceSDK = z
  .string()
  .refine(isAbsolute)
  .parse(process.env.SUITECUT_NATIVE_SDK_DIRECTORY ?? resolve('dist'))
const run = promisify(execFile)
const reuseManifestPath = process.env.SUITECUT_PUPPETEER_NATIVE_REUSE_MANIFEST
const reuseManifest = reuseManifestPath
  ? z
      .object({
        revision: z.literal(revision),
        runnerDependencies: z.object({ mocha: z.literal('12.0.3'), expect: z.literal('30.5.2') }),
        inputs: z.array(
          z.object({ path: z.string(), sha256: z.string(), upstream: z.string().optional() }),
        ),
      })
      .parse(JSON.parse(await readFile(reuseManifestPath, 'utf8')))
  : undefined
/**
 * Hash an exact file without keeping a large shared CEF library in memory.
 * @param {string} path
 */
async function hash(path) {
  const digest = createHash('sha256')
  for await (const chunk of createReadStream(path))
    digest.update(z.instanceof(Uint8Array).parse(chunk))
  return digest.digest('hex')
}
/**
 * Record regular files recursively under a known runtime or fixture directory.
 * @param {string} path
 * @returns {Promise<Array<{path:string,sha256:string}>>}
 */
async function inventory(path) {
  const entries = []
  for (const item of await readdir(path, { withFileTypes: true })) {
    const target = resolve(path, item.name)
    if (item.isDirectory()) entries.push(...(await inventory(target)))
    else entries.push({ path: target, sha256: await hash(target) })
  }
  return entries
}
await mkdir(`${runDirectory}/runtime`, { recursive: true })
await cp(sourceExecutable, `${runDirectory}/runtime/suitecut-browser`)
await chmod(`${runDirectory}/runtime/suitecut-browser`, 0o500)
await cp(sourceSDK, `${runDirectory}/sdk`, { recursive: true })
const cefSDK = resolve('.suitecut/cef-sdk')
const cefVersion = z
  .object({ cefVersion: z.string() })
  .parse(JSON.parse(await readFile('native/browser/cef-build.json', 'utf8'))).cefVersion
assert.equal(
  /^#define CEF_VERSION "([^"]+)"/mu.exec(
    await readFile(`${cefSDK}/include/cef_version.h`, 'utf8'),
  )?.[1],
  cefVersion,
)
const runtimeResources = []
for (const directory of ['Release', 'Resources']) {
  runtimeResources.push(...(await inventory(`${cefSDK}/${directory}`)))
  for (const name of await readdir(`${cefSDK}/${directory}`))
    await symlink(`${cefSDK}/${directory}/${name}`, `${runDirectory}/runtime/${name}`)
}
await writeFile(`${root}/package.json`, JSON.stringify({ private: true, type: 'module' }) + '\n')
if (reuseManifest) {
  const lock = reuseManifest.inputs.find((input) => input.path === `${root}/package-lock.json`)
  assert(lock, 'The prior runner lockfile must be recorded')
  assert.equal(await hash(lock.path), lock.sha256, 'The cached runner lockfile must match exactly')
  for (const [name, version] of Object.entries(reuseManifest.runnerDependencies)) {
    const installed = z
      .object({ name: z.literal(name), version: z.literal(version) })
      .parse(JSON.parse(await readFile(`${root}/node_modules/${name}/package.json`, 'utf8')))
    assert.equal(installed.version, version)
  }
} else
  await run(
    'npm',
    [
      'install',
      '--prefix',
      root,
      '--ignore-scripts',
      '--no-audit',
      '--no-fund',
      '--save-exact',
      'mocha@12.0.3',
      'expect@30.5.2',
    ],
    { timeout: 120000 },
  )
const originals = [
  'test/src/evaluation.test.ts',
  'LICENSE',
  'package.json',
  ...[
    'empty.html',
    'global-var.html',
    'one-style.html',
    'one-style.css',
    'tamperable.html',
    'frames/one-frame.html',
    'frames/frame.html',
  ].map((name) => `test/assets/${name}`),
]
const sources = []
for (const original of originals) {
  const target = `${runDirectory}/original/${original}`
  await mkdir(resolve(target, '..'), { recursive: true })
  if (reuseManifest) {
    const cached = reuseManifest.inputs.find((input) => input.upstream === original)
    assert(cached, `Missing exact pinned original: ${original}`)
    assert.equal(await hash(cached.path), cached.sha256, `Changed pinned original: ${original}`)
    await cp(cached.path, target)
    assert.equal(await hash(target), cached.sha256)
  } else
    await run('curl', [
      '--fail',
      '--silent',
      '--show-error',
      '--max-time',
      '30',
      `https://raw.githubusercontent.com/puppeteer/puppeteer/${revision}/${original}`,
      '-o',
      target,
    ])
  sources.push({ upstream: original, path: target, sha256: await hash(target) })
}
await mkdir(`${runDirectory}/tests`, { recursive: true })
const originalTest = await readFile(`${runDirectory}/original/test/src/evaluation.test.ts`, 'utf8')
const compiled = ts.transpileModule(originalTest, {
  fileName: `${runDirectory}/original/test/src/evaluation.test.ts`,
  compilerOptions: {
    module: ts.ModuleKind.ESNext,
    target: ts.ScriptTarget.ES2022,
    inlineSourceMap: true,
    inlineSources: true,
  },
})
await writeFile(`${runDirectory}/tests/evaluation.test.js`, compiled.outputText)
const nativeModule = pathToFileURL(`${runDirectory}/sdk/native.js`).href
await writeFile(
  `${runDirectory}/tests/mocha-utils.js`,
  `
import assert from 'node:assert/strict';
import { createServer } from 'node:http';
import { readFile,writeFile } from 'node:fs/promises';
import { extname,resolve } from 'node:path';
import expect from 'expect';
import { launchNativeBrowser,NativeBrowserContexts } from ${JSON.stringify(nativeModule)};
expect.extend({atLeastOneToContain(actual,values){return{pass:typeof actual==='string'&&values.some(value=>actual.includes(value)),message:()=> 'Expected at least one supplied substring in actual error: '+actual};}});
let source,browser,context,page,server,httpServer;
const routes=new Map(),policies=new Map(),waiters=new Map();
let nativeStderr='';
export async function getTestState(){if(!page)throw new Error('Native test page is not ready');return{page,server,browser};}
export function setupTestBrowserHooks(){
  before(async function(){
    this.timeout(30000);
    source=await launchNativeBrowser({executablePath:${JSON.stringify(`${runDirectory}/runtime/suitecut-browser`)},headless:true,width:1280,height:720,onDiagnostic(message){if(nativeStderr.length<1048576)nativeStderr+=message.slice(0,1048576-nativeStderr.length);}});
    assert.equal(source.cefVersion,${JSON.stringify(cefVersion)});browser=new NativeBrowserContexts(source);
    httpServer=createServer(async(request,response)=>{
      const path=new URL(request.url??'/','http://localhost').pathname;
      const waiting=waiters.get(path);if(waiting){waiters.delete(path);for(const done of waiting)done(request);}
      if(policies.has(path))response.setHeader('Content-Security-Policy',policies.get(path));
      if(routes.has(path)){await routes.get(path)(request,response);return;}
      try{
        const asset=resolve(${JSON.stringify(`${runDirectory}/original/test/assets`)},'.'+path);
        if(!asset.startsWith(${JSON.stringify(`${runDirectory}/original/test/assets/`)}))throw new Error('Asset is outside the fixture root');
        const data=await readFile(asset),extension=extname(asset);
        response.setHeader('Content-Type',({'.html':'text/html; charset=utf-8','.css':'text/css; charset=utf-8','.js':'application/javascript; charset=utf-8'})[extension]??'application/octet-stream');response.end(data);
      }catch{response.statusCode=404;response.end('Not Found');}
    });
    await new Promise((accept,reject)=>{httpServer.once('error',reject);httpServer.listen(0,'0.0.0.0',accept);});
    const address=httpServer.address();if(!address||typeof address==='string')throw new Error('Expected actual HTTP address');
    const PREFIX='http://localhost:'+address.port;
    server={PREFIX,CROSS_PROCESS_PREFIX:'http://127.0.0.1:'+address.port,EMPTY_PAGE:PREFIX+'/empty.html',setCSP(path,policy){policies.set(path,policy);},setRoute(path,handler){routes.set(path,handler);},waitForRequest(path){return new Promise(accept=>{const waiting=waiters.get(path)??[];waiting.push(accept);waiters.set(path,waiting);});}};
  });
  beforeEach(async function(){this.timeout(30000);routes.clear();policies.clear();waiters.clear();context=await browser.newContext();page=await context.newPage();});
  afterEach(async function(){this.timeout(30000);try{await context.close();}finally{page=undefined;context=undefined;}});
  after(async function(){this.timeout(30000);try{await source?.close();}finally{if(httpServer)await new Promise(accept=>httpServer.close(accept));await writeFile(${JSON.stringify(`${runDirectory}/native-stderr.log`)},nativeStderr);}});
}
`,
)
await writeFile(
  `${runDirectory}/tests/utils.js`,
  `
export function html(strings,...values){return strings.reduce((text,part,index)=>text+part+(index<values.length?String(values[index]):''),'');}
export async function attachFrame(page,name,url){await page.evaluate(({name,url})=>new Promise((accept,reject)=>{const frame=document.createElement('iframe');frame.name=name;frame.id=name;frame.src=url;frame.onload=()=>accept(undefined);frame.onerror=reject;document.body.append(frame);}),{name,url});return page.frame({name});}
`,
)
const inputs = [
  ...sources,
  ...(await inventory(`${runDirectory}/sdk`)),
  ...(await inventory(`${runDirectory}/tests`)),
  ...runtimeResources,
  { path: `${root}/package-lock.json`, sha256: await hash(`${root}/package-lock.json`) },
  {
    path: `${runDirectory}/runtime/suitecut-browser`,
    sha256: await hash(`${runDirectory}/runtime/suitecut-browser`),
  },
]
const manifest = {
  revision,
  cefVersion,
  sourceExecutable,
  sourceSDK,
  runDirectory,
  reuseManifest: reuseManifestPath ?? null,
  runnerDependencies: { mocha: '12.0.3', expect: '30.5.2' },
  typescriptVersion: ts.version,
  inputs,
  adaptations: [
    'The entire original evaluation.test.ts and every assertion are unchanged; only TypeScript syntax is transpiled by the generic compiler.',
    'Native fixture hooks create genuine NativeBrowserContexts and NativePage instances. No SDK brands, fake handles, or behavioral aliases are supplied.',
    'The authored Node HTTP fixture serves the exact pinned assets, CSP headers, routes and request waiters; attachFrame uses real DOM iframe creation.',
    'Pure value assertions use pinned generic expect; the authored atLeastOneToContain extension compares actual error text.',
    'Only generic mocha and expect dependencies are installed in the ignored oracle directory; no Puppeteer package or browser driver is installed or imported.',
    'Browser runtime and SDK are immutable per-run copies with pinned CEF resources; originals, generated fixtures, native runtime files and runner lockfile are hashed before and after.',
    'Missing native public methods and differing serialization or error contracts remain actual failed tests; no test cases are omitted.',
    'Explicit reuse verifies the prior revision, exact upstream file SHA256 values, runner lockfile SHA256 and installed pinned generic runner versions before executing unchanged originals; it does not substitute test bodies.',
  ],
}
await writeFile(`${runDirectory}/manifest.json`, JSON.stringify(manifest, null, 2) + '\n')
console.log(`Independent Puppeteer native snapshot: ${runDirectory}`)
let exitCode = 0
let stdout
let stderr
try {
  const result = await run(
    process.execPath,
    [
      `${root}/node_modules/mocha/bin/mocha.js`,
      '--timeout',
      '30000',
      '--reporter',
      'json',
      `${runDirectory}/tests/evaluation.test.js`,
      ...process.argv.slice(2),
    ],
    { timeout: 900000, maxBuffer: 16 * 1024 * 1024 },
  )
  stdout = result.stdout
  stderr = result.stderr
} catch (error) {
  const failure = z
    .object({
      code: z.union([z.number(), z.string()]).optional(),
      stdout: z.string().optional(),
      stderr: z.string().optional(),
    })
    .parse(error)
  exitCode = typeof failure.code === 'number' ? failure.code : 1
  stdout = failure.stdout ?? ''
  stderr = failure.stderr ?? ''
}
await writeFile(`${runDirectory}/runner-stdout.json`, stdout)
await writeFile(`${runDirectory}/runner-stderr.log`, stderr)
const changedInputs = []
for (const input of inputs)
  if ((await hash(input.path)) !== input.sha256) changedInputs.push(input.path)
assert.equal(
  changedInputs.length,
  0,
  'An immutable native runtime or oracle input changed during the proof',
)
let stats
try {
  stats = z.object({ stats: z.record(z.string(), z.unknown()) }).parse(JSON.parse(stdout)).stats
} catch {
  stats = { resultArtifactMissing: true }
}
await writeFile(
  `${runDirectory}/summary.json`,
  JSON.stringify(
    {
      revision,
      cefVersion,
      exitCode,
      stats,
      inputsUnchanged: true,
      changedInputs,
      manifest: `${runDirectory}/manifest.json`,
    },
    null,
    2,
  ) + '\n',
)
await writeFile(`${root}/latest-run.json`, JSON.stringify({ runDirectory }) + '\n')
console.log(JSON.stringify({ exitCode, stats, runDirectory, inputsUnchanged: true }))
process.exitCode = exitCode
