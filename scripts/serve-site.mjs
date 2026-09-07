import { readFile, stat } from 'node:fs/promises'
import { createServer } from 'node:http'
import { extname, resolve, sep } from 'node:path'
import process from 'node:process'

const argumentsList = process.argv.slice(2)
/**
 * Reads one string option from the command line.
 * @param {string} name
 * @param {string} fallback
 * @returns {string}
 */
const option = (name, fallback) => {
  const index = argumentsList.indexOf(name)
  return index === -1 ? fallback : (argumentsList[index + 1] ?? fallback)
}
const root = resolve(option('--root', 'site/dist'))
const host = option('--host', '127.0.0.1')
const port = Number(option('--port', '4322'))
if (!Number.isInteger(port) || port < 1 || port > 65_535) {
  throw new Error('--port must be an integer from 1 through 65535')
}

const contentTypes = new Map([
  ['.css', 'text/css; charset=utf-8'],
  ['.html', 'text/html; charset=utf-8'],
  ['.js', 'text/javascript; charset=utf-8'],
  ['.json', 'application/json; charset=utf-8'],
  ['.mp4', 'video/mp4'],
  ['.png', 'image/png'],
  ['.svg', 'image/svg+xml'],
  ['.wav', 'audio/wav'],
])

/**
 * Returns a file path only when it names a regular file.
 * @param {string} path
 * @returns {Promise<string | undefined>}
 */
async function existingFile(path) {
  try {
    return (await stat(path)).isFile() ? path : undefined
  } catch {
    return undefined
  }
}

/**
 * Resolves one clean request path beneath the static root.
 * @param {string} pathname
 * @returns {Promise<string | undefined>}
 */
async function routeFile(pathname) {
  const decoded = decodeURIComponent(pathname)
  const requested = resolve(root, `.${decoded}`)
  if (requested !== root && !requested.startsWith(`${root}${sep}`)) return undefined
  const direct = await existingFile(requested)
  if (direct !== undefined) return direct
  if (extname(requested) !== '') return undefined
  return existingFile(resolve(requested, 'index.html'))
}

const notFoundPath = resolve(root, '404.html')
const server = createServer((request, response) => {
  void (async () => {
    if (request.method !== 'GET' && request.method !== 'HEAD') {
      response.writeHead(405, { Allow: 'GET, HEAD' })
      response.end()
      return
    }
    const pathname = new URL(request.url ?? '/', `http://${host}:${String(port)}`).pathname
    const matchedPath = await routeFile(pathname)
    const path = matchedPath ?? notFoundPath
    const body = await readFile(path)
    response.writeHead(matchedPath === undefined ? 404 : 200, {
      'Cache-Control': 'no-store',
      'Content-Length': String(body.byteLength),
      'Content-Type': contentTypes.get(extname(path)) ?? 'application/octet-stream',
    })
    response.end(request.method === 'HEAD' ? undefined : body)
  })().catch((error) => {
    response.writeHead(500, { 'Content-Type': 'text/plain; charset=utf-8' })
    response.end(error instanceof Error ? error.message : String(error))
  })
})

await new Promise((resolvePromise, reject) => {
  server.once('error', reject)
  server.listen(port, host, () => {
    server.off('error', reject)
    process.stdout.write(`SuiteCut site ready at http://${host}:${String(port)}\n`)
    resolvePromise()
  })
})

for (const signal of ['SIGINT', 'SIGTERM']) {
  process.once(signal, () => {
    server.close(() => process.exit(0))
  })
}
