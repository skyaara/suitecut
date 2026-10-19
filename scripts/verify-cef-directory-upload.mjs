// SPDX-License-Identifier: MIT
/* global document, HTMLInputElement */
import assert from 'node:assert/strict'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, mkdtemp, readFile, rm, writeFile } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { basename, dirname, join, resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import { createNativePage, launchNativeBrowser } from '../dist/native.js'

const executable = z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const directory = resolve('.suitecut/cef-directory-upload')
await mkdir(directory, { recursive: true })
const upload = await mkdtemp(join(tmpdir(), 'suitecut-folder-測試-'))
await mkdir(join(upload, 'nested-中文'))
const first = join(upload, 'é-first.txt')
const second = join(upload, 'nested-中文', '第二.txt')
await writeFile(first, 'native first')
await writeFile(second, 'native nested')
const hash = async (path) =>
  createHash('sha256')
    .update(await readFile(z.string().parse(path)))
    .digest('hex')
const binarySHA256 = await hash(executable)
const cefSHA256 = await hash(join(dirname(executable), 'libcef.so'))
const source = await launchNativeBrowser({
  executablePath: executable,
  width: 320,
  height: 180,
  headless: true,
})
const results = []
try {
  const page = createNativePage(source)
  for (const mode of [
    'regular',
    'directory',
    'directory-slash',
    'enumerated-files',
    'directory-multiple',
    'intercepted-directory',
  ]) {
    await page.setContent(
      `<input type=file ${mode === 'regular' ? '' : 'webkitdirectory'} ${mode === 'directory-multiple' ? 'multiple' : ''}>`,
    )
    const identity = z.object({ result: z.object({ objectId: z.string() }) }).parse(
      await page.sendDevToolsCommand('Runtime.evaluate', {
        expression: 'document.querySelector("input")',
        returnByValue: false,
        objectGroup: 'suitecut-directory-probe',
      }),
    )
    const described = z
      .object({ node: z.object({ backendNodeId: z.number().int() }) })
      .parse(
        await page.sendDevToolsCommand('DOM.describeNode', { objectId: identity.result.objectId }),
      )
    await page.sendDevToolsCommand('DOM.enable')
    let pickerResult = null
    if (mode === 'intercepted-directory') {
      await page.sendDevToolsCommand('Page.setInterceptFileChooserDialog', { enabled: true })
      pickerResult = await page.sendDevToolsCommand('Runtime.callFunctionOn', {
        objectId: identity.result.objectId,
        functionDeclaration: 'function(){this.showPicker();}',
        returnByValue: true,
        userGesture: true,
      })
    }
    const paths =
      mode === 'regular'
        ? [first]
        : mode === 'enumerated-files'
          ? [first, second]
          : [mode === 'directory-slash' ? `${upload}/` : upload]
    const protocolResult = await page.sendDevToolsCommand('DOM.setFileInputFiles', {
      backendNodeId: described.node.backendNodeId,
      files: paths,
    })
    let waitError = null
    try {
      const ready = await page.waitForFunction(
        () => {
          const input = document.querySelector('input')
          return input instanceof HTMLInputElement && !!input.files?.length
        },
        undefined,
        { timeout: 1000 },
      )
      await ready.dispose()
    } catch (error) {
      waitError = error instanceof Error ? error.message : String(error)
    }
    const observed = await page.evaluate(async () => {
      const input = document.querySelector('input')
      if (!(input instanceof HTMLInputElement)) throw new Error('Missing input probe')
      return {
        directory: input.webkitdirectory,
        multiple: input.multiple,
        files: await Promise.all(
          Array.from(input.files ?? [], async (file) => ({
            name: file.name,
            relativePath: file.webkitRelativePath,
            text: await file.text(),
          })),
        ),
      }
    })
    if (mode === 'regular') {
      assert.equal(observed.files.length, 1)
      assert.equal(observed.files[0]?.text, 'native first')
    }
    results.push({ mode, protocolResult, pickerResult, waitError, observed })
    await page.sendDevToolsCommand('Runtime.releaseObjectGroup', {
      objectGroup: 'suitecut-directory-probe',
    })
    if (mode === 'intercepted-directory')
      await page.sendDevToolsCommand('Page.setInterceptFileChooserDialog', { enabled: false })
  }
  assert.equal(
    await hash(executable),
    binarySHA256,
    'Native executable changed during directory capability proof',
  )
  const expected = [
    { relativePath: `${basename(upload)}/é-first.txt`, text: 'native first' },
    { relativePath: `${basename(upload)}/nested-中文/第二.txt`, text: 'native nested' },
  ]
  const matched = results
    .filter((result) => result.mode !== 'regular')
    .some(
      (result) =>
        result.observed.files.length === 2 &&
        expected.every((wanted) =>
          result.observed.files.some(
            (file) => file.relativePath === wanted.relativePath && file.text === wanted.text,
          ),
        ),
    )
  await writeFile(
    join(directory, 'protocol-report.json'),
    JSON.stringify(
      {
        binarySHA256,
        cefSHA256,
        cefVersion: source.cefVersion,
        directoryVerified: matched,
        expected: [
          { path: `${basename(upload)}/é-first.txt`, text: 'native first' },
          { path: `${basename(upload)}/nested-中文/第二.txt`, text: 'native nested' },
        ],
        results,
      },
      null,
      2,
    ),
  )
  console.log(
    JSON.stringify({
      directoryVerified: matched,
      cases: results.map((result) => ({
        mode: result.mode,
        fileCount: result.observed.files.length,
        waitError: result.waitError,
      })),
    }),
  )
} finally {
  await source.close()
  await rm(upload, { recursive: true, force: true })
}
