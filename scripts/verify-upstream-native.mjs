// SPDX-License-Identifier: MIT
// Test-only runner dependencies and upstream fixtures live in ignored .suitecut.
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import { execFile, spawn } from 'node:child_process'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { createReadStream } from 'node:fs'
import { chmod, cp, mkdir, readFile, readdir, symlink, writeFile } from 'node:fs/promises'
import { isAbsolute, resolve } from 'node:path'
import process from 'node:process'
import { pathToFileURL } from 'node:url'
import { promisify } from 'node:util'

import * as z from 'zod'

const version = '1.64.0'
const revision = '606ecd8b34bac100db32561b6278ffd76e5eb3cd'
const root = resolve('.suitecut/upstream-native')
const runDirectory = `${root}/runs/${new Date().toISOString().replaceAll(':', '-')}-${process.pid}`
const originalExecutable = process.env.SUITECUT_NATIVE_EXECUTABLE
const sdkDirectory = process.env.SUITECUT_NATIVE_SDK_DIRECTORY ?? resolve('dist')
const fixtureHostname = z
  .enum(['127.0.0.1', 'localhost'])
  .parse(process.env.SUITECUT_NATIVE_FIXTURE_HOST ?? '127.0.0.1')
const crossProcessHostname = fixtureHostname === 'localhost' ? '127.0.0.1' : 'localhost'
assert(
  originalExecutable && isAbsolute(originalExecutable),
  'Set SUITECUT_NATIVE_EXECUTABLE to the built absolute CEF executable',
)
assert(isAbsolute(sdkDirectory), 'SUITECUT_NATIVE_SDK_DIRECTORY must be absolute')
const run = promisify(execFile)
const sharedSnapshot =
  z.enum(['0', '1']).parse(process.env.SUITECUT_NATIVE_SHARED_SNAPSHOT ?? '0') === '1'
const executable = sharedSnapshot
  ? originalExecutable
  : `${runDirectory}/native-runtime/suitecut-browser`
/**
 * Hashes a runtime resource without retaining the complete file.
 * @param {string} path
 */
const hashFile = async (path) => {
  const hash = createHash('sha256')
  for await (const chunk of createReadStream(path)) hash.update(z.instanceof(Buffer).parse(chunk))
  return hash.digest('hex')
}
const cases = [
  'page-strict.spec.ts',
  'page-dispatchevent.spec.ts',
  'queryselector.spec.ts',
  'page-evaluate.spec.ts',
  'elementhandle-wait-for-element-state.spec.ts',
  'locator-is-visible.spec.ts',
  'page-fill.spec.ts',
  'page-click.spec.ts',
  'page-mouse.spec.ts',
  'elementhandle-misc.spec.ts',
  'elementhandle-scroll-into-view.spec.ts',
  'elementhandle-select-text.spec.ts',
  'elementhandle-owner-frame.spec.ts',
  'elementhandle-bounding-box.spec.ts',
  'elementhandle-type.spec.ts',
  'elementhandle-convenience.spec.ts',
  'locator-convenience.spec.ts',
  'retarget.spec.ts',
  'elementhandle-click.spec.ts',
  'locator-click.spec.ts',
  'locator-wait-for-function.spec.ts',
  'page-aria-snapshot-json.spec.ts',
  'page-evaluate-handle.spec.ts',
  'jshandle-evaluate.spec.ts',
  'jshandle-json-value.spec.ts',
  'jshandle-properties.spec.ts',
  'jshandle-as-element.spec.ts',
  'jshandle-to-string.spec.ts',
  'frame-evaluate.spec.ts',
  'frame-hierarchy.spec.ts',
  'frame-goto.spec.ts',
  'page-keyboard.spec.ts',
  'page-dialog.spec.ts',
  'page-event-console.spec.ts',
  'workers.spec.ts',
  'page-network-request.spec.ts',
  'page-network-response.spec.ts',
  'page-route.spec.ts',
  'page-expose-function.spec.ts',
  'page-filechooser.spec.ts',
  'page-drag.spec.ts',
  'expect-boolean.spec.ts',
  'expect-builtins.spec.ts',
  'expect-misc.spec.ts',
  'expect-to-have-text.spec.ts',
  'expect-to-have-value.spec.ts',
  'expect-to-have-accessible.spec.ts',
  'to-match-aria-snapshot.spec.ts',
  'page-aria-snapshot.spec.ts',
  'page-check.spec.ts',
  'page-select-option.spec.ts',
  'page-set-input-files.spec.ts',
  'locator-query.spec.ts',
  'selectors-role.spec.ts',
  'selectors-get-by.spec.ts',
  'selectors-register.spec.ts',
  'selectors-css.spec.ts',
  'selectors-text.spec.ts',
  'selectors-misc.spec.ts',
  'page-screenshot.spec.ts',
  'library/pdf.spec.ts',
  'library/geolocation.spec.ts',
  'library/permissions.spec.ts',
  'library/global-fetch.spec.ts',
  'library/global-fetch-cookie.spec.ts',
  'library/browsercontext-fetch.spec.ts',
  'library/chromium/js-coverage.spec.ts',
  'library/chromium/css-coverage.spec.ts',
  'library/browsercontext-storage-state.spec.ts',
  'library/browsercontext-webauthn.spec.ts',
  'library/download.spec.ts',
  'library/downloads-path.spec.ts',
  'library/tracing.spec.ts',
  'library/har.spec.ts',
  'library/browsercontext-har.spec.ts',
]
/** @type {Set<string>} */
const requestedFiles = new Set()
for (const argument of process.argv.slice(2)) {
  const match = /^([\w./-]+\.spec\.ts)(?::\d+(?::\d+)?)?$/u.exec(argument)
  if (!match) continue
  const name = match[1]
  assert(name)
  const matching = cases.filter(
    (entry) => entry === name || entry.endsWith(`/${name}`) || name.endsWith(`/${entry}`),
  )
  assert(matching.length, `Unsupported upstream test filename: ${name}`)
  for (const entry of matching) requestedFiles.add(entry)
}
const dispatchWholeFileOnly =
  process.argv.slice(2).length === 1 &&
  /(?:^|\/)page-dispatchevent\.spec\.ts$/u.test(process.argv[2] ?? '')
const boundingBoxWholeFileOnly =
  process.argv.slice(2).length === 1 &&
  /(?:^|\/)elementhandle-bounding-box\.spec\.ts$/u.test(process.argv[2] ?? '')
const locatorWaitForFunctionWholeFileOnly =
  process.argv.slice(2).length === 1 &&
  /(?:^|\/)locator-wait-for-function\.spec\.ts$/u.test(process.argv[2] ?? '')
const expectedCollectedTests =
  process.env.SUITECUT_NATIVE_EXPECTED_TESTS === undefined
    ? dispatchWholeFileOnly
      ? 17
      : boundingBoxWholeFileOnly
        ? 9
        : locatorWaitForFunctionWholeFileOnly
          ? 10
          : undefined
    : z.coerce.number().int().min(0).max(100000).parse(process.env.SUITECUT_NATIVE_EXPECTED_TESTS)
const assets = [
  'offscreenbuttons.html',
  'empty.html',
  'dom.html',
  'geolocation.html',
  'har.html',
  'har-fulfill.har',
  'har-redirect.har',
  'har-sha1.har',
  'global-var.html',
  'one-style.html',
  'one-style.css',
  'simple.json',
  'simplezip.json',
  'csscoverage/Dosis-Regular.ttf',
  'csscoverage/OFL.txt',
  'csscoverage/involved.html',
  'csscoverage/media.html',
  'csscoverage/multiple.html',
  'csscoverage/simple.html',
  'csscoverage/sourceurl.html',
  'csscoverage/stylesheet1.css',
  'csscoverage/stylesheet2.css',
  'csscoverage/unused.html',
  'jscoverage/eval.html',
  'jscoverage/involved.html',
  'jscoverage/multiple.html',
  'jscoverage/ranges.html',
  'jscoverage/script1.js',
  'jscoverage/script2.js',
  'jscoverage/simple.html',
  'jscoverage/sourceurl.html',
  'jscoverage/unused.html',
  'multi-value-rdn/key.pem',
  'multi-value-rdn/cert.pem',
  'title.html',
  'prefetch.html',
  'prefetch.js',
  'serviceworkers/fetch/sw.html',
  'serviceworkers/fetch/sw.js',
  'serviceworkers/fetch/style.css',
  'serviceworkers/bug-33561/index.html',
  'serviceworkers/bug-33561/sw.js',
  'counter.html',
  'consolelog.html',
  'codicon.css',
  'codicon.ttf',
  'worker/worker.html',
  'worker/worker.js',
  'frames/frameset.html',
  'frames/one-frame.html',
  'frames/frame.html',
  'frames/style.css',
  'frames/script.js',
  'frames/two-frames.html',
  'frames/nested-frames.html',
  'frames/child-redirect.html',
  'frames/redirect-my-parent.html',
  'shadow.html',
  'deep-shadow.html',
  'shadow-dom-link.html',
  'input/animating-button.html',
  'input/button.html',
  'input/checkbox.html',
  'input/rotatedButton.html',
  'input/scrollable.html',
  'input/textarea.html',
  'input/keyboard.html',
  'offscreenbuttons.html',
  'wrappedlink.html',
  'input/select.html',
  'input/fileupload.html',
  'input/fileupload-multi.html',
  'input/folderupload.html',
  'drag-n-drop.html',
  'device-orientation.html',
  'device-motion.html',
  'file-to-upload.txt',
  'file-to-upload-2.txt',
  'file to upload.txt',
  'pptr.png',
  'grid.html',
  ...Array.from({ length: 10 }, (_, digit) => `digits/${digit}.png`),
  'grid-iframe-in-shadow.html',
  'injectedstyle.css',
  'screenshots/canvas.html',
  'screenshots/webgl.html',
  'screenshots/translateZ.html',
  'redirectloop1.html',
  'redirectloop2.html',
  'rotate-z.html',
  'rotate-pseudo.html',
  'rotate-z-shadow-dom.html',
  'web-animation.html',
  'css-transition.html',
  'webfont/webfont.html',
  'webfont/iconfont.woff2',
  'headings.html',
  'chromium-linux.zip',
  'download-blob.html',
  'empty.pdf',
  'to-do-notifications/LICENSE',
  'to-do-notifications/index.html',
  'to-do-notifications/manifest.webapp',
  'to-do-notifications/scripts/todo.js',
  'to-do-notifications/style/style.css',
]
const screenshotBaselines = [
  'canvas-changes-0-chromium.png',
  'canvas-changes-1-chromium.png',
  'canvas-changes-2-chromium.png',
  'grid-cell-1-chromium.png',
  'hide-should-work-chromium.png',
  'mask-color-should-work-chromium.png',
  'mask-should-work-chromium.png',
  'mask-should-work-with-elementhandle-chromium.png',
  'mask-should-work-with-locator-chromium.png',
  'page-screenshot-should-capture-css-transform-1-chromium.png',
  'red-chromium.webp',
  'remove-should-work-chromium.png',
  'screenshot-canvas-chromium.png',
  'screenshot-canvas-text-chromium.png',
  'screenshot-clip-odd-size-chromium.png',
  'screenshot-clip-rect-chromium.png',
  'screenshot-grid-fullpage-chromium.png',
  'screenshot-grid-fullpage-mask-outside-viewport-chromium.png',
  'screenshot-iframe-chromium.png',
  'screenshot-offscreen-clip-chromium.png',
  'screenshot-sanity-chromium.png',
  'screenshot-translateZ-chromium.png',
  'screenshot-web-font-chromium.png',
  'screenshot-webgl-chromium.png',
  'should-capture-css-box-shadow-1-chromium.png',
  'should-mask-in-parallel-1-chromium.png',
  'should-mask-in-parallel-2-chromium.png',
  'should-mask-inside-iframe-chromium.png',
  'should-mask-multiple-elements-chromium.png',
  'transparent-chromium.png',
  'white-chromium.jpg',
]
await mkdir(`${root}/tests/page`, { recursive: true })
await mkdir(`${root}/tests/config`, { recursive: true })
await mkdir(`${root}/assets`, { recursive: true })
await mkdir(runDirectory, { recursive: true })
await mkdir(`${runDirectory}/native-runtime`, { recursive: true })
const executableBytes = await readFile(originalExecutable)
const originalExecutableSha256 = createHash('sha256').update(executableBytes).digest('hex')
if (!sharedSnapshot) {
  await writeFile(executable, executableBytes)
  await chmod(executable, 0o500)
}
const cefSDK = resolve('.suitecut/cef-sdk')
const cefPin = z
  .object({ cefVersion: z.string(), chromiumVersion: z.string() })
  .parse(JSON.parse(await readFile('native/browser/cef-build.json', 'utf8')))
const cefHeader = await readFile(`${cefSDK}/include/cef_version.h`, 'utf8')
assert.equal(
  /^#define CEF_VERSION "([^"]+)"/mu.exec(cefHeader)?.[1],
  cefPin.cefVersion,
  'Runtime snapshot SDK must match the CEF pin',
)
const runtimeResources = z.array(z.object({ path: z.string(), sha256: z.string() })).parse([])
/**
 * Records the original pinned resources linked into the snapshot.
 * @param {string} path
 */
const recordResource = async (path) => {
  const entries = await readdir(path, { withFileTypes: true })
  for (const entry of entries) {
    const file = resolve(path, entry.name)
    if (entry.isDirectory()) await recordResource(file)
    else runtimeResources.push({ path: file, sha256: await hashFile(file) })
  }
}
for (const directory of ['Release', 'Resources']) {
  await recordResource(`${cefSDK}/${directory}`)
  for (const name of await readdir(`${cefSDK}/${directory}`))
    await symlink(`${cefSDK}/${directory}/${name}`, `${runDirectory}/native-runtime/${name}`)
}
await writeFile(
  `${root}/package.json`,
  JSON.stringify({ private: true, type: 'module' }, null, 2) + '\n',
)
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
    `@playwright/test@${version}`,
    'formidable@2.1.5',
    'pngjs@7.0.0',
    'pixelmatch@7.1.0',
  ],
  { env: { ...process.env, PLAYWRIGHT_SKIP_BROWSER_DOWNLOAD: '1' }, timeout: 120000 },
)
const sources = []
const cacheSchema = z.object({
  version: z.literal(version),
  revision: z.literal(revision),
  sources: z.array(
    z.object({
      upstream: z.string(),
      local: z.string(),
      sha256: z.string().regex(/^[a-f0-9]{64}$/u),
    }),
  ),
})
let cachedSources = new Map()
try {
  const latest = z
    .object({ runDirectory: z.string() })
    .parse(JSON.parse(await readFile(`${root}/latest-run.json`, 'utf8')))
  const cache = cacheSchema.parse(
    JSON.parse(await readFile(`${latest.runDirectory}/source-manifest.json`, 'utf8')),
  )
  cachedSources = new Map(cache.sources.map((entry) => [entry.upstream, entry.sha256]))
} catch {
  /* Missing or invalid cache requires a fresh pinned upstream download. */
}
const files = [
  ...cases.map((name) => {
    const path = `tests/${name.startsWith('library/') ? name : `page/${name}`}`
    return [path, path]
  }),
  ...assets.map((name) => [`tests/assets/${name}`, `assets/${name}`]),
  ...screenshotBaselines.map((name) => {
    const path = `tests/page/page-screenshot.spec.ts-snapshots/${name}`
    return [path, path]
  }),
  ['LICENSE', 'UPSTREAM-LICENSE'],
  ['tests/config/errors.ts', 'tests/config/errors.ts'],
  ['tests/config/testserver/cert.pem', 'tests/config/testserver/cert.pem'],
  ['tests/config/testserver/key.pem', 'tests/config/testserver/key.pem'],
]
// Exact public test/asset bytes for the newly collected getter/label family.
const pinnedGetterSources = new Map([
  [
    'tests/page/page-aria-snapshot-json.spec.ts',
    'c2da26b18e5c9b29e67d7c1941ccc5cb38b73ec73882b855be564616884cbb09',
  ],
  [
    'tests/page/locator-wait-for-function.spec.ts',
    'a9867b485d5db606ead982aa45a8332920b1aab0edd09630de9ff431b52869dc',
  ],
  [
    'tests/page/elementhandle-bounding-box.spec.ts',
    '2e2adce9a4637e58b06d7de2c7bda1ee05d108be94d058f52bdfb43f1410bfec',
  ],
  [
    'tests/page/page-strict.spec.ts',
    '89641befba6dccc84682f667e1ac86ea6e5c92e1d34210bdb579041ffef933ae',
  ],
  [
    'tests/page/queryselector.spec.ts',
    '5e25bfb861ece37ba6a266c212b59c3b0667c96dd71964a222db80a5c813810c',
  ],
  [
    'tests/page/elementhandle-convenience.spec.ts',
    'de695b8a38bbb63f34422aa844a67354fd569895aea67e848423c06c5bdc499a',
  ],
  [
    'tests/page/locator-convenience.spec.ts',
    '9c63a184b0eb678bb5bf794ee8509802aa278545f2eb67d33f17bc8cd04f2e9f',
  ],
  [
    'tests/page/retarget.spec.ts',
    '3dce9587f62e8fdcd0e17f957c6cfaba7e72b4d0bb175f0b99147ea3a35298c8',
  ],
  ['tests/assets/dom.html', 'fe36296fd8731283bd43ed37ab62df46bdb317d337cee5afcc42b27329c1c5b5'],
])
// Exact unchanged dispatch tests/assets; no browser implementation is fetched.
const pinnedDispatchSources = new Map([
  [
    'tests/page/page-dispatchevent.spec.ts',
    '0af6f0562a67f391939881030426b6c0c4c0b4d77ba7d88bd64160b75231e36e',
  ],
  [
    'tests/assets/device-orientation.html',
    'f680a4563377fcb62e6aaaae2e70b39a20d4e438e5e126115600a2cdf0e4b184',
  ],
  [
    'tests/assets/device-motion.html',
    'a1092baf5b4ba79cb6b4b91bdaab2bd73425fff07ac8ad117bc1244ed341fee3',
  ],
])
for (const [upstream, local] of files) {
  assert(upstream && local, 'Invalid pinned upstream catalog mapping')
  await mkdir(resolve(root, local, '..'), { recursive: true })
  const destination = resolve(root, local)
  let cached = false
  try {
    cached =
      cachedSources.get(upstream) ===
      createHash('sha256')
        .update(await readFile(destination))
        .digest('hex')
  } catch {
    /* Missing fixture requires a fresh pinned upstream download. */
  }
  if (!cached)
    await run('curl', [
      '--fail',
      '--silent',
      '--show-error',
      '--retry',
      '2',
      '--retry-delay',
      '1',
      '--max-time',
      '30',
      `https://raw.githubusercontent.com/microsoft/playwright/${revision}/${upstream.split('/').map(encodeURIComponent).join('/')}`,
      '-o',
      destination,
    ])
  const sourceSHA256 = createHash('sha256')
    .update(await readFile(destination))
    .digest('hex')
  const pinnedGetterSHA256 = pinnedGetterSources.get(upstream)
  assert(
    !pinnedGetterSHA256 || sourceSHA256 === pinnedGetterSHA256,
    `Pinned upstream getter/label source SHA256 mismatch: ${upstream}`,
  )
  const pinnedDispatchSHA256 = pinnedDispatchSources.get(upstream)
  assert(
    !pinnedDispatchSHA256 || sourceSHA256 === pinnedDispatchSHA256,
    `Pinned upstream dispatch source SHA256 mismatch: ${upstream}`,
  )
  sources.push({ upstream, local, sha256: sourceSHA256 })
}
await writeFile(
  `${runDirectory}/source-manifest.json`,
  JSON.stringify(
    {
      version,
      revision,
      sources,
      adaptations: [
        'Original test files and assertion calls are unchanged.',
        'Pinned inputs can be reused only when their SHA256 matches the preceding pinned source manifest; otherwise they are downloaded again.',
        'DOM matcher implementations are authored native matchers with C++ polling; pure-value assertions use the upstream runner.',
        'pageTest is replaced with an authored native CEF fixture, platform flags, and animation-frame helper.',
        'Server fixture serves the pinned selected assets and authored route/header/request-wait behavior. Generic ws8.21.3, copied and hashed per run with its license, handles actual fixture WebSocket connections; it controls no browser.',
        'SUITECUT_NATIVE_FIXTURE_HOST selects an actual loopback hostname (127.0.0.1 by default or localhost for domain-only WebAuthn RP ceremonies); cross-process origin uses the other actual loopback host. No browser credentials or navigator marker is simulated.',
        'Formidable 2.1.5 is installed only in the ignored test workspace to execute unchanged upstream upload server fixtures.',
        'asset and stripAnsi helpers are authored test-fixture equivalents; fixture files are hashed.',
        'Chromium screenshot baselines are pinned original upstream files and are never regenerated.',
        'Authored image-only utilities use independent PNG/pixelmatch libraries and ffmpeg WebP/JPEG decoding; a test-loader path alias replaces the bare utilsBundle import with those generic decoders. No browser driver utilities are loaded.',
        'An authored bounded Python stdlib ZIP/CRC reader decodes actual native trace entries, raw JSON journals, genuine action titles and indexed stack records; missing browser records or titles are never synthesized. Private upstream runner artifacts-folder naming remains an explicit unsupported fixture.',
        'Frame attachment helpers use DOM operations; missing native frame APIs remain failures.',
        'The runner keeps its existing namespace for fixture bookkeeping; selector methods map to native SDK and every stock browser launch/connect entry point explicitly fails.',
        'Declared errors.TimeoutError fixture mapping uses the genuine exported sealed native constructor; it does not change thrown errors, prototypes, brands or original assertions.',
        'Upstream private before-stable click hooks are translated to the public native action-stage diagnostic hook; the C++ scheduler performs the pause after resolving actionability.',
        'Browser/context fixtures are authored native CEF context managers; no stock browser or fallback.',
        'Persistent browser fixtures launch the native executable with its actual userDataDir option, return its real default context/page, and compose real native context closure with physical helper process closure. Profile files remain owned by the test directory.',
        'The executable and SDK are immutable per-run copies; only version-checked pinned CEF SDK runtime resources are symlinked and rehashed.',
        'One Linux Chromium project, headless software rendering, one worker, zero retries, 30-second test timeout.',
        'Runner tracing, screenshots, and video are disabled; SuiteCut owns its capture.',
        'The test fixture saves the original performance object as window.builtins.performance, matching the upstream test environment.',
      ],
    },
    null,
    2,
  ) + '\n',
)

// Each worker imports an immutable SDK snapshot, independent of later builds.
if (sharedSnapshot) await symlink(sdkDirectory, `${runDirectory}/native-sdk`, 'dir')
else await cp(sdkDirectory, `${runDirectory}/native-sdk`, { recursive: true })
const nativeModule = pathToFileURL(`${runDirectory}/native-sdk/native.js`).href
const nativeContextModule = pathToFileURL(`${runDirectory}/native-sdk/native-context.js`).href
const genericWsDirectory = resolve('node_modules/.pnpm/ws@8.21.3/node_modules/ws')
z.object({ name: z.literal('ws'), version: z.literal('8.21.3') }).parse(
  JSON.parse(await readFile(`${genericWsDirectory}/package.json`, 'utf8')),
)
await cp(genericWsDirectory, `${runDirectory}/generic-ws`, { recursive: true })
const genericWsHashes = []
for (const directory of ['', '/lib'])
  for (const entry of await readdir(`${runDirectory}/generic-ws${directory}`, {
    withFileTypes: true,
  }))
    if (entry.isFile()) {
      const path = `${runDirectory}/generic-ws${directory}/${entry.name}`
      genericWsHashes.push({ path, sha256: await hashFile(path) })
    }
const genericWsModule = pathToFileURL(`${runDirectory}/generic-ws/wrapper.mjs`).href
await writeFile(
  `${root}/tests/page/pageTest.ts`,
  `
import { test as base, expect as runnerExpect } from '@playwright/test';
import assert from 'node:assert/strict';
import { createServer } from 'node:http';
import { createServer as createSecureServer } from 'node:https';
import { once } from 'node:events';
import { WebSocketServer } from ${JSON.stringify(genericWsModule)};
import { readFile, writeFile } from 'node:fs/promises';
import { resolve, extname, dirname } from 'node:path';
import { gzipSync } from 'node:zlib';
import { parseDocument } from 'yaml';
import { launchNativeBrowser, createNativeMatchers, setNativeTestIdAttribute, NativeSelectors, NativePage, NativeLocator, NativeAPIRequestContext, TimeoutError } from ${JSON.stringify(nativeModule)};
import { NativeBrowserContexts } from ${JSON.stringify(nativeContextModule)};
// The unchanged oracle's private lookup option maps only to the authored public
// DNS callback API. Every actual request and callback deadline remains native.
const adaptedNativeRequests=new WeakSet();
const adaptNativeRequest=request=>{
  if(adaptedNativeRequests.has(request))return request;
  adaptedNativeRequests.add(request);
  for(const name of ['fetch','get','head','post','put','patch','delete']){
    const original=request[name].bind(request);
    request[name]=(url,options)=>{
      if(options&&Object.hasOwn(options,'__testHookLookup')){
        const {__testHookLookup,...rest}=options;
        if(Object.hasOwn(rest,'lookup'))throw new Error('Conflicting oracle lookup options');
        options={...rest,lookup:__testHookLookup};
      }
      return original(url,options);
    };
  }
  return request;
};
const authoredMatchers={...createNativeMatchers(),toContainYaml(actual,expected){
  const decode=value=>{if(typeof value!=='string')throw new Error('YAML comparison requires strings');const doc=parseDocument(value);if(doc.errors.length)throw doc.errors[0];return doc.toJS({maxAliasCount:0});};
  let visits=0;
  const contains=(value,wanted,depth=0)=>{
    if(++visits>100000||depth>64)throw new Error('YAML comparison budget exceeded');
    if(Array.isArray(wanted)){
      if(!Array.isArray(value))return false;
      let cursor=0;for(const entry of wanted){while(cursor<value.length&&!contains(value[cursor],entry,depth+1))++cursor;if(cursor===value.length)return false;++cursor;}return true;
    }
    if(wanted&&typeof wanted==='object')return !!value&&typeof value==='object'&&!Array.isArray(value)&&Object.keys(wanted).every(key=>Object.hasOwn(value,key)&&contains(value[key],wanted[key],depth+1));
    return Object.is(value,wanted);
  };
  const pass=contains(decode(actual),decode(expected));return{pass,message:()=> 'Expected YAML to contain the supplied structure. Expected: '+expected+'\\nActual: '+actual};
},async toHaveScreenshot(target,...args){
  if(!(target instanceof NativePage)&&!(target instanceof NativeLocator))throw new Error('Native screenshot matcher requires a native page or locator');
  const named=typeof args[0]==='string'||Array.isArray(args[0]);
  const options=(named?args[1]:args[0])??{};
  const {maxDiffPixels,maxDiffPixelRatio,threshold,...captureOptions}=options;
  const comparison=Object.fromEntries(Object.entries({maxDiffPixels,maxDiffPixelRatio,threshold}).filter(([,value])=>value!==undefined));
  try{const actual=await target.screenshot({animations:'disabled',caret:'hide',scale:'css',timeout:this.timeout??10000,...captureOptions});
    if(named)runnerExpect(actual).toMatchSnapshot(args[0],comparison);else runnerExpect(actual).toMatchSnapshot(comparison);
    return{pass:true,message:()=> 'Native screenshot matched the pinned pixel snapshot'};
  }catch(error){return{pass:false,message:()=>error instanceof Error?error.message:'Native screenshot comparison failed'};}
}};
export const expect = runnerExpect.configure({timeout:10000}).extend(authoredMatchers);
const createNativeFixtureServer = async(use,secure=false)=>{
    const routes = new Map(), policies = new Map(), waiters = new Map(), extraHeaders = new Map(), compressed = new Set(), authentication = new Map();
    const assets = ${JSON.stringify(`${root}/assets`)};
    const serveFile = async (request, response, requestedFile) => {
      const path = new URL(request.url, 'http://fixture').pathname;
      let file = requestedFile===undefined?resolve(assets, '.' + path):resolve(requestedFile);
      try {
        file = requestedFile===undefined?resolve(assets, '.' + decodeURIComponent(path)):resolve(requestedFile);
        if (requestedFile===undefined&&!file.startsWith(assets + '/')) { response.writeHead(403).end(); return; }
        const data = await readFile(file);
        const types={'.html':'text/html; charset=utf-8','.css':'text/css; charset=utf-8','.js':'application/javascript; charset=utf-8','.txt':'text/plain; charset=utf-8','.png':'image/png','.jpg':'image/jpeg','.jpeg':'image/jpeg','.webp':'image/webp','.svg':'image/svg+xml','.woff':'font/woff','.woff2':'font/woff2','.ttf':'font/ttf','.json':'application/json; charset=utf-8'};
        response.setHeader('Content-Type', types[extname(file)] ?? 'application/octet-stream');
        if(compressed.has(path))response.setHeader('Content-Encoding','gzip');
        response.end(compressed.has(path)?gzipSync(data):data);
      } catch { response.writeHead(404,{'Content-Type':'text/plain'}).end(request.method==='HEAD'?undefined:'File not found: '+file); }
    };
    const listener = async (request, response) => {
      const path = new URL(request.url, 'http://fixture').pathname;
      const body=[];let bodyBytes=0;
      request.postBody=new Promise((resolve,reject)=>{
        request.on('data',chunk=>{bodyBytes+=chunk.length;if(bodyBytes>64*1024*1024){request.destroy(new Error('Native fixture request body exceeds64MiB'));return;}body.push(chunk);});
        request.on('end',()=>resolve(Buffer.concat(body)));request.on('error',reject);
      });request.postBody.catch(()=>{});
      for(const key of new Set([request.url,path])){const callbacks=waiters.get(key)||[];waiters.delete(key);for(const callback of callbacks)callback(request);}
      if(authentication.has(path)&&request.headers.authorization!==authentication.get(path)){
        response.writeHead(401,{'WWW-Authenticate':'Basic realm="Native fixture"'}).end('Unauthorized');return;
      }
      if (policies.has(path)) response.setHeader('Content-Security-Policy', policies.get(path));
      for(const [name,value] of Object.entries(extraHeaders.get(path)??{}))response.setHeader(name,value);
      const routeKey=routes.has(request.url)?request.url:path;
      if (routes.has(routeKey)) { await routes.get(routeKey)(request,response); return; }
      await serveFile(request,response);
    };
    const server = secure ? createSecureServer({cert:await readFile(${JSON.stringify(`${root}/tests/config/testserver/cert.pem`)}),key:await readFile(${JSON.stringify(`${root}/tests/config/testserver/key.pem`)})},listener) : createServer(listener);
    const webSockets = new WebSocketServer({server});
    server.listen(0); await once(server,'listening');
    const prefix = (secure?'https':'http')+'://'+${JSON.stringify(fixtureHostname)}+':' + server.address().port;
    try { await use({ PREFIX:prefix, CROSS_PROCESS_PREFIX:prefix.replace(${JSON.stringify(fixtureHostname)},${JSON.stringify(crossProcessHostname)}), HOSTNAME:${JSON.stringify(fixtureHostname)}, EMPTY_PAGE:prefix+'/empty.html', HOST:new URL(prefix).host, PORT:server.address().port,
      onceWebSocketConnection:callback=>webSockets.once('connection',callback),
      setRoute:(path,handler)=>routes.set(path,handler),
      setAuth:(path,username,password)=>authentication.set(path,'Basic '+Buffer.from(username+':'+password).toString('base64')),
      setContent:(path,content,contentType='text/html')=>routes.set(path,(_,response)=>{response.setHeader('Content-Type',contentType);response.end(content);}),
      setRedirect:(path,target)=>routes.set(path,(_,response)=>response.writeHead(302,{location:target}).end()),
      setCSP:(path,policy)=>policies.set(path,policy),
      setExtraHeaders:(path,headers)=>extraHeaders.set(path,{...headers}),
      enableGzip:path=>compressed.add(path),
      serveFile,
      waitForRequest:(path)=>new Promise(resolve=>{const list=waiters.get(path)||[];list.push(resolve);waiters.set(path,list);}),
    }); } finally { for(const client of webSockets.clients)client.terminate();await new Promise(resolve=>webSockets.close(resolve));server.closeAllConnections(); await new Promise(resolve=>server.close(resolve)); }
};
export const test = base.extend({
  _nativeModuleGlobals: [async ({}, use, testInfo) => {
    const oldFilename=globalThis.__filename,oldDirname=globalThis.__dirname;
    globalThis.__filename=testInfo.file;globalThis.__dirname=dirname(testInfo.file);
    try {await use(undefined);} finally {globalThis.__filename=oldFilename;globalThis.__dirname=oldDirname;}
  },{auto:true}],
  nativeSource: [async ({}, use, workerInfo) => {
    let diagnostics='',diagnosticsTruncated=false;
    const source = await launchNativeBrowser({ executablePath: process.env.SUITECUT_NATIVE_EXECUTABLE, width:1280, height:720, headless:true,
      onDiagnostic(message) { const room=1024*1024-diagnostics.length;diagnostics+=message.slice(0,Math.max(0,room));if(message.length>room)diagnosticsTruncated=true; },
    });
    try {
      assert.equal(source.cefVersion,${JSON.stringify(cefPin.cefVersion)},'Executed runtime must match the CEF pin');
      await writeFile(${JSON.stringify(`${runDirectory}/native-browser.json`)},JSON.stringify({cefVersion:source.cefVersion,executable:process.env.SUITECUT_NATIVE_EXECUTABLE,width:1280,height:720,headless:true},null,2)+'\\n');
      await use(source);
    } finally { try { await source.close(); } finally {
      await writeFile(${JSON.stringify(`${runDirectory}/native-stderr.log`)},diagnostics+(diagnosticsTruncated?'\\n<diagnostics truncated at 1 MiB>\\n':''));
      await writeFile(${JSON.stringify(`${runDirectory}/native-stderr-`)}+workerInfo.workerIndex+'.log',diagnostics+(diagnosticsTruncated?'\\n<diagnostics truncated at 1 MiB>\\n':''));
    } }
  },{scope:'worker'}],
  browser: async ({nativeSource}, use) => { const browser=new NativeBrowserContexts(nativeSource);browser.close=()=>nativeSource.close();await use(browser); },
  browserType: async ({},use,testInfo)=>{
    const owned=new Set(),diagnostics=[];
    const launch=async(options)=>{
      const record={text:'',truncated:false};diagnostics.push(record);
      const onDiagnostic=options.onDiagnostic;
      return launchNativeBrowser({...options,onDiagnostic(message){
        const room=1024*1024-record.text.length;record.text+=message.slice(0,Math.max(0,room));
        if(message.length>room)record.truncated=true;if(onDiagnostic)onDiagnostic(message);
      }});
    };
    const type={name:()=> 'chromium',async launch(options={}){
      const source=await launch({executablePath:process.env.SUITECUT_NATIVE_EXECUTABLE,width:1280,height:720,headless:true,...options});owned.add(source);
      const browser=new NativeBrowserContexts(source);browser.close=()=>source.close();return browser;
    },async launchPersistentContext(userDataDir,options={}){
      const source=await launch({executablePath:process.env.SUITECUT_NATIVE_EXECUTABLE,width:1280,height:720,headless:true,...options,userDataDir});owned.add(source);
      const browser=new NativeBrowserContexts(source),context=browser.defaultContext(),close=context.close.bind(context);
      context.close=async(options)=>{try{await close(options);}finally{await source.close();}};
      return context;
    },connect(){throw new Error('External browser connections are unavailable in the native oracle');}};
    try{await use(type);}finally{
      const closed=await Promise.allSettled([...owned].map(source=>source.close()));
      await writeFile(testInfo.outputPath('native-launched-browser-stderr.json'),JSON.stringify(diagnostics,null,2));
      const rejected=closed.find(result=>result.status==='rejected');if(rejected)throw rejected.reason;
    }
  },
  launchPersistent: async({browserType},use,testInfo)=>{await use(async(options={})=>{
    const context=await browserType.launchPersistentContext(testInfo.outputPath('native-user-data-dir'),options);
    return{context,page:context.pages()[0]??await context.newPage()};
  });},
  playwright: async ({playwright,nativeSource}, use) => {
    const restores=[],requests=new Set();
    const replace=(object,key,value)=>{const previous=object[key];object[key]=value;restores.push(()=>object[key]=previous);};
    // Declared fixture surface: use the genuine exported native constructor.
    // Thrown errors, prototypes, original assertions and SDK remain unchanged.
    replace(playwright.errors,'TimeoutError',TimeoutError);
    replace(playwright.selectors,'setTestIdAttribute',setNativeTestIdAttribute);
    replace(playwright.request,'newContext',async(options={})=>{const context=adaptNativeRequest(await NativeAPIRequestContext.create(nativeSource,options));requests.add(context);return context;});
    const selectors=new NativeSelectors(nativeSource);
    replace(playwright.selectors,'register',selectors.register.bind(selectors));
    for(const name of ['chromium','firefox','webkit'])
      for(const method of ['launch','launchPersistentContext','connect','connectOverCDP'])
        if(typeof playwright[name][method]==='function') replace(playwright[name],method,()=>{throw new Error('Stock browser driver entry point is disabled');});
    try { await use(playwright); } finally { try{for(const context of requests)await context.dispose();}finally{for(const restore of restores.reverse())restore();} }
  },
  request: async({playwright,baseURL,extraHTTPHeaders,httpCredentials,ignoreHTTPSErrors,userAgent},use)=>{
    const options=Object.fromEntries(Object.entries({baseURL,extraHTTPHeaders,httpCredentials,userAgent,...(ignoreHTTPSErrors?{ignoreHTTPSErrors}:{} )}).filter(([,value])=>value!==undefined));
    const request=await playwright.request.newContext(options);try{await use(request);}finally{await request.dispose();}
  },
  requestFactory: async({playwright},use)=>{await use(options=>playwright.request.newContext(options));},
  context: async ({browser,ignoreHTTPSErrors}, use) => {
    const context=await browser.newContext({ignoreHTTPSErrors});
    adaptNativeRequest(context.request);
    try { await use(context); } finally { await context.close(); }
  },
  contextFactory: async ({browser}, use) => {
    const contexts=[];
    try { await use(async(options={})=>{ const context=await browser.newContext(options);adaptNativeRequest(context.request);contexts.push(context);return context; }); }
    finally { for(const context of contexts) await context.close(); }
  },
  page: async ({context}, use, testInfo) => {
      setNativeTestIdAttribute('data-testid');
      const page = context.pages()[0] ?? await context.newPage();
      const adapted = new WeakSet();
      const adaptClick = target => {
        if(adapted.has(target))return;
        adapted.add(target);
        const click=target.click.bind(target);
        target.click=(selector,options={})=>{
          const {__testHookBeforeStable,...config}=options;
          return click(selector,{...config,...(__testHookBeforeStable===undefined?{}:{beforeStable:__testHookBeforeStable})});
        };
      };
      const needsClickHookAdapter=testInfo.file.endsWith('/page-click.spec.ts');
      if(needsClickHookAdapter){adaptClick(page);for(const frame of page.frames())adaptClick(frame);page.on('frameattached',adaptClick);}
      const builtins = () => { window.builtins = { Date, performance, setTimeout: setTimeout.bind(window), clearTimeout: clearTimeout.bind(window), requestAnimationFrame: requestAnimationFrame.bind(window), cancelAnimationFrame: cancelAnimationFrame.bind(window) }; };
      await page.addInitScript(builtins);
      await page.evaluate(builtins);
      try { await use(page); } finally { if(needsClickHookAdapter)page.off('frameattached',adaptClick); }
  },
  isLinux: true, isWindows: false, isMac: false,
  isChromium: true, isFirefox: false, isWebKit: false,
  isBidi: false, isAndroid: false, isFrozenWebkit: false,
  isElectron: false, isHeadlessShell: false,
  electronMajorVersion: 0,
  toImpl: async ({}, use) => { await use(() => { throw new Error('Unsupported stock-driver private implementation inspection'); }); },
  headless: true, mode: 'default', platform: 'linux',
  asset: async ({}, use) => { await use(name => resolve(${JSON.stringify(`${root}/assets`)}, name)); },
  browserMajorVersion: ${JSON.stringify(Number(cefPin.chromiumVersion.split('.')[0]))},
  browserVersion: ${JSON.stringify(cefPin.chromiumVersion)},
  nodeVersion: {major:Number(process.versions.node.split('.')[0])},
  channel: 'chromium',
  server: async ({}, use) => createNativeFixtureServer(use),
  httpsServer: async ({}, use) => createNativeFixtureServer(use,true),
});
export const rafraf = async (page, count=2) => page.evaluate(async count=>{for(let i=0;i<count;i++)await new Promise(requestAnimationFrame);},count);
`,
)
await writeFile(
  `${root}/tests/config/utils.ts`,
  `
import assert from 'node:assert/strict';
export {parseTraceRaw,readNativeTraceZip as parseHar} from './native-trace-reader.mjs';
// This authored fixture preserves real TLS warnings in captured diagnostics.
export function suppressCertificateWarning() {}
export function chromiumVersionLessThan(actual,required){const a=actual.split('.').map(Number),b=required.split('.').map(Number);for(let i=0;i<Math.max(a.length,b.length);i++){if((a[i]??0)!==(b[i]??0))return(a[i]??0)<(b[i]??0);}return false;}
export const stripAnsi = (text) => text.replace(/\\x1b\\[[0-?]*[ -/]*[@-~]/g, '');
export function unshift(value) {
  const lines=value.replace(/\\r\\n/g,'\\n').split('\\n');
  while(lines.length&&!lines[0].trim())lines.shift();
  while(lines.length&&!lines[lines.length-1].trim())lines.pop();
  const width=Math.min(...lines.filter(line=>line.trim()).map(line=>line.match(/^ */)[0].length));
  return lines.map(line=>line.slice(Number.isFinite(width)?width:0)).join('\\n');
}
export async function verifyViewport(page, width, height) {
  assert.deepEqual(page.viewportSize(), {width, height});
  assert.deepEqual(await page.evaluate(() => ({width:innerWidth,height:innerHeight})), {width, height});
}
export async function attachFrame(page, name, url) {
  await page.evaluate(({name,url})=>new Promise((resolve,reject)=>{
    const frame=document.createElement('iframe');frame.id=name;frame.name=name;frame.src=url;
    frame.onload=()=>resolve(undefined);frame.onerror=reject;document.body.append(frame);
  }),{name,url});
  return page.frame({name});
}
export async function detachFrame(page,name) {
  await page.evaluate(name=>document.getElementById(name).remove(),name);
}
`,
)
await mkdir(`${root}/packages/playwright-core/lib`, { recursive: true })
await writeFile(
  `${root}/packages/playwright-core/lib/coreBundle.ts`,
  `
import {execFileSync} from 'node:child_process';
import {createServer as createHttpsServer} from 'node:https';
import {createSecureServer as createHttp2Server} from 'node:http2';
import {PNG} from 'pngjs';
export {PNG};
export const getPlaywrightVersion=()=> '1.64.0';export const jpegjs = {decode(buffer){const png=execFileSync('ffmpeg',['-hide_banner','-loglevel','error','-i','pipe:0','-frames:v','1','-threads','2','-f','image2pipe','-c:v','png','pipe:1'],{input:buffer,maxBuffer:192*1024*1024,timeout:30000});return PNG.sync.read(png);}};
export const utils = {
  createHttpsServer,
  createHttp2Server,
  isLosslessWebp(buffer) {
    if (buffer.toString('ascii',0,4)!=='RIFF'||buffer.toString('ascii',8,12)!=='WEBP') throw new Error('Invalid WebP fixture input');
    for(let offset=12;offset+8<=buffer.length;) {
      const kind=buffer.toString('ascii',offset,offset+4), length=buffer.readUInt32LE(offset+4);
      if(kind==='VP8L') return true;
      if(kind==='VP8 ') return false;
      offset += 8+length+(length%2);
    }
    throw new Error('Missing WebP image chunk');
  },
  decodeWebp(buffer) {
    const png=execFileSync('ffmpeg',['-hide_banner','-loglevel','error','-i','pipe:0','-frames:v','1','-threads','2','-f','image2pipe','-c:v','png','pipe:1'],{input:buffer,maxBuffer:192*1024*1024,timeout:30000});
    return PNG.sync.read(png);
  },
};
`,
)
await writeFile(
  `${root}/tests/config/comparator.ts`,
  `
import {PNG} from 'pngjs';
import pixelmatch from 'pixelmatch';
export function comparePNGs(first, second, options={}) {
  const a=PNG.sync.read(first),b=PNG.sync.read(second);
  if(a.width!==b.width||a.height!==b.height) return {error:'Image dimensions differ'};
  const different=pixelmatch(a.data,b.data,null,a.width,a.height,{threshold:0.2});
  return different <= (options.maxDiffPixels ?? 0) ? null : {different};
}
`,
)
await writeFile(
  `${root}/tests/config/testserver/index.ts`,
  `
import {readFile} from 'node:fs/promises';
export class TestServer {
  static async certOptions(){return{cert:await readFile(${JSON.stringify(`${root}/tests/config/testserver/cert.pem`)}),key:await readFile(${JSON.stringify(`${root}/tests/config/testserver/key.pem`)})};}
}
`,
)
await cp('scripts/native-trace-reader.mjs', `${root}/tests/config/native-trace-reader.mjs`)
await mkdir(`${root}/packages/utils/third_party`, { recursive: true })
await writeFile(
  `${root}/packages/utils/third_party/extractZip.ts`,
  `import {mkdir,writeFile} from 'node:fs/promises';
import {dirname,resolve,sep} from 'node:path';
import {readNativeTraceZip} from '../../../tests/config/native-trace-reader.mjs';
export async function extractZip(path,{dir}) {
  const root=resolve(dir);await mkdir(root,{recursive:true});
  for(const[name,bytes]of await readNativeTraceZip(path)){
    const target=resolve(root,name);if(!target.startsWith(root+sep))throw new Error('ZIP resource escapes destination');
    await mkdir(dirname(target),{recursive:true});await writeFile(target,bytes,{flag:'wx'});
  }
}
`,
)
await mkdir(`${root}/packages/playwright/src/isomorphic`, { recursive: true })
await writeFile(
  `${root}/packages/playwright/src/isomorphic/folders.ts`,
  `
export function artifactsFolderName(){throw new Error('Upstream private runner artifacts naming has no native fixture contract');}
`,
)
await writeFile(
  `${root}/tsconfig.native.json`,
  JSON.stringify(
    {
      compilerOptions: {
        baseUrl: '.',
        paths: {
          'playwright-core/lib/utilsBundle': ['./packages/playwright-core/lib/coreBundle.ts'],
        },
      },
    },
    null,
    2,
  ) + '\n',
)
await writeFile(
  `${root}/tests/config/browserTest.ts`,
  `
export {test as browserTest,test as contextTest,test as playwrightTest,expect} from '../page/pageTest';
`,
)
await writeFile(
  `${root}/playwright.config.ts`,
  `
import {defineConfig} from '@playwright/test';
export default defineConfig({tsconfig:${JSON.stringify(`${root}/tsconfig.native.json`)},testDir:'./tests',workers:1,retries:0,timeout:30000,
  snapshotPathTemplate:${JSON.stringify(`${root}/tests/page/{testFileName}-snapshots/{arg}-chromium{ext}`)},updateSnapshots:'none',
  outputDir:${JSON.stringify(`${runDirectory}/test-results`)},reporter:[['line'],['json',{outputFile:${JSON.stringify(`${runDirectory}/results.json`)}}]],
  use:{browserName:'chromium',trace:'off',video:'off',screenshot:'off'},
});
`,
)
const executableSha256 = createHash('sha256')
  .update(await readFile(executable))
  .digest('hex')
const sdkHashes = []
for (const name of (await readdir(`${runDirectory}/native-sdk`))
  .filter((name) => name.endsWith('.js'))
  .sort())
  sdkHashes.push({
    name,
    sha256: createHash('sha256')
      .update(await readFile(`${runDirectory}/native-sdk/${name}`))
      .digest('hex'),
  })
const fixtureHashes = []
for (const name of [
  'tests/page/pageTest.ts',
  'tests/config/utils.ts',
  'tests/config/native-trace-reader.mjs',
  'packages/playwright/src/isomorphic/folders.ts',
  'tsconfig.native.json',
  'tests/config/comparator.ts',
  'tests/config/browserTest.ts',
  'tests/config/testserver/index.ts',
  'packages/playwright-core/lib/coreBundle.ts',
  'packages/utils/third_party/extractZip.ts',
  'playwright.config.ts',
])
  fixtureHashes.push({
    name,
    sha256: createHash('sha256')
      .update(await readFile(`${root}/${name}`))
      .digest('hex'),
  })
await writeFile(
  `${runDirectory}/fixture-manifest.json`,
  JSON.stringify(
    {
      executable,
      executableSha256,
      originalExecutable,
      originalExecutableSha256,
      originalSDKDirectory: sdkDirectory,
      sharedSnapshot,
      fixtureHostname,
      crossProcessHostname,
      cefVersion: cefPin.cefVersion,
      runtimeResources,
      sdkHashes,
      fixtureHashes,
      genericWsVersion: '8.21.3',
      genericWsHashes,
      platform: process.platform,
      architecture: process.arch,
      node: process.version,
      selectedArguments: process.argv.slice(2),
      requestedFiles: [...requestedFiles],
      expectedCollectedTests,
    },
    null,
    2,
  ) + '\n',
)
console.log(`Running unchanged upstream tests at ${revision} against ${executable}`)
const child = spawn(
  process.execPath,
  [
    `${root}/node_modules/@playwright/test/cli.js`,
    'test',
    '--config',
    `${root}/playwright.config.ts`,
    ...process.argv.slice(2),
  ],
  {
    stdio: 'inherit',
    env: {
      ...process.env,
      SUITECUT_NATIVE_EXECUTABLE: executable,
      PLAYWRIGHT_SKIP_BROWSER_DOWNLOAD: '1',
    },
  },
)
const completion = new Promise((resolve, reject) => {
  child.once('error', reject)
  child.once('close', (code) => resolve(code ?? 1))
})
let code = z
  .number()
  .int()
  .parse(await completion)
const runnerExitCode = code
const executableUnchanged =
  executableSha256 ===
  createHash('sha256')
    .update(await readFile(executable))
    .digest('hex')
const changedInputs = []
for (const entry of [...runtimeResources, ...genericWsHashes]) {
  if ((await hashFile(entry.path)) !== entry.sha256) changedInputs.push(entry.path)
}
for (const entry of sources) {
  const hash = createHash('sha256')
    .update(await readFile(resolve(root, entry.local)))
    .digest('hex')
  if (hash !== entry.sha256) changedInputs.push(entry.local)
}
for (const entry of fixtureHashes) {
  const hash = createHash('sha256')
    .update(await readFile(resolve(root, entry.name)))
    .digest('hex')
  if (hash !== entry.sha256) changedInputs.push(entry.name)
}
for (const entry of sdkHashes) {
  const hash = createHash('sha256')
    .update(await readFile(`${runDirectory}/native-sdk/${entry.name}`))
    .digest('hex')
  if (hash !== entry.sha256) changedInputs.push(`native-sdk/${entry.name}`)
}
if (!executableUnchanged || changedInputs.length) {
  console.error(
    'Verification inputs changed during the run; this run cannot establish a build result',
    { executableUnchanged, changedInputs },
  )
  code = 1
}
try {
  const result = z
    .object({
      suites: z.array(z.json()),
      stats: z
        .object({
          expected: z.number(),
          skipped: z.number(),
          unexpected: z.number(),
          flaky: z.number(),
          duration: z.number(),
        })
        .passthrough(),
    })
    .parse(JSON.parse(await readFile(`${runDirectory}/results.json`, 'utf8')))
  /** @type {Map<string, number>} */
  const collectedFiles = new Map()
  const suiteShape = z.object({
    file: z.string(),
    specs: z.array(z.object({ tests: z.array(z.json()) })).default([]),
    suites: z.array(z.json()).default([]),
  })
  /**
   * Counts actual reporter declarations without changing their outcomes.
   * @param {import('../src/untrusted.js').UntrustedInput} value
   * @param {number} depth
   */
  const collect = (value, depth) => {
    assert(depth <= 128, 'Upstream reporter suite nesting limit exceeded')
    const suite = suiteShape.parse(value)
    const count = suite.specs.reduce((total, spec) => total + spec.tests.length, 0)
    if (count)
      collectedFiles.set(suite.file, z.number().parse(collectedFiles.get(suite.file) ?? 0) + count)
    for (const child of suite.suites) collect(child, depth + 1)
  }
  for (const suite of result.suites) collect(suite, 0)
  let collectedTests = 0
  for (const count of collectedFiles.values()) collectedTests += z.number().parse(count)
  const collectionErrors = []
  for (const name of requestedFiles) {
    if (![...collectedFiles.keys()].some((file) => file === name || file.endsWith(`/${name}`)))
      collectionErrors.push(`Requested upstream file collected no tests: ${name}`)
  }
  if (expectedCollectedTests !== undefined && collectedTests !== expectedCollectedTests)
    collectionErrors.push(
      `Expected ${expectedCollectedTests} collected tests; reporter declared ${collectedTests}`,
    )
  if (collectionErrors.length) {
    code = 1
    console.error('Upstream collection scope mismatch:', collectionErrors)
  }
  await writeFile(
    `${runDirectory}/summary.json`,
    JSON.stringify(
      {
        version,
        revision,
        executable,
        executableSha256,
        originalExecutable,
        originalExecutableSha256,
        exitCode: code,
        runnerExitCode,
        interrupted: runnerExitCode === 130,
        stats: result.stats,
        collectedTests,
        collectedFiles: Object.fromEntries(collectedFiles),
        expectedCollectedTests,
        collectionErrors,
        sourceManifest: `${runDirectory}/source-manifest.json`,
        fixtureManifest: `${runDirectory}/fixture-manifest.json`,
        executableUnchanged,
        inputsUnchanged: !changedInputs.length,
        changedInputs,
      },
      null,
      2,
    ) + '\n',
  )
  console.log(JSON.stringify(result.stats))
  await writeFile(`${root}/latest-run.json`, JSON.stringify({ runDirectory }, null, 2) + '\n')
} catch (error) {
  code = 1
  console.error('No readable upstream result artifact:', error)
}
process.exitCode = code
