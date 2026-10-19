// SPDX-License-Identifier: MIT
import { access, mkdtemp, readFile, rm, writeFile } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { setTimeout as delay } from 'node:timers/promises'

import { afterEach, describe, expect, it } from 'vitest'

import { launchProtocolProcess, type ProtocolBrowserProcess } from '../src/browser-process.js'

const directories: string[] = []
const browsers: ProtocolBrowserProcess[] = []

async function fixture(body: string): Promise<{ executablePath: string; statePath: string }> {
  const directory = await mkdtemp(join(tmpdir(), 'suitecut-process-test-'))
  directories.push(directory)
  const executablePath = join(directory, 'browser.cjs')
  const statePath = join(directory, 'state.json')
  await writeFile(
    executablePath,
    `#!${process.execPath}\nconst fs = require('node:fs');\nconst args = process.argv.slice(2);\nconst profile = args.find(a => a.startsWith('--user-data-dir='))?.split('=').slice(1).join('=') ?? args[args.indexOf('--profile') + 1];\nfs.writeFileSync(${JSON.stringify(statePath)}, JSON.stringify({pid: process.pid, args, profile}));\n${body}\n`,
    { mode: 0o755 },
  )
  return { executablePath, statePath }
}

async function state(path: string): Promise<{ pid: number; args: string[]; profile: string }> {
  return JSON.parse(await readFile(path, 'utf8')) as {
    pid: number
    args: string[]
    profile: string
  }
}

afterEach(async () => {
  await Promise.all(browsers.splice(0).map((browser) => browser.close()))
  await Promise.all(
    directories.splice(0).map((directory) => rm(directory, { recursive: true, force: true })),
  )
})

describe.skipIf(process.platform === 'win32')('owned protocol browser processes', () => {
  it('discovers fragmented CDP output and removes the isolated profile after idempotent close', async () => {
    const config = await fixture(
      "process.stderr.write('DevTools listen'); setTimeout(() => process.stderr.write('ing on ws://127.0.0.1:12345/devtools/browser/test-id\\n'), 10); setInterval(() => {}, 1000)",
    )
    const browser = await launchProtocolProcess({
      backend: 'cdp',
      executablePath: config.executablePath,
      args: ['--no-sandbox'],
      timeoutMs: 2_000,
    })
    browsers.push(browser)
    expect(browser.endpointURL).toBe('ws://127.0.0.1:12345/devtools/browser/test-id')
    const launched = await state(config.statePath)
    expect(launched.args).toContain('--remote-debugging-address=127.0.0.1')
    expect(launched.args).toContain('--headless=new')
    expect(launched.args).toContain('--no-sandbox')
    await access(launched.profile)
    const first = browser.close()
    expect(browser.close()).toBe(first)
    await Promise.all([first, browser.closed])
    expect(() => process.kill(launched.pid, 0)).toThrow()
    await expect(access(launched.profile)).rejects.toMatchObject({ code: 'ENOENT' })
  })

  it('discovers CDP from its bounded endpoint file without stderr and preserves sandbox defaults', async () => {
    const config = await fixture(
      "fs.writeFileSync(require('node:path').join(profile, 'DevToolsActivePort'), '23456\\n/devtools/browser/from-file\\n'); setInterval(() => {}, 1000)",
    )
    const browser = await launchProtocolProcess({
      backend: 'cdp',
      executablePath: config.executablePath,
      headless: false,
      timeoutMs: 2_000,
    })
    browsers.push(browser)
    expect(browser.endpointURL).toBe('ws://127.0.0.1:23456/devtools/browser/from-file')
    const launched = await state(config.statePath)
    expect(launched.args.some((arg) => arg.startsWith('--headless'))).toBe(false)
    expect(launched.args).not.toContain('--no-sandbox')
  })

  it('normalizes Firefox BiDi to /session and closes a browser that ignores SIGTERM', async () => {
    const config = await fixture(
      "process.on('SIGTERM', () => {}); process.stdout.write('WebDriver BiDi listening on ws://127.0.0.1:34567\\n'); setInterval(() => {}, 1000)",
    )
    const browser = await launchProtocolProcess({
      backend: 'bidi',
      executablePath: config.executablePath,
      timeoutMs: 2_000,
    })
    browsers.push(browser)
    expect(browser.endpointURL).toBe('ws://127.0.0.1:34567/session')
    const launched = await state(config.statePath)
    expect(launched.args).toContain('--headless')
    await browser.close()
    expect(() => process.kill(launched.pid, 0)).toThrow()
    await expect(access(launched.profile)).rejects.toMatchObject({ code: 'ENOENT' })
  })

  it('reaps timed out launches and bounds diagnostic output', async () => {
    const config = await fixture(
      "process.stderr.write('x'.repeat(256 * 1024)); setInterval(() => {}, 1000)",
    )
    let error: Error | undefined
    try {
      await launchProtocolProcess({
        backend: 'cdp',
        executablePath: config.executablePath,
        timeoutMs: 150,
      })
    } catch (failure) {
      if (failure instanceof Error) error = failure
      else throw failure
    }
    expect(error?.name).toBe('TimeoutError')
    expect(error?.message.length).toBeLessThan(17_000)
    const launched = await state(config.statePath)
    expect(() => process.kill(launched.pid, 0)).toThrow()
    await expect(access(launched.profile)).rejects.toMatchObject({ code: 'ENOENT' })
  })

  it('cleans up aborted launches and honors a pre-aborted signal without spawning', async () => {
    const config = await fixture('setInterval(() => {}, 1000)')
    const controller = new AbortController()
    const launching = launchProtocolProcess({
      backend: 'cdp',
      executablePath: config.executablePath,
      signal: controller.signal,
    })
    const timer = setTimeout(() => controller.abort(), 150)
    try {
      await expect(launching).rejects.toMatchObject({ name: 'AbortError' })
    } finally {
      clearTimeout(timer)
    }
    const launched = await state(config.statePath)
    expect(() => process.kill(launched.pid, 0)).toThrow()
    await expect(access(launched.profile)).rejects.toMatchObject({ code: 'ENOENT' })
    await rm(config.statePath)
    await expect(
      launchProtocolProcess({
        backend: 'cdp',
        executablePath: config.executablePath,
        signal: controller.signal,
      }),
    ).rejects.toMatchObject({ name: 'AbortError' })
    await expect(access(config.statePath)).rejects.toMatchObject({ code: 'ENOENT' })
  })

  it('rejects external endpoint advertisements and reports early exit', async () => {
    const config = await fixture(
      "process.stderr.write('DevTools listening on ws://example.com:9222/devtools/browser/untrusted\\n'); process.exitCode = 7",
    )
    await expect(
      launchProtocolProcess({
        backend: 'cdp',
        executablePath: config.executablePath,
        timeoutMs: 2_000,
      }),
    ).rejects.toThrow('exited before exposing')
    await expect(access((await state(config.statePath)).profile)).rejects.toMatchObject({
      code: 'ENOENT',
    })
  })

  it('surfaces spawn failures without leaking the temporary profile', async () => {
    await expect(
      launchProtocolProcess({ backend: 'bidi', executablePath: '/suitecut/missing-browser' }),
    ).rejects.toMatchObject({ code: 'ENOENT' })
  })

  it('terminates owned renderer subprocesses and awaits their parent reaping them', async () => {
    const config =
      await fixture(`const child = require('node:child_process').spawn(process.execPath, ['-e', 'setInterval(() => {}, 1000)'], {stdio: 'ignore'});
      fs.writeFileSync(require('node:path').join(profile, 'child-pid'), String(child.pid));
      process.on('SIGTERM', () => {});
      child.on('exit', () => process.exit(0));
      process.stderr.write('DevTools listening on ws://127.0.0.1:12345/devtools/browser/children\\n');
      setInterval(() => {}, 1000)`)
    const browser = await launchProtocolProcess({
      backend: 'cdp',
      executablePath: config.executablePath,
      timeoutMs: 2_000,
    })
    browsers.push(browser)
    const launched = await state(config.statePath)
    const childPid = Number(await readFile(join(launched.profile, 'child-pid'), 'utf8'))
    await browser.close()
    expect(() => process.kill(launched.pid, 0)).toThrow()
    expect(() => process.kill(childPid, 0)).toThrow()
  })

  it('cleans its profile when the browser exits without an explicit close', async () => {
    const config = await fixture(
      "process.stderr.write('WebDriver BiDi listening on ws://127.0.0.1:34567/session\\n'); setTimeout(() => process.exit(0), 100)",
    )
    const browser = await launchProtocolProcess({
      backend: 'bidi',
      executablePath: config.executablePath,
      timeoutMs: 2_000,
    })
    browsers.push(browser)
    const launched = await state(config.statePath)
    await browser.closed
    expect(() => process.kill(launched.pid, 0)).toThrow()
    await expect(access(launched.profile)).rejects.toMatchObject({ code: 'ENOENT' })
    await browser.close()
  })

  it('allows a renderer to complete naturally after its browser parent exits', async () => {
    const config = await fixture(`const path = require('node:path');
      const marker = path.join(path.dirname(__filename), 'renderer-done');
      const child = require('node:child_process').spawn(process.execPath, ['-e',
        'process.send("ready"); process.disconnect(); setTimeout(() => { require("node:fs").writeFileSync(' + JSON.stringify(marker) + ', "done"); process.exit(0); }, 75)'
      ], {stdio: ['ignore', 'ignore', 'ignore', 'ipc']});
      child.once('message', () => {
        process.stderr.write('DevTools listening on ws://127.0.0.1:12345/devtools/browser/drain\\n');
        setTimeout(() => process.exit(0), 10);
      });`)
    const browser = await launchProtocolProcess({
      backend: 'cdp',
      executablePath: config.executablePath,
      timeoutMs: 2_000,
    })
    browsers.push(browser)
    await browser.closed
    await expect(
      readFile(join(config.executablePath, '..', 'renderer-done'), 'utf8'),
    ).resolves.toBe('done')
    await expect(access((await state(config.statePath)).profile)).rejects.toMatchObject({
      code: 'ENOENT',
    })
  })

  it.skipIf(process.platform !== 'linux')(
    'kills a stalled renderer after the bounded parent-exit drain',
    async () => {
      const config =
        await fixture(`const child = require('node:child_process').spawn(process.execPath, ['-e',
      'process.on("SIGTERM", () => {}); process.send("ready"); process.disconnect(); setInterval(() => {}, 1000)'
    ], {stdio: ['ignore', 'ignore', 'ignore', 'ipc']});
    fs.writeFileSync(require('node:path').join(require('node:path').dirname(__filename), 'renderer-pid'), String(child.pid));
    child.once('message', () => {
      process.stderr.write('DevTools listening on ws://127.0.0.1:12345/devtools/browser/stalled\\n');
      setTimeout(() => process.exit(0), 10);
    });`)
      const browser = await launchProtocolProcess({
        backend: 'cdp',
        executablePath: config.executablePath,
        timeoutMs: 2_000,
      })
      browsers.push(browser)
      const childPid = Number(
        await readFile(join(config.executablePath, '..', 'renderer-pid'), 'utf8'),
      )
      const fixtureReport = process.env.SUITECUT_PROCESS_FIXTURE_REPORT
      const incarnation = fixtureReport
        ? Number(
            (await readFile(`/proc/${childPid}/stat`, 'utf8')).split(') ').at(-1)!.split(' ')[19],
          )
        : undefined
      await browser.closed
      let stopped = false
      for (let attempt = 0; attempt < 50; attempt++) {
        try {
          const stat = await readFile(`/proc/${childPid}/stat`, 'utf8')
          if (/^[ZX] /.test(stat.slice(stat.lastIndexOf(') ') + 2))) {
            stopped = true
            break
          }
        } catch (error) {
          if (error instanceof Error && 'code' in error && error.code === 'ENOENT') {
            stopped = true
            break
          }
          throw error
        }
        await delay(10)
      }
      expect(stopped).toBe(true)
      if (fixtureReport) {
        await writeFile(
          fixtureReport,
          JSON.stringify({
            pid: childPid,
            start: incarnation,
            expectedSignal: 9,
            reason:
              'Explicitly stalled orphan renderer ignores SIGTERM; bounded SDK drain must kill it',
          }),
        )
      }
      await expect(access((await state(config.statePath)).profile)).rejects.toMatchObject({
        code: 'ENOENT',
      })
    },
  )

  it.each([
    '--remote-debugging-port=9222',
    '--remote-debugging-address',
    '--remote-debugging-pipe',
    '--user-data-dir=/tmp/existing',
    '--profile',
    '-profile',
    '-P',
    '--profile-directory=Default',
    '--',
  ])('rejects managed flag override %s before spawning', async (arg) => {
    const config = await fixture('process.exit(1)')
    await expect(
      launchProtocolProcess({ backend: 'cdp', executablePath: config.executablePath, args: [arg] }),
    ).rejects.toThrow('argument is not allowed')
    await expect(access(config.statePath)).rejects.toMatchObject({ code: 'ENOENT' })
  })
})
