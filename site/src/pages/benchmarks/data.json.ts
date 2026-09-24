import report from '../../data/benchmark-report.json'

export function GET(): Response {
  return new Response(JSON.stringify(report, null, 2), {
    headers: { 'Content-Type': 'application/json; charset=utf-8' },
  })
}
