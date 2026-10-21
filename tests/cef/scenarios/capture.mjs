// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import { test } from './helpers.mjs'
import { wait } from './helpers.mjs'

export const capture = [
  test('capture.audio-video-under-load', ['media', 'race', 'capture'], async (h) => {
    // Checks live packets and responsiveness, not a unique-frame/max-FPS qualification.
    const { page } = await h.page()
    let frames = 0,
      audio = 0,
      lastFrame = -Infinity,
      lastAudio = -Infinity,
      nonzero = false
    const failures = [],
      unframe = page.source.onFrame((f) => {
        try {
          assert(f.timestampMs >= lastFrame)
          assert.equal(f.data.length, f.width * f.height * 1.5)
          assert.equal(f.pixelFormat, 'i420')
          lastFrame = f.timestampMs
          ++frames
        } catch (e) {
          failures.push(String(e))
        }
      }),
      unaudio = page.source.onAudio((p) => {
        try {
          assert(p.timestampMs >= lastAudio)
          assert.equal(p.data.length, p.frames * 4)
          lastAudio = p.timestampMs
          audio += p.frames
          nonzero ||= p.data.some((x) => x !== 0)
        } catch (e) {
          failures.push(String(e))
        }
      })
    h.onCleanup(() => {
      unframe()
      unaudio()
    })
    await page.goto(h.fixture.origin + '/media')
    await page.locator('#audio').click()
    await wait(page, 'globalThis.audioReady===true')
    await page.evaluate(
      `globalThis.load=setInterval(()=>{const end=performance.now()+5;while(performance.now()<end)Math.sqrt(Math.random())},16)`,
    )
    await Promise.all(
      Array.from({ length: 16 }, (_, i) =>
        page.evaluate(`${i}*${i}`).then((v) => assert.equal(v, i * i)),
      ),
    )
    await h.until(
      () => frames >= 30 && audio >= 48000,
      'Sustained actual audio/video packets did not arrive',
      10000,
    )
    await page.evaluate('clearInterval(globalThis.load)')
    assert(nonzero)
    assert.deepEqual(failures, [])
    h.observe('capture', { frames, audioFrames: audio, nonzero, lastFrame, lastAudio })
    unframe()
    unaudio()
    h.check(
      'I420 pixel packets, non-silent stereo PCM and concurrent commands remain coherent under bounded renderer load',
    )
  }),
]
