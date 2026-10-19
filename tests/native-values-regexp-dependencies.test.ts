// SPDX-License-Identifier: MIT
import { runInNewContext } from 'node:vm'

import { describe, expect, it } from 'vitest'

import { createNativeValueCodec } from '../src/native-values.js'
import { type UntrustedInput } from '../src/untrusted.js'

const codecBody = createNativeValueCodec.toString()
function run(body: string, methods: readonly string[]): UntrustedInput {
  const poison = methods
    .map(
      (name) =>
        `RegExp.prototype[${JSON.stringify(name)}]=()=>{throw new Error('Actual authored RegExp ${name} poison')};`,
    )
    .join('')
  return runInNewContext(
    `(()=>{${poison}const codec=(${codecBody})();${body}})()`,
  ) as UntrustedInput
}

for (const methods of [['exec'], ['test'], ['exec', 'test']]) {
  describe(`native value codec without RegExp ${methods.join('/')} dependencies`, () => {
    it('encodes ordinary object fields and plain text without invoking author regex methods', () => {
      expect(
        run("return JSON.stringify(codec.encode({text:'ordinary text',answer:42}))", methods),
      ).toBe('["object",1,[["text",["primitive","ordinary text"]],["answer",["primitive",42]]]]')
    })
    it('keeps valid surrogate pairs primitive and only tags lone UTF16 units', () => {
      expect(
        run('return JSON.stringify(codec.encode(String.fromCharCode(0xd83d,0xde80)))', methods),
      ).toBe('["primitive","🚀"]')
      expect(
        run(
          'return JSON.stringify(codec.encode(String.fromCharCode(0xd800,0x61,0xdfff,0xd83d,0xde80)))',
          methods,
        ),
      ).toBe('["utf16","d8000061dfffd83dde80"]')
      expect(
        run(
          'return JSON.stringify(codec.encode(String.fromCharCode(0xd800,0xd800,0xdc00)))',
          methods,
        ),
      ).toBe('["utf16","d800d800dc00"]')
    })
    it('decodes exact lowercase hex units including empty UTF16 text', () => {
      expect(
        run(
          "const text=codec.decode(['utf16','d8000061dfff']);return JSON.stringify([text.length,text.charCodeAt(0),text.charCodeAt(1),text.charCodeAt(2),codec.decode(['utf16',''])])",
          methods,
        ),
      ).toBe('[3,55296,97,57343,""]')
    })
    it('rejects malformed hex without executing regex methods', () => {
      for (const text of ['D800', 'd80', 'zzzz', 'd80\n', 'd800z000', '\uFF14\uFF14\uFF14\uFF14'])
        expect(() =>
          run(`return codec.decode(['utf16',${JSON.stringify(text)}])`, methods),
        ).toThrow('Invalid native serialized value')
    })
    it('preserves genuine own getter exceptions and rejects Performance tag spoofing', () => {
      expect(() =>
        run(
          "return codec.encode({get value(){throw new Error('Actual enumerable getter failure')}})",
          methods,
        ),
      ).toThrow('Actual enumerable getter failure')
      expect(
        run(
          "return JSON.stringify(codec.encode({[Symbol.toStringTag]:'Performance',answer:42,toJSON(){throw new Error('Spoof toJSON must not run')}}))",
          methods,
        ),
      ).toBe('["object",1,[["answer",["primitive",42]],["toJSON",["empty"]]]]')
      expect(() =>
        run(
          "return codec.encode({get [Symbol.toStringTag](){throw new Error('Actual tag getter failure')}})",
          methods,
        ),
      ).toThrow('Actual tag getter failure')
    })
    it('keeps exact recognized Performance-family tags and genuine toJSON failures', () => {
      for (const name of [
        'Performance',
        'PerformanceTiming',
        'PerformanceNavigation',
        'PerformanceMeasure',
        'PerformanceMark',
        'PerformanceResourceTiming',
        'PerformanceNavigationTiming',
      ]) {
        const define = `const name=${JSON.stringify(name)};globalThis[name]=class{get [Symbol.toStringTag](){return name}toJSON(){return {answer:42}}};`
        expect(
          run(`${define}return JSON.stringify(codec.encode(new globalThis[name]()))`, methods),
        ).toBe('["object",1,[["answer",["primitive",42]]]]')
      }
      expect(() =>
        run(
          "globalThis.Performance=class{get [Symbol.toStringTag](){return 'Performance'}toJSON(){throw new Error('Actual platform-shaped toJSON failure')}};return codec.encode(new Performance())",
          methods,
        ),
      ).toThrow('Actual platform-shaped toJSON failure')
    })
  })
}
