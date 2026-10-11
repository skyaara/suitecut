import { launchEngine } from '../../dist/engine.js'
import { setTimeout as delay } from 'node:timers/promises'
const engine = await launchEngine({
  executablePath: process.env.SUITECUT_NATIVE_EXECUTABLE,
  width: 640,
  height: 360,
  framesPerSecond: 30,
})
try {
  for (let i = 0; i < 20; i++) {
    const context = await engine.browser.newContext()
    const page = await context.newPage()
    await page.goto('data:text/html,<canvas>')
    await context.close()
  }
  await delay(5000)
} finally {
  await engine.close()
}
