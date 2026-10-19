// SPDX-License-Identifier: MIT
// Independent WPT WebDriver-classic oracle: unchanged assertions and native wire adapter.
/* global fetch */
import assert from 'node:assert/strict'
import { execFile, spawn } from 'node:child_process'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { createReadStream } from 'node:fs'
import { chmod, cp, mkdir, readFile, readdir, symlink, writeFile } from 'node:fs/promises'
import { isAbsolute, resolve } from 'node:path'
import process from 'node:process'
import { setTimeout } from 'node:timers/promises'
import { promisify } from 'node:util'

import * as z from 'zod'

const revision = 'd1c5cb6aacd9238e1cd6b999a8507a198c7d32fb'
const root = resolve('.suitecut/webdriver-native')
const runDirectory = `${root}/runs/${new Date().toISOString().replaceAll(':', '-')}-${process.pid}`
const sourceExecutable = z.string().refine(isAbsolute).parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const sourceSDK = z
  .string()
  .refine(isAbsolute)
  .parse(process.env.SUITECUT_NATIVE_SDK_DIRECTORY ?? resolve('dist'))
const run = promisify(execFile)
/**
 * Hash exact input bytes without retaining a large native library.
 * @param {string} path
 */
async function hash(path) {
  const digest = createHash('sha256')
  for await (const chunk of createReadStream(path))
    digest.update(z.instanceof(Uint8Array).parse(chunk))
  return digest.digest('hex')
}
/**
 * Record immutable files recursively.
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
const sharedSnapshot =
  z.enum(['0', '1']).parse(process.env.SUITECUT_NATIVE_SHARED_SNAPSHOT ?? '0') === '1'
if (sharedSnapshot) {
  await symlink(sourceExecutable, `${runDirectory}/runtime/suitecut-browser`)
  await symlink(sourceSDK, `${runDirectory}/sdk`, 'dir')
} else {
  await cp(sourceExecutable, `${runDirectory}/runtime/suitecut-browser`)
  await chmod(`${runDirectory}/runtime/suitecut-browser`, 0o500)
  await cp(sourceSDK, `${runDirectory}/sdk`, { recursive: true })
}
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
const oracleScope = z
  .enum([
    'basic',
    'clear',
    'keys',
    'frames',
    'lifecycle',
    'newwindows',
    'keysbehavior',
    'keyscroll',
    'parentframes',
  ])
  .parse(process.env.SUITECUT_NATIVE_WEBDRIVER_SCOPE ?? 'basic')
const cases =
  oracleScope === 'keyscroll'
    ? ['webdriver/tests/classic/element_send_keys/scroll_into_view.py']
    : oracleScope === 'parentframes'
      ? ['webdriver/tests/classic/switch_to_parent_frame/switch.py']
      : oracleScope === 'keysbehavior'
        ? ['form_controls.py', 'content_editable.py', 'interactability.py'].map(
            (name) => `webdriver/tests/classic/element_send_keys/${name}`,
          )
        : oracleScope === 'newwindows'
          ? [
              'webdriver/tests/classic/new_window/new.py',
              'webdriver/tests/classic/new_window/new_tab.py',
              'webdriver/tests/classic/new_window/new_window.py',
            ]
          : oracleScope === 'lifecycle'
            ? [
                'webdriver/tests/classic/close_window/close.py',
                'webdriver/tests/classic/delete_session/delete.py',
              ]
            : oracleScope === 'clear'
              ? ['webdriver/tests/classic/element_clear/clear.py']
              : oracleScope === 'keys'
                ? [
                    'webdriver/tests/classic/element_send_keys/send_keys.py',
                    'webdriver/tests/classic/element_send_keys/events.py',
                  ]
                : oracleScope === 'frames'
                  ? [
                      'webdriver/tests/classic/switch_to_frame/switch_number.py',
                      'webdriver/tests/classic/switch_to_frame/switch_webelement.py',
                    ]
                  : [
                      ...['get_title', 'get_current_url'].map(
                        (name) => `webdriver/tests/classic/${name}/get.py`,
                      ),
                      'webdriver/tests/classic/element_click/click.py',
                      'webdriver/tests/classic/get_element_tag_name/get.py',
                      'webdriver/tests/classic/is_element_selected/selected.py',
                      'webdriver/tests/classic/is_element_enabled/enabled.py',
                      'webdriver/tests/classic/get_element_css_value/get.py',
                    ]
const originals = [
  'LICENSE.md',
  ...cases,
  ...(oracleScope === 'newwindows'
    ? ['webdriver/tests/classic/new_window/__init__.py', 'webdriver/tests/support/sync.py']
    : []),
  ...(oracleScope === 'lifecycle' || oracleScope === 'parentframes'
    ? ['frames.html', 'subframe.html', 'deleteframe.html', 'beforeunload.html', 'default.html'].map(
        (name) => `webdriver/tests/support/html/${name}`,
      )
    : []),
  ...(oracleScope === 'keyscroll'
    ? [
        'webdriver/tests/support/classic/helpers.py',
        'webdriver/tests/support/defaults.py',
        'webdriver/tests/support/helpers.py',
        'webdriver/tests/support/sync.py',
      ]
    : []),
  'webdriver/tests/support/classic/asserts.py',
  'webdriver/tests/support/dom.py',
  'webdriver/tests/classic/is_element_enabled/__init__.py',
  ...(oracleScope === 'clear' ? ['webdriver/tests/classic/element_clear/__init__.py'] : []),
  ...(oracleScope === 'keys'
    ? [
        'webdriver/tests/classic/element_send_keys/__init__.py',
        'webdriver/tests/classic/element_send_keys/conftest.py',
      ]
    : []),
]
const sourceInputManifest = process.env.SUITECUT_NATIVE_WEBDRIVER_INPUT_MANIFEST
const pinnedInputs = sourceInputManifest
  ? z
      .object({
        revision: z.literal(revision),
        inputs: z.array(
          z.object({
            upstream: z.string().optional(),
            path: z.string().refine(isAbsolute),
            sha256: z.string().regex(/^[a-f0-9]{64}$/),
          }),
        ),
      })
      .parse(JSON.parse(await readFile(sourceInputManifest, 'utf8'))).inputs
  : undefined
const executableDependencies = z
  .array(z.string().refine(isAbsolute))
  .parse(JSON.parse(process.env.SUITECUT_NATIVE_EXECUTABLE_DEPENDENCIES ?? '[]'))
const sources = []
for (const original of originals) {
  const target = `${runDirectory}/original/${original}`
  await mkdir(resolve(target, '..'), { recursive: true })
  if (pinnedInputs) {
    const pinned = pinnedInputs.find((item) => item.upstream === original)
    assert(pinned, `Pinned original input absent from immutable manifest: ${original}`)
    assert.equal(
      await hash(pinned.path),
      pinned.sha256,
      `Pinned original input changed: ${original}`,
    )
    await cp(pinned.path, target)
    assert.equal(await hash(target), pinned.sha256)
  } else {
    await run('curl', [
      '--fail',
      '--silent',
      '--show-error',
      '--max-time',
      '30',
      `https://raw.githubusercontent.com/web-platform-tests/wpt/${revision}/${original}`,
      '-o',
      target,
    ])
  }
  sources.push({ upstream: original, path: target, sha256: await hash(target) })
}
const runner = `${root}/runner/bin/python`
try {
  await readFile(runner)
} catch {
  await run('python3', ['-m', 'venv', `${root}/runner`])
  await run(
    runner,
    [
      '-m',
      'pip',
      'install',
      '--disable-pip-version-check',
      'pytest==8.4.2',
      'iniconfig==2.3.0',
      'packaging==25.0',
      'pluggy==1.6.0',
      'pygments==2.19.2',
    ],
    { maxBuffer: 1048576 },
  )
}
const dependencies = (await run(runner, ['-m', 'pip', 'freeze'])).stdout
await writeFile(`${runDirectory}/runner-dependencies.txt`, dependencies)
await mkdir(`${runDirectory}/original/webdriver/tests/support/classic`, { recursive: true })
for (const path of ['tests', 'tests/support', 'tests/support/classic'])
  await writeFile(`${runDirectory}/original/webdriver/${path}/__init__.py`, '')
await mkdir(`${runDirectory}/original/webdriver/webdriver`, { recursive: true })
await writeFile(
  `${runDirectory}/original/webdriver/webdriver/__init__.py`,
  `
# Authored standard wire references; no external driver implementation.
import builtins
class WebDriverException(Exception):
    pass
class NoSuchAlertException(WebDriverException):
    pass
class NoSuchElementException(WebDriverException):
    pass
class WebElement:
    identifier = "element-6066-11e4-a52e-4f735466cecf"
    def __init__(self, session, element_id):
        self.session, self.id = session, element_id
    def __eq__(self, other):
        return isinstance(other, type(self)) and other.session is self.session and other.id == self.id
    def property(self, name):
        response = self.session.transport.send('GET', f'session/{self.session.session_id}/element/{self.id}/property/{name}')
        assert response.status == 200, response.body
        return response.body['value']
    def attribute(self, name):
        response=self.session.transport.send('GET',f'session/{self.session.session_id}/element/{self.id}/attribute/{name}')
        assert response.status==200,response.body
        return response.body['value']
    def send_keys(self, text):
        response = self.session.transport.send('POST', f'session/{self.session.session_id}/element/{self.id}/value', {'text':text})
        assert response.status == 200, response.body
    @builtins.property
    def text(self):
        return self.session._read(f'element/{self.id}/text')
    @builtins.property
    def rect(self):
        response = self.session.transport.send('GET', f'session/{self.session.session_id}/element/{self.id}/rect')
        assert response.status == 200, response.body
        return response.body['value']
    @builtins.property
    def shadow_root(self):
        response = self.session.transport.send('GET', f'session/{self.session.session_id}/element/{self.id}/shadow')
        assert response.status == 200, response.body
        return ShadowRoot(self.session, response.body['value'][ShadowRoot.identifier])
    def click(self):
        response = self.session.transport.send('POST', f'session/{self.session.session_id}/element/{self.id}/click')
        assert response.status == 200, response.body
class ShadowRoot(WebElement):
    identifier = 'shadow-6066-11e4-a52e-4f735466cecf'
`,
)
await writeFile(
  `${runDirectory}/original/webdriver/webdriver/error.py`,
  'from . import WebDriverException, NoSuchAlertException, NoSuchElementException\nclass NoSuchWindowException(WebDriverException): pass\nclass InvalidSessionIdException(WebDriverException): pass\nclass TimeoutException(WebDriverException):\n    def __init__(self,message): super().__init__(message)\n',
)
await writeFile(
  `${runDirectory}/original/webdriver/webdriver/transport.py`,
  `
# Authored generic HTTP response parser, no external driver code.
import json, urllib.error, urllib.request
class HTTPWireProtocol:
    def __init__(self, host, port):
        self.endpoint=f'http://{host}:{port}'
    def send(self, method, path, body=None):
        request=urllib.request.Request(self.endpoint+'/'+path.lstrip('/'),data=json.dumps(body).encode() if body is not None else None,method=method,headers={'Content-Type':'application/json'})
        try: response=urllib.request.urlopen(request,timeout=15)
        except urllib.error.HTTPError as error: response=error
        with response: return Response.from_http(response)
class Response:
    @staticmethod
    def from_http(response):
        from types import SimpleNamespace
        payload=json.loads(response.read().decode())
        return SimpleNamespace(status=response.status,body=payload,headers=dict(response.headers.items()))
`,
)
await writeFile(
  `${runDirectory}/original/webdriver/webdriver/protocol.py`,
  'import json\nEncoder=json.JSONEncoder\nDecoder=json.JSONDecoder\n',
)
await writeFile(
  `${runDirectory}/original/webdriver/conftest.py`,
  `
import base64, html, json, os, sys, urllib.error, urllib.request, urllib.parse
from pathlib import Path
from types import SimpleNamespace
import pytest
from contextlib import contextmanager
sys.path.insert(0, str(Path(__file__).parent))
from webdriver import WebElement, error
class Transport:
    def __init__(self, endpoint):
        self.endpoint = endpoint
    def close(self):
        # urllib requests use distinct actual HTTP connections; no idle connection is retained.
        pass
    def send(self, method, path, body=None, **protocol_options):
        def encode(value):
            if isinstance(value, WebElement):
                return {value.identifier:value.id}
            raise TypeError(type(value).__name__)
        data = json.dumps(body, default=encode).encode() if body is not None else None
        request = urllib.request.Request(self.endpoint + '/' + path.lstrip('/'), data=data, method=method, headers={'Content-Type':'application/json'})
        try:
            response = urllib.request.urlopen(request, timeout=15)
        except urllib.error.HTTPError as error:
            response = error
        with response:
            def decode(value):
                if isinstance(value, dict) and WebElement.identifier in value:
                    return WebElement(self.session, value[WebElement.identifier])
                if isinstance(value, dict):
                    return {key:decode(item) for key,item in value.items()}
                if isinstance(value, list):
                    return [decode(item) for item in value]
                return value
            payload = json.loads(response.read().decode())
            if path.endswith('/execute/sync'):
                payload = decode(payload)
            return SimpleNamespace(status=response.status, body=payload, headers=dict(response.headers.items()), error=payload.get('value', {}).get('error') if isinstance(payload.get('value'), dict) else None)
class Session:
    def __init__(self, endpoint):
        self.transport = Transport(endpoint)
        self.transport.session = self
        self.session_id = None
        self.find = SimpleNamespace(css=self._css)
        self.window = SimpleNamespace(close=self._close_window)
        self.alert = Alert(self)
    def start(self):
        if self.session_id is None:
            response = self.transport.send('POST', 'session', {'capabilities':{'alwaysMatch':{}}})
            assert response.status == 200, response.body
            self.session_id = response.body['value']['sessionId']
            self.capabilities=response.body['value']['capabilities']
    def _css(self, selector, all=True):
        endpoint='elements' if all else 'element'
        response = self.transport.send('POST', f'session/{self.session_id}/{endpoint}', {'using':'css selector','value':selector})
        assert response.status == 200, response.body
        if all:
            return [WebElement(self,value[WebElement.identifier]) for value in response.body['value']]
        return WebElement(self, response.body['value'][WebElement.identifier])
    def end(self):
        # Original tests call this only after the remote end has deleted the actual session.
        self.session_id=None
    def _read(self, suffix):
        response=self.transport.send('GET',f'session/{self.session_id}/'+suffix)
        if response.status != 200:
            code=response.body['value']['error']
            exception={'invalid session id':error.InvalidSessionIdException,'no such window':error.NoSuchWindowException,'no such element':error.NoSuchElementException}.get(code,error.WebDriverException)
            raise exception(response.body['value']['message'])
        return response.body['value']
    @property
    def active_element(self):
        value=self._read('element/active')
        return WebElement(self,value[WebElement.identifier])
    @property
    def handles(self):
        return self._read('window/handles')
    def new_window(self, type_hint='tab'):
        response = self.transport.send('POST', f'session/{self.session_id}/window/new', {'type':type_hint})
        assert response.status == 200, response.body
        return response.body['value']['handle']
    def _close_window(self):
        response = self.transport.send('DELETE', f'session/{self.session_id}/window')
        assert response.status == 200, response.body
        return response.body['value']
    @property
    def window_handle(self):
        response = self.transport.send('GET', f'session/{self.session_id}/window')
        assert response.status == 200, response.body
        return response.body['value']
    @window_handle.setter
    def window_handle(self, value):
        response = self.transport.send('POST', f'session/{self.session_id}/window', {'handle':value})
        assert response.status == 200, response.body
    def switch_to_frame(self, element):
        response = self.transport.send('POST', f'session/{self.session_id}/frame', {'id':element})
        assert response.status == 200, response.body
    def switch_to_parent_frame(self):
        response = self.transport.send('POST', f'session/{self.session_id}/frame/parent')
        assert response.status == 200, response.body
    @property
    def url(self):
        return self.transport.send('GET', f'session/{self.session_id}/url').body['value']
    @url.setter
    def url(self, value):
        response = self.transport.send('POST', f'session/{self.session_id}/url', {'url':value})
        assert response.status == 200, response.body
    def execute_script(self, script, args=()):
        response = self.transport.send('POST', f'session/{self.session_id}/execute/sync', {'script':script, 'args':list(args)})
        assert response.status == 200, response.body
        return response.body['value']
class Alert:
    def __init__(self,session): self.session=session
    @property
    def text(self): return self.session._read('alert/text')
@pytest.fixture
def configuration():
    address=urllib.parse.urlsplit(os.environ['SUITECUT_WEBDRIVER_ENDPOINT'])
    return {'host':address.hostname,'port':address.port}
@pytest.fixture
def url():
    return lambda path: os.environ['SUITECUT_WEBDRIVER_ENDPOINT']+path
@pytest.fixture
def session():
    value = Session(os.environ['SUITECUT_WEBDRIVER_ENDPOINT'])
    value.start()
    try:
        yield value
    finally:
        if value.session_id is not None:
            response = value.transport.send('DELETE', f'session/{value.session_id}')
            assert response.status == 200, response.body
@pytest.fixture
def inline(session):
    def create(document, doctype='html'):
        mime = {'html':'text/html','xml':'application/xml','xhtml':'application/xhtml+xml'}[doctype]
        if doctype == 'xhtml':
            document = '<html xmlns="http://www.w3.org/1999/xhtml"><head/><body>' + document + '</body></html>'
        response=session.transport.send('POST',f'session/{session.session_id}/fixture/document',{'content':document,'mime':mime})
        assert response.status==200,response.body
        return response.body['value']
    return create
@pytest.fixture
def closed_window(session):
    response = session.transport.send('POST', f'session/{session.session_id}/fixture/closed-window')
    assert response.status == 200, response.body
    return response.body['value']['original'], WebElement(session,response.body['value']['element'])
@pytest.fixture
def closed_frame(session):
    response = session.transport.send('POST', f'session/{session.session_id}/fixture/closed-frame')
    assert response.status == 200, response.body
@pytest.fixture
def get_test_page(inline):
    def create(as_frame=False):
        document = '<div id=parent>Text</div><input id=text><input type=checkbox id=checkbox><custom-element></custom-element><iframe srcdoc="<body>Nested actual frame</body>"></iframe><script>document.querySelector("custom-element").attachShadow({mode:"open"}).innerHTML="<input id=in-shadow-root>";</script>'
        if as_frame:
            document = '<iframe srcdoc="' + html.escape(document, quote=True) + '"></iframe>'
        return inline(document)
    return create
@pytest.fixture
def stale_element(session, get_test_page):
    def create(selector, as_frame=False):
        session.url = get_test_page(as_frame=as_frame)
        if as_frame:
            session.switch_to_frame(session.find.css('iframe', all=False))
        element = session.find.css(selector, all=False)
        session.execute_script('arguments[0].remove();', args=[element])
        return element
    return create
@pytest.fixture
def add_event_listeners(session):
    def attach(element, events):
        session.execute_script('window.events=[];for(const type of arguments[1]) arguments[0].addEventListener(type,event=>window.events.push(event.type));', args=[element,events])
    return attach
@pytest.fixture
def iframe(inline):
    def create(document):
        return '<iframe src="' + html.escape(inline(document),quote=True) + '"></iframe>'
    return create
@pytest.fixture
def http(session):
    class HTTP:
        @contextmanager
        def post(self, path, body):
            request=urllib.request.Request(session.transport.endpoint+path,data=json.dumps(body).encode(),method='POST',headers={'Content-Type':'application/json'})
            try:
                response=urllib.request.urlopen(request,timeout=15)
            except urllib.error.HTTPError as error:
                response=error
            with response:
                yield response
    return HTTP()
`,
)
await cp('scripts/native-webdriver-gateway.mjs', `${runDirectory}/gateway.mjs`)
await cp('scripts/verify-webdriver-native.mjs', `${runDirectory}/harness.mjs`)
await cp('native/browser/cef-build.json', `${runDirectory}/cef-build.json`)
// The checked gateway's only relative type imports are erased; runtime SDK path is the snapshot.
const inputs = [
  ...sources,
  ...(sourceInputManifest
    ? [{ path: sourceInputManifest, sha256: await hash(sourceInputManifest) }]
    : []),
  ...(await Promise.all(
    executableDependencies.map(async (path) => ({ path, sha256: await hash(path) })),
  )),
  ...runtimeResources,
  ...(await inventory(`${runDirectory}/sdk`)),
  ...(await inventory(`${runDirectory}/original`)),
  { path: `${runDirectory}/gateway.mjs`, sha256: await hash(`${runDirectory}/gateway.mjs`) },
  { path: `${runDirectory}/harness.mjs`, sha256: await hash(`${runDirectory}/harness.mjs`) },
  { path: `${runDirectory}/cef-build.json`, sha256: await hash(`${runDirectory}/cef-build.json`) },
  {
    path: `${runDirectory}/runtime/suitecut-browser`,
    sha256: await hash(`${runDirectory}/runtime/suitecut-browser`),
  },
  {
    path: `${runDirectory}/runner-dependencies.txt`,
    sha256: await hash(`${runDirectory}/runner-dependencies.txt`),
  },
  {
    path: resolve('scripts/verify-webdriver-native.mjs'),
    sha256: await hash(resolve('scripts/verify-webdriver-native.mjs')),
  },
]
await writeFile(
  `${runDirectory}/manifest.json`,
  JSON.stringify(
    {
      revision,
      oracleScope,
      cefVersion,
      sourceExecutable,
      sharedSnapshot,
      executableDependencies,
      sourceInputManifest,
      sourceSDK,
      dependencies,
      inputs,
      adaptations: [
        oracleScope === 'keyscroll'
          ? 'The complete original element_send_keys/scroll_into_view.py and original generic viewport/response/DOM helpers are unchanged; actual native sendKeys performs native-owned real DOM scroll/focus followed by physical keyboard delivery.'
          : oracleScope === 'parentframes'
            ? 'Complete original switch_to_parent_frame/switch.py is unchanged; selected parent references are real native frames retained from their observed relationship before detachment.'
            : oracleScope === 'keysbehavior'
              ? 'Complete original send-keys form_controls.py,content_editable.py,interactability.py are unchanged. W3C value command delegates the distinct genuine fixed-handle native sendKeys operation with native keyboard queue/focus/caret/deadline ownership; unsupported platform focus targets remain visible failures.'
              : oracleScope === 'newwindows'
                ? 'Complete original new_window/new.py,new_tab.py,new_window.py and generic Poll/window helpers are unchanged. Actual native headless contexts expose tabs; desktop window UI mode remains an explicit adapter capability gap.'
                : oracleScope === 'lifecycle'
                  ? 'Complete original close_window/close.py and delete_session/delete.py are unchanged, with exact pinned support HTML assets served over ordinary HTTP; sessions and windows are actual native contexts/pages.'
                  : oracleScope === 'frames'
                    ? 'Complete original switch_to_frame/switch_number.py and switch_webelement.py are unchanged; numeric/element references resolve genuine native frames and all inline/frame documents use an ordinary bounded loopback HTTP server with their actual same origin.'
                    : oracleScope === 'keys'
                      ? 'Complete original element_send_keys/send_keys.py and events.py plus their complete original generic file/response helpers are unchanged; key and input events come from the actual native keyboard/upload pipeline.'
                      : oracleScope === 'clear'
                        ? 'The complete original WPT element_clear/clear.py and original generic response/DOM parameter helpers are unchanged; fixture event listeners observe real platform events without dispatching events or setting input values.'
                        : 'Seven complete original WPT WebDriver-classic title/current-URL, element-click, element-tag-name, selected-state, enabled-state and computed-CSS files and original generic response/DOM parameter helpers are unchanged.',
        'An authored test-only W3C HTTP gateway dispatches navigation, title, URL and synchronous script commands to genuine NativeBrowserContext/NativePage/NativeFrame objects, and exposes explicitly limited protocol command coverage.',
        'Closed-window and closed-frame fixtures actually close a native page or remove an actual iframe and wait for its genuine native detach event. Standard errors reflect observed native liveness.',
        'The authored Python client uses only stdlib HTTP/JSON and a pinned generic pytest runner; no Selenium, webdriver client, Rod, chromedp or other browser driver implementation is installed or loaded.',
        'Inline documents use bounded ordinary HTTP loopback resources; actual same-origin iframe DOM access remains genuine. Original assertions observe native document title and committed URL. No browser results are approximated or substituted.',
      ],
    },
    null,
    2,
  ) + '\n',
)
console.log(`Independent WebDriver native snapshot: ${runDirectory}`)
const child = spawn(process.execPath, [`${runDirectory}/gateway.mjs`, runDirectory], {
  stdio: ['ignore', 'pipe', 'pipe'],
})
let gatewayOutput = '',
  gatewayErrors = ''
child.stderr.on('data', (value) => {
  const chunk = z.instanceof(Uint8Array).parse(value)
  gatewayErrors += new TextDecoder()
    .decode(chunk)
    .slice(0, Math.max(0, 1048576 - gatewayErrors.length))
})
const endpoint = /** @type {unknown} */ (
  await new Promise((accept, reject) => {
    child.once('error', reject)
    child.once('exit', (code) =>
      reject(new Error(`Native gateway exited before readiness (${code}): ${gatewayErrors}`)),
    )
    child.stdout.on('data', (value) => {
      gatewayOutput += new TextDecoder().decode(z.instanceof(Uint8Array).parse(value))
      const line = gatewayOutput.split('\n')[0]
      if (gatewayOutput.includes('\n') && line)
        accept(z.object({ endpoint: z.string().url() }).parse(JSON.parse(line)).endpoint)
    })
  })
)
const exit = new Promise((accept) => child.once('exit', accept))
let runnerExitCode
try {
  const completed = await run(
    runner,
    [
      '-m',
      'pytest',
      '-q',
      '--import-mode=importlib',
      `--junitxml=${runDirectory}/results.xml`,
      ...cases.map((path) => `${runDirectory}/original/${path}`),
    ],
    {
      env: { ...process.env, SUITECUT_WEBDRIVER_ENDPOINT: z.string().parse(endpoint) },
      maxBuffer: 2097152,
    },
  )
  await writeFile(`${runDirectory}/runner.log`, completed.stdout + completed.stderr)
  runnerExitCode = 0
} catch (error) {
  const failure = z
    .object({
      stdout: z.string().optional(),
      stderr: z.string().optional(),
      code: z.number().optional(),
    })
    .parse(error)
  await writeFile(`${runDirectory}/runner.log`, (failure.stdout ?? '') + (failure.stderr ?? ''))
  runnerExitCode = failure.code ?? 1
} finally {
  try {
    await fetch(`${z.string().parse(endpoint)}/shutdown`, { method: 'POST' })
    await Promise.race([
      exit,
      setTimeout(10000).then(() => {
        throw new Error('Native wire gateway did not close')
      }),
    ])
  } finally {
    await writeFile(`${runDirectory}/gateway-stderr.log`, gatewayErrors)
    if (child.exitCode === null) child.kill('SIGKILL')
  }
}
const changedInputs = []
for (const input of inputs)
  if ((await hash(input.path)) !== input.sha256) changedInputs.push(input.path)
let results = '[]'
let resultArtifactPresent = true
try {
  results = (
    await run(runner, [
      '-c',
      'import json,sys,xml.etree.ElementTree as E; r=E.parse(sys.argv[1]).getroot(); print(json.dumps([dict(x.attrib) for x in r.findall("testsuite")]))',
      `${runDirectory}/results.xml`,
    ])
  ).stdout
} catch {
  resultArtifactPresent = false
}
const stats = z
  .array(
    z.object({
      tests: z.coerce.number(),
      failures: z.coerce.number(),
      errors: z.coerce.number(),
      skipped: z.coerce.number(),
    }),
  )
  .parse(JSON.parse(results))
const summary = {
  revision,
  cefVersion,
  runDirectory,
  scope:
    oracleScope === 'keyscroll'
      ? 'Complete original WebDriver send-keys viewport scrolling file using actual native scrolling and keyboard delivery'
      : oracleScope === 'parentframes'
        ? 'Complete original WebDriver parent-frame switching file with genuine retained parent context references'
        : oracleScope === 'keysbehavior'
          ? 'Three complete original WebDriver send-keys form/editing-host/interactability files using the distinct actual fixed native handle sendKeys intent'
          : oracleScope === 'newwindows'
            ? 'Three complete original WebDriver new-context files using actual native page/history/selection and explicitly limited headless tab classification'
            : oracleScope === 'lifecycle'
              ? 'Complete original WebDriver close-window/delete-session files with genuine native page/context lifecycle and exact support HTML assets'
              : oracleScope === 'frames'
                ? 'Complete original WebDriver numeric and element frame-switch files using genuine native frame references and same-origin HTTP documents'
                : oracleScope === 'keys'
                  ? 'Complete original WebDriver send-keys and key-events files using actual native typing/upload/focus and stdlib HTTP fixture'
                  : oracleScope === 'clear'
                    ? 'Complete original WebDriver element-clear file using actual native clear/upload/input and platform blur, no DOM value or synthetic event substitutions'
                    : 'Seven complete original WebDriver-classic title/current-URL/click/tag-name/selected/enabled/CSS protocol files using authored native HTTP fixture',
  runnerExitCode,
  resultArtifactPresent,
  inputsUnchanged: changedInputs.length === 0,
  changedInputs,
  stats,
}
await writeFile(`${runDirectory}/summary.json`, JSON.stringify(summary, null, 2) + '\n')
console.log(JSON.stringify(summary))
process.exitCode = runnerExitCode || changedInputs.length || !resultArtifactPresent ? 1 : 0
