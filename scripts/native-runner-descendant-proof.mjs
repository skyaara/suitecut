// SPDX-License-Identifier: MIT
import { spawn } from 'node:child_process'
import { Socket } from 'node:net'
import process from 'node:process'

const control = new Socket({ fd: 3, readable: true, writable: true })
const child = spawn('/bin/sh', ['-c', 'sleep 60 & printf ready >&4; wait'], {
  detached: true,
  stdio: ['ignore', 'ignore', 'ignore', 'ignore', 'pipe'],
})
child.unref()
// Actual child spawn and a shell-ready marker prevent an admission timing probe
// from confusing a shell killed before it forks with descendant cleanup.
const ready = child.stdio[4]
if (!ready) throw new Error('Actual descendant readiness pipe is unavailable')
ready.once('data', () => {
  if (typeof child.pid !== 'number') throw new Error('Actual descendant PID is unavailable')
  control.write(`READY ${child.pid}\n`)
})
control.on('data', () => process.exit(0))
