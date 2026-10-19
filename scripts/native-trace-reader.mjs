// SPDX-License-Identifier: MIT
// Authored test-only reader of actual native ZIP/JSON artifacts, no driver/viewer code.
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import { execFile } from 'node:child_process'
import { promisify } from 'node:util'

import * as z from 'zod'

const run = promisify(execFile)
const record = z.record(z.string(), z.unknown())
const archiveEntry = z.tuple([z.string(), z.string()])
const stacksSchema = z.object({
  files: z.array(z.string()),
  stacks: z.array(
    z.tuple([
      z.union([z.number(), z.string()]),
      z.array(
        z.tuple([z.number().int().nonnegative(), z.number(), z.number(), z.string().optional()]),
      ),
    ]),
  ),
})
/**
 * Decode exact artifact entries with the Python standard ZIP/CRC reader.
 * @param {string} path
 * @returns {Promise<Map<string,Buffer>>}
 */
export async function readNativeTraceZip(path) {
  const result = await run(
    'python3',
    [
      '-c',
      `import base64,json,sys,zipfile
with zipfile.ZipFile(sys.argv[1]) as archive:
    entries=archive.infolist()
    if len(entries)>16384 or sum(entry.file_size for entry in entries)>96*1024*1024: raise ValueError('Authored trace reader exceeds96MiB/16384entry bound')
    names=set()
    output=[]
    for entry in entries:
        name=entry.filename
        if name in names or name.startswith('/') or '..' in name.split('/') or '\\x00' in name: raise ValueError('Invalid or duplicate trace artifact entry')
        names.add(name)
        if not entry.is_dir(): output.append([name,base64.b64encode(archive.read(entry)).decode('ascii')])
    print(json.dumps(output))`,
      path,
    ],
    { maxBuffer: 136 * 1024 * 1024 },
  )
  const entries = z.array(archiveEntry).parse(JSON.parse(result.stdout))
  return new Map(entries.map(([name, bytes]) => [name, Buffer.from(bytes, 'base64')]))
}
/**
 * Project only actual stored trace records, titles, replies and stack references.
 * Missing browser events or titles are never synthesized by the reader.
 * @param {string} path
 */
export async function parseTraceRaw(path) {
  const resources = await readNativeTraceZip(path)
  const events = []
  const journals = [...resources]
    .filter(([name]) => name.endsWith('.trace'))
    .concat([...resources].filter(([name]) => name.endsWith('.network')))
  for (const [, bytes] of journals) {
    for (const line of bytes.toString('utf8').split('\n'))
      if (line.trim()) events.push(record.parse(JSON.parse(line)))
  }
  const before = events.filter((event) => event.type === 'before')
  const after = new Map(
    events
      .filter((event) => event.type === 'after')
      .map((event) => [z.string().parse(event.callId), event]),
  )
  const actionObjects = before.map((event) => ({
    ...event,
    ...after.get(z.string().parse(event.callId)),
  }))
  const actions = before.map((event) => event.title ?? event.apiName)
  const stacks = new Map()
  for (const [name, bytes] of resources) {
    if (!name.endsWith('.stacks')) continue
    const encoded = stacksSchema.parse(JSON.parse(bytes.toString('utf8')))
    for (const [id, frames] of encoded.stacks) {
      const matching = before.filter(
        (event) =>
          typeof event.callId === 'string' &&
          (event.callId === String(id) || event.callId.endsWith(`@${id}`)),
      )
      for (const event of matching) {
        const decoded = frames.map(([file, line, column, fn]) => {
          const name = encoded.files[file]
          assert(name !== undefined, 'Actual trace stack file index must exist')
          return { file: name, line, column, ...(fn === undefined ? {} : { function: fn }) }
        })
        stacks.set(z.string().parse(event.callId), decoded)
      }
    }
  }
  return { events, resources, actions, actionObjects, stacks }
}
