// The frame clock, hand-drawn: Chromium's paints arrive unevenly along the top,
// the video's frames tick evenly along the bottom, and each tick takes the newest
// paint since the last one. Older paints in the same gap are crossed out; a tick
// with no new paint holds the last picture. A playhead sweeps across on a loop
// and each video frame lights up as it passes.
import { line, random, rectangle, round, type Random } from './doodle.js'

const width = 960
const height = 430
const left = 60
const right = 920
const spanMs = 400
const x = (ms: number) => left + ((right - left) * ms) / spanMs

/** A 60 Hz animation for about 90 ms, a still page, a short scroll, then still again. */
const paints = [3, 20, 36, 53, 70, 86, 186, 203, 220, 236]
const ticks = Array.from({ length: 12 }, (_, i) => ((i + 1) * 1000) / 30)

export interface ClockTick {
  readonly ms: number
  /** The paint this tick shows, or undefined when it holds the last one. */
  readonly paint: number | undefined
  readonly skipped: readonly number[]
}

/** What the recorder does at each tick: the newest paint since the last tick, or hold. */
export function clockTicks(): ClockTick[] {
  let previous = 0
  return ticks.map((ms) => {
    const since = paints.filter((paint) => paint > previous && paint <= ms)
    previous = ms
    return { ms, paint: since.at(-1), skipped: since.slice(0, -1) }
  })
}

function arrow(rand: Random, x1: number, y1: number, x2: number, y2: number) {
  const angle = Math.atan2(y2 - y1, x2 - x1)
  const wing = (side: number) =>
    line(
      rand,
      x2,
      y2,
      x2 - Math.cos(angle + side * 0.5) * 12,
      y2 - Math.sin(angle + side * 0.5) * 12,
      0.5,
    )
  return [line(rand, x1, y1, x2, y2, 0.8), wing(1), wing(-1)]
}

function text(
  tx: number,
  ty: number,
  value: string,
  size: number,
  className: string,
  anchor = 'middle',
) {
  return `<text class="${className}" x="${round(tx)}" y="${round(ty)}" font-size="${size}" text-anchor="${anchor}" dominant-baseline="central">${value}</text>`
}

export function renderClock(): string {
  const rand = random(88)
  const parts: string[] = []
  let order = 0
  const ink = (d: string, extra = '') =>
    parts.push(`<path class="dd-ink" d="${d}" pathLength="1" style="--i:${order++}"${extra}/>`)
  const words = (markup: string) =>
    parts.push(`<g class="dd-text" style="--i:${order}">${markup}</g>`)

  const paintY = 96
  const tickY = 286
  const decided = clockTicks()
  const skipped = new Set(decided.flatMap((tick) => tick.skipped))

  words(text(left - 22, 24, 'Chromium paints only when something changes', 20, 'dd-note', 'start'))
  words(
    text(
      left - 22,
      tickY + 50,
      'The video takes one frame every 33 ms (30 fps): 12 frames in 400 ms',
      20,
      'dd-note',
      'start',
    ),
  )

  // The paints, each a small frame on the time line.
  ink(line(rand, left - 20, paintY, right + 20, paintY, 0.6))
  for (const paint of paints) {
    const px = x(paint)
    parts.push(
      `<path class="dd-fill" d="M${round(px - 11)} ${paintY - 8} h22 v16 h-22 Z" fill="${skipped.has(paint) ? 'var(--card)' : 'var(--doodle-yellow, var(--marker-yellow))'}" style="--i:${order}"/>`,
    )
    ink(rectangle(rand, px - 11, paintY - 8, 22, 16))
    if (skipped.has(paint)) {
      ink(line(rand, px - 14, paintY - 12, px + 14, paintY + 12, 1.2), ' data-cross=""')
      ink(line(rand, px + 14, paintY - 12, px - 14, paintY + 12, 1.2), ' data-cross=""')
    }
  }
  words(text(x(48), paintY - 32, 'an animation', 17, 'dd-detail'))
  words(text(x(136), paintY - 32, 'nothing moves', 17, 'dd-detail'))
  words(text(x(211), paintY - 32, 'a scroll', 17, 'dd-detail'))
  words(text(x(320), paintY - 32, 'still again', 17, 'dd-detail'))

  // The ticks, evenly spaced, each with the paint it took.
  ink(line(rand, left - 20, tickY, right + 20, tickY, 0.6))
  const box = { w: 52, h: 40 }
  decided.forEach((tick) => {
    const tx = x(tick.ms)
    const top = tickY - box.h / 2
    const fill =
      tick.paint === undefined ? 'var(--card)' : 'var(--doodle-yellow, var(--marker-yellow))'
    parts.push(
      `<path class="dd-fill" d="M${round(tx - box.w / 2)} ${top} h${box.w} v${box.h} h-${box.w} Z" fill="${fill}" style="--i:${order}"/>`,
    )
    ink(rectangle(rand, tx - box.w / 2, top, box.w, box.h))
    words(
      text(
        tx,
        tickY,
        tick.paint === undefined ? 'held' : 'new',
        16,
        tick.paint === undefined ? 'dd-detail' : 'dd-label',
      ),
    )
    if (tick.paint !== undefined)
      arrow(rand, x(tick.paint), paintY + 12, tx, top - 6).forEach((d) => ink(d))
    // Lights up as the playhead passes; it crosses in the first 84% of the loop, then rests.
    const at = Number(((0.84 * tick.ms) / spanMs).toFixed(4))
    parts.push(
      `<rect class="dd-pulse" x="${round(tx - box.w / 2 - 5)}" y="${top - 5}" width="${box.w + 10}" height="${box.h + 10}" rx="8" opacity="0"><animate attributeName="opacity" dur="6s" repeatCount="indefinite" values="0;0;1;0;0" keyTimes="0;${Number(Math.max(0, at - 0.01).toFixed(4))};${at};${Number(Math.min(1, at + 0.06).toFixed(4))};1"/></rect>`,
    )
  })

  // Legend.
  const legendY = 392
  const legend: [string, string, boolean][] = [
    ['new: the newest paint', 'var(--doodle-yellow, var(--marker-yellow))', false],
    ['held: the same picture again', 'var(--card)', false],
    ['skipped: a newer paint replaced it', 'var(--card)', true],
  ]
  legend.forEach(([label, fill, crossed], i) => {
    const lx = left - 20 + i * 300
    parts.push(
      `<path class="dd-fill" d="M${lx} ${legendY - 8} h22 v16 h-22 Z" fill="${fill}" style="--i:${order}"/>`,
    )
    ink(rectangle(rand, lx, legendY - 8, 22, 16))
    if (crossed) ink(line(rand, lx - 3, legendY - 11, lx + 25, legendY + 11, 1.2), ' data-cross=""')
    words(text(lx + 34, legendY, label, 17, 'dd-detail', 'start'))
  })

  // The playhead.
  parts.push(
    `<g class="dd-flow" aria-hidden="true"><line class="dd-playhead" x1="${left}" y1="${paintY - 30}" x2="${left}" y2="${tickY + 30}"><animate attributeName="x1" dur="6s" repeatCount="indefinite" values="${left};${right};${right}" keyTimes="0;0.84;1"/><animate attributeName="x2" dur="6s" repeatCount="indefinite" values="${left};${right};${right}" keyTimes="0;0.84;1"/></line></g>`,
  )

  return `<svg class="doodle" viewBox="0 0 ${width} ${height}" role="img" aria-label="Chromium paints arrive unevenly: a burst during an animation, none while the page is still, a few during a scroll. The video takes a frame every 33 ms: the newest paint since the last frame, marked new, or the same picture again, marked held. Paints that a newer one replaced before the next frame are skipped." style="--strokes:${order}">${parts.join('')}</svg>`
}
