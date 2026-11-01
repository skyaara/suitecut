// Hand-drawn diagrams as static SVG. Each stroke wobbles a little and is drawn
// twice, like a pen going over a line; fills are highlighter swipes. A seed
// makes every diagram come out the same on each build.

export type Fill = 'yellow' | 'blue' | 'red' | 'chrome' | 'none'

export interface DoodleNode {
  readonly id: string
  readonly x: number
  readonly y: number
  readonly w: number
  readonly h: number
  readonly label: string
  /** A smaller second line under the label. */
  readonly detail?: string
  readonly fill?: Fill
  /** `window` adds a tab strip, `round` is a pill, `circle` an ellipse, `note` has no outline. */
  readonly shape?: 'box' | 'round' | 'circle' | 'window' | 'note'
}

export interface DoodleEdge {
  readonly from: string
  readonly to: string
  readonly label?: string
  /** Bends the arrow sideways by this many units; negative bends the other way. */
  readonly bend?: number
  readonly dashed?: boolean
  /** Arrowheads at both ends. */
  readonly both?: boolean
}

export interface DoodleText {
  readonly x: number
  readonly y: number
  readonly text: string
  readonly size?: number
  readonly muted?: boolean
  readonly anchor?: 'start' | 'middle' | 'end'
  readonly rotate?: number
}

export interface Doodle {
  readonly width: number
  readonly height: number
  readonly seed: number
  /** What the diagram shows, for screen readers. */
  readonly title: string
  readonly nodes: readonly DoodleNode[]
  readonly edges?: readonly DoodleEdge[]
  readonly texts?: readonly DoodleText[]
  /** Crossed-out strokes over a node, for "not this". */
  readonly crossed?: readonly string[]
}

type Random = () => number

function random(seed: number): Random {
  let state = seed >>> 0 || 1
  return () => {
    state = (Math.imul(state, 1664525) + 1013904223) >>> 0
    return state / 2 ** 32
  }
}

const round = (value: number) => Math.round(value * 10) / 10
const point = (x: number, y: number) => `${round(x)} ${round(y)}`

/** One pass of a wobbly line: a cubic whose ends and middle drift a little. */
function pass(rand: Random, x1: number, y1: number, x2: number, y2: number, roughness: number) {
  const length = Math.hypot(x2 - x1, y2 - y1) || 1
  const drift = Math.min(length * 0.02, 2.2) * roughness
  const jitter = () => (rand() - 0.5) * 2 * drift
  const bow = (rand() - 0.5) * Math.min(length * 0.035, 7) * roughness
  const nx = -(y2 - y1) / length
  const ny = (x2 - x1) / length
  const c1x = x1 + (x2 - x1) * 0.3 + nx * bow + jitter()
  const c1y = y1 + (y2 - y1) * 0.3 + ny * bow + jitter()
  const c2x = x1 + (x2 - x1) * 0.7 + nx * bow + jitter()
  const c2y = y1 + (y2 - y1) * 0.7 + ny * bow + jitter()
  return `M${point(x1 + jitter(), y1 + jitter())} C${point(c1x, c1y)} ${point(c2x, c2y)} ${point(x2 + jitter(), y2 + jitter())}`
}

function line(rand: Random, x1: number, y1: number, x2: number, y2: number, roughness = 1) {
  return `${pass(rand, x1, y1, x2, y2, roughness)} ${pass(rand, x1, y1, x2, y2, roughness * 0.8)}`
}

/** A closed loop through points, smoothed, that overshoots its start like a quick pen. */
function loop(rand: Random, points: readonly [number, number][], wobble: number) {
  const shaken = points.map(
    ([x, y]) => [x + (rand() - 0.5) * wobble, y + (rand() - 0.5) * wobble] as const,
  )
  const [first, second] = shaken
  if (!first || !second) return ''
  const overshoot = [
    first[0] + (second[0] - first[0]) * 0.35,
    first[1] + (second[1] - first[1]) * 0.35,
  ] as const
  const closed = [shaken.at(-1) ?? first, ...shaken, first, overshoot]
  const at = (i: number) => closed[Math.min(Math.max(i, 0), closed.length - 1)] ?? first
  // Catmull-Rom through the points, from the first real point to the overshoot.
  let d = `M${point(first[0], first[1])}`
  for (let i = 1; i < closed.length - 1; i++) {
    const [x0, y0] = at(i - 1)
    const [x1, y1] = at(i)
    const [x2, y2] = at(i + 1)
    const [x3, y3] = at(i + 2)
    d += ` C${point(x1 + (x2 - x0) / 6, y1 + (y2 - y0) / 6)} ${point(x2 - (x3 - x1) / 6, y2 - (y3 - y1) / 6)} ${point(x2, y2)}`
  }
  return d
}

function ellipse(rand: Random, cx: number, cy: number, rx: number, ry: number) {
  const draw = () => {
    const start = rand() * Math.PI * 2
    const points: [number, number][] = []
    for (let i = 0; i < 12; i++) {
      const angle = start + (i / 12) * Math.PI * 2
      const r = 1 + (rand() - 0.5) * 0.05
      points.push([cx + Math.cos(angle) * rx * r, cy + Math.sin(angle) * ry * r])
    }
    return loop(rand, points, 2)
  }
  return `${draw()} ${draw()}`
}

function rectangle(rand: Random, x: number, y: number, w: number, h: number) {
  return [
    line(rand, x, y, x + w, y),
    line(rand, x + w, y, x + w, y + h),
    line(rand, x + w, y + h, x, y + h),
    line(rand, x, y + h, x, y),
  ].join(' ')
}

function pill(rand: Random, x: number, y: number, w: number, h: number) {
  const r = h / 2
  const draw = () => {
    const points: [number, number][] = [
      [x + r, y],
      [x + w / 2, y],
      [x + w - r, y],
    ]
    for (let i = 1; i < 4; i++)
      points.push([
        x + w - r + Math.sin((i / 4) * Math.PI) * r,
        y + r - Math.cos((i / 4) * Math.PI) * r,
      ])
    points.push([x + w - r, y + h], [x + w / 2, y + h], [x + r, y + h])
    for (let i = 1; i < 4; i++)
      points.push([
        x + r - Math.sin((i / 4) * Math.PI) * r,
        y + r + Math.cos((i / 4) * Math.PI) * r,
      ])
    return loop(rand, points, 2.5)
  }
  return `${draw()} ${draw()}`
}

/** A highlighter swipe a little off the shape, as if filled in by hand. */
function swipe(rand: Random, x: number, y: number, w: number, h: number) {
  const j = () => (rand() - 0.5) * 6
  const dx = 5 + rand() * 3
  const dy = 5 + rand() * 3
  return `M${point(x + dx + j(), y + dy + j())} L${point(x + w + dx + j(), y + dy + j())} L${point(x + w + dx + j(), y + h + dy + j())} L${point(x + dx + j(), y + h + dy + j())} Z`
}

const fills: Record<Exclude<Fill, 'none'>, string> = {
  yellow: 'var(--doodle-yellow, var(--marker-yellow))',
  blue: 'var(--doodle-blue, var(--marker-blue))',
  red: 'var(--doodle-red, var(--marker-red))',
  chrome: 'var(--doodle-chrome, rgba(150, 160, 172, 0.45))',
}

function escape(text: string) {
  return text.replaceAll('&', '&amp;').replaceAll('<', '&lt;').replaceAll('>', '&gt;')
}

function textBlock(
  x: number,
  y: number,
  lines: readonly string[],
  size: number,
  className: string,
  anchor = 'middle',
) {
  const height = size * 1.25
  const top = y - ((lines.length - 1) * height) / 2
  return lines
    .map(
      (text, i) =>
        `<text class="${className}" x="${round(x)}" y="${round(top + i * height)}" font-size="${size}" text-anchor="${anchor}" dominant-baseline="central">${escape(text)}</text>`,
    )
    .join('')
}

/** Where a straight line from a node's centre towards (tx, ty) leaves the node, plus a gap. */
function exit(node: DoodleNode, tx: number, ty: number, gap = 10) {
  const cx = node.x + node.w / 2
  const cy = node.y + node.h / 2
  const dx = tx - cx
  const dy = ty - cy
  const length = Math.hypot(dx, dy) || 1
  if (node.shape === 'circle') {
    const t = 1 / Math.sqrt((dx / (node.w / 2)) ** 2 + (dy / (node.h / 2)) ** 2)
    return [cx + dx * t + (dx / length) * gap, cy + dy * t + (dy / length) * gap] as const
  }
  const scale = Math.min(Math.abs(node.w / 2 / (dx || 1e-9)), Math.abs(node.h / 2 / (dy || 1e-9)))
  return [cx + dx * scale + (dx / length) * gap, cy + dy * scale + (dy / length) * gap] as const
}

/** Renders a diagram to an SVG string whose strokes can draw themselves in, in order. */
export function renderDoodle(doodle: Doodle): string {
  const rand = random(doodle.seed)
  const byId = new Map(doodle.nodes.map((node) => [node.id, node]))
  const parts: string[] = []
  let order = 0
  const stroke = (d: string, extra = '') =>
    parts.push(`<path class="dd-ink" d="${d}" pathLength="1" style="--i:${order++}"${extra}/>`)

  for (const node of doodle.nodes) {
    const { x, y, w, h } = node
    parts.push(`<g data-node="${node.id}">`)
    if (node.fill && node.fill !== 'none') {
      const shape =
        node.shape === 'circle'
          ? ellipse(rand, x + w / 2 + 6, y + h / 2 + 6, w / 2, h / 2).split(' M')[0]
          : swipe(rand, x, y, w, h)
      parts.push(
        `<path class="dd-fill" d="${shape}" fill="${fills[node.fill]}" style="--i:${order}"/>`,
      )
    }
    if (node.shape === 'circle') stroke(ellipse(rand, x + w / 2, y + h / 2, w / 2, h / 2))
    else if (node.shape === 'round') stroke(pill(rand, x, y, w, h))
    else if (node.shape !== 'note') stroke(rectangle(rand, x, y, w, h))
    if (node.shape === 'window') {
      stroke(line(rand, x, y + 30, x + w, y + 30, 0.6))
      for (let i = 0; i < 3; i++) stroke(ellipse(rand, x + 18 + i * 16, y + 15, 4, 4))
    }
    const labelY = node.shape === 'window' ? y + 30 + (h - 30) / 2 : y + h / 2
    const labels = node.label.split('\n')
    const details = node.detail?.split('\n') ?? []
    const labelSize = 24
    const detailSize = 19
    const total =
      labels.length * labelSize * 1.25 +
      (details.length ? 6 + details.length * detailSize * 1.25 : 0)
    const top = labelY - total / 2
    parts.push(
      `<g class="dd-text" style="--i:${order}">${textBlock(x + w / 2, top + (labels.length * labelSize * 1.25) / 2, labels, labelSize, 'dd-label')}${
        details.length
          ? textBlock(
              x + w / 2,
              top + labels.length * labelSize * 1.25 + 6 + (details.length * detailSize * 1.25) / 2,
              details,
              detailSize,
              'dd-detail',
            )
          : ''
      }</g></g>`,
    )
  }

  for (const id of doodle.crossed ?? []) {
    const node = byId.get(id)
    if (!node) throw new Error(`Doodle "${doodle.title}" crosses out a missing node: ${id}`)
    stroke(
      line(rand, node.x - 8, node.y - 6, node.x + node.w + 8, node.y + node.h + 6, 1.4),
      ' data-cross=""',
    )
    stroke(
      line(rand, node.x + node.w + 8, node.y - 6, node.x - 8, node.y + node.h + 6, 1.4),
      ' data-cross=""',
    )
  }

  for (const edge of doodle.edges ?? []) {
    const from = byId.get(edge.from)
    const to = byId.get(edge.to)
    if (!from || !to)
      throw new Error(
        `Doodle "${doodle.title}" has an edge to a missing node: ${edge.from} → ${edge.to}`,
      )
    const fx = from.x + from.w / 2
    const fy = from.y + from.h / 2
    const tx = to.x + to.w / 2
    const ty = to.y + to.h / 2
    const length = Math.hypot(tx - fx, ty - fy) || 1
    const bend = edge.bend ?? 0
    const mx = (fx + tx) / 2 + (-(ty - fy) / length) * bend
    const my = (fy + ty) / 2 + ((tx - fx) / length) * bend
    const [sx, sy] = exit(from, mx, my)
    const [ex, ey] = exit(to, mx, my)
    const curve = (wobble: number) => {
      const j = () => (rand() - 0.5) * wobble
      return `M${point(sx + j(), sy + j())} Q${point(mx + j() * 2, my + j() * 2)} ${point(ex + j(), ey + j())}`
    }
    stroke(`${curve(3)} ${curve(2.4)}`, edge.dashed ? ' data-dashed=""' : '')
    const head = (hx: number, hy: number, ax: number, ay: number) => {
      const angle = Math.atan2(hy - ay, hx - ax)
      const size = 14
      const wing = (side: number) =>
        line(
          rand,
          hx,
          hy,
          hx - Math.cos(angle + side * 0.5) * size,
          hy - Math.sin(angle + side * 0.5) * size,
          0.5,
        )
      stroke(`${wing(1)} ${wing(-1)}`)
    }
    head(ex, ey, mx, my)
    if (edge.both) head(sx, sy, mx, my)
    if (edge.label) {
      const lx = 0.25 * sx + 0.5 * mx + 0.25 * ex
      const ly = 0.25 * sy + 0.5 * my + 0.25 * ey
      const lines = edge.label.split('\n')
      const width = Math.max(...lines.map((text) => text.length)) * 9.6 + 18
      const height = lines.length * 22 + 10
      parts.push(
        `<g class="dd-text" style="--i:${order}"><rect class="dd-chip" x="${round(lx - width / 2)}" y="${round(ly - height / 2)}" width="${round(width)}" height="${round(height)}" rx="6"/>${textBlock(lx, ly, lines, 17, 'dd-edge')}</g>`,
      )
    }
  }

  for (const text of doodle.texts ?? []) {
    const size = text.size ?? 20
    const transform = text.rotate ? ` transform="rotate(${text.rotate} ${text.x} ${text.y})"` : ''
    parts.push(
      `<g class="dd-text" style="--i:${order}"${transform}>${textBlock(text.x, text.y, text.text.split('\n'), size, text.muted ? 'dd-detail' : 'dd-note', text.anchor ?? 'middle')}</g>`,
    )
  }

  return `<svg class="doodle" viewBox="0 0 ${doodle.width} ${doodle.height}" role="img" aria-label="${escape(doodle.title)}" style="--strokes:${order}">${parts.join('')}</svg>`
}
