// SPDX-License-Identifier: MIT
import { execFile } from 'node:child_process'
import { promisify } from 'node:util'

import { expect, it } from 'vitest'

const execute = promisify(execFile)

it('does not revive discarded cached HTTP bodies when admitting an unrelated response', async () => {
  const program = `
import assert from 'node:assert/strict';
import { existsSync } from 'node:fs';
import { registerHooks } from 'node:module';
import { setImmediate as turn } from 'node:timers/promises';
registerHooks({resolve(specifier, context, next) {
  if (specifier.endsWith('.js') && context.parentURL?.startsWith('file:')) {
    const candidate = new URL(specifier.slice(0, -3) + '.ts', context.parentURL);
    if (existsSync(candidate)) return next(candidate.href, context);
  }
  return next(specifier, context);
}});
const { NativeAPIRequestContext } = await import(${JSON.stringify(new URL('../src/native-http.ts', import.meta.url).href)});
const lifetime = new AbortController();
let serial = 0;
const encoded = Buffer.alloc(512 * 1024, 7).toString('base64');
const source = {
  signal: lifetime.signal, processSignal: lifetime.signal,
  runAutomation: async (_command, params) => {
    if (params.action === 'create') return { apiContextId: 1, browserContextId: 1 };
    if (params.action === 'body') return { body: encoded };
    if (params.action === 'fetch') return { apiContextId: 1, responseId: String(++serial),
      url: 'https://local.test/', status: 200, statusText: 'OK', headers: [] };
    return {};
  },
};
const api = new NativeAPIRequestContext(source);
async function populate() {
  for (let i = 0; i < 128; i++) {
    const response = await api.get('https://local.test/');
    assert.equal((await response.body()).length, 512 * 1024);
  }
}
await populate();
await turn(); await turn();
const held = await api.get('https://local.test/unrelated');
global.gc(); global.gc();
assert.ok(process.memoryUsage().arrayBuffers < 4 * 1024 * 1024,
  'Unrelated response admission revived discarded 64 MiB of cached HTTP bodies');
const bytes = new WeakRef(await held.body());
await api.dispose();
await turn(); global.gc(); await turn(); global.gc();
assert.equal(bytes.deref(), undefined, 'Context disposal retained a held response cached body');
await assert.rejects(held.body(), /disposed/);
console.log('HTTP weak-body ownership checks passed');
`
  const { stdout } = await execute(
    process.execPath,
    ['--expose-gc', '--experimental-transform-types', '--input-type=module', '--eval', program],
    { timeout: 30_000, maxBuffer: 1024 * 1024 },
  )
  expect(stdout.trim()).toBe('HTTP weak-body ownership checks passed')
})
