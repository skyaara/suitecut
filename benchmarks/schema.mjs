// SPDX-License-Identifier: MIT
// Shapes of what the orchestrator sends each adapter and what adapters print.
import * as z from 'zod'

const Target = z.object({ role: z.string(), name: z.string() })
export const Task = z.object({
  name: z.string(),
  goal: z.string(),
  url: z.string(),
  steps: z.array(
    z.union([
      z.object({ click: Target }),
      z.object({ fill: Target, text: z.string() }),
      z.object({ press: z.string() }),
    ]),
  ),
  done: z.string(),
})
export const Config = z.object({
  sites: z.array(z.string()),
  tasks: z.array(Task),
  out: z.string(),
})

const Timing = z.object({ ms: z.number(), all: z.array(z.number()) })
export const Site = z.object({
  url: z.string(),
  host: z.string(),
  loadMs: z.number().optional(),
  snapshot: Timing.extend({ chars: z.number() }).optional(),
  snapshotFull: Timing.extend({ chars: z.number() }).optional(),
  screenshot: Timing.extend({ bytes: z.number() }).optional(),
  error: z.string().optional(),
})
export const TaskRun = z.object({
  name: z.string(),
  ok: z.boolean(),
  ms: z.number(),
  snapshots: z.number(),
  chars: z.number(),
  snapshotMs: z.number(),
  /** Steps that needed a second snapshot and try. */
  retries: z.number().default(0),
  error: z.string().optional(),
})
export const Run = z.object({
  tool: z.string(),
  version: z.string().optional(),
  launchMs: z.number().optional(),
  closeMs: z.number().optional(),
  sites: z.array(Site).default([]),
  tasks: z.array(TaskRun).default([]),
  error: z.string().optional(),
  round: z.number().optional(),
  load1: z.number().optional(),
})
export const Results = z.object({
  environment: z.record(z.string(), z.union([z.string(), z.number()]).optional()),
  sites: z.array(z.string()),
  tasks: z.array(Task),
  rounds: z.number(),
  runs: z.array(Run),
})

/** @typedef {z.infer<typeof Task>} BenchTask */
/** @typedef {z.infer<typeof Site>} BenchSite */
/** @typedef {z.infer<typeof TaskRun>} BenchTaskRun */
/** @typedef {z.infer<typeof Run>} BenchRun */
