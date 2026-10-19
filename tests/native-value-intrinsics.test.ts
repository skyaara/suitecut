// SPDX-License-Identifier: MIT
import { runInNewContext } from 'node:vm'

import { describe, expect, it } from 'vitest'

import { createNativeValueCodec } from '../src/native-values.js'
import { type UntrustedInput } from '../src/untrusted.js'

const codecBody = createNativeValueCodec.toString()
function run(body: string): UntrustedInput {
  return runInNewContext(
    `(()=>{globalThis.Window=function Window(){};Object.defineProperty(globalThis,'window',{get(){if(this!==globalThis)throw new TypeError('Native receiver required');return globalThis}});Object.create=null;Object.defineProperty=null;Object.getOwnPropertyDescriptor=null;Object.getOwnPropertyNames=null;Object.getPrototypeOf=null;Object.prototype.hasOwnProperty=null;const codec=(${codecBody})();${body}})()`,
  ) as UntrustedInput
}
describe('native codec independent Reflect intrinsics', () => {
  it('encodes primitive results when the author replaces legacy Object descriptor helpers', () => {
    expect(run('return JSON.stringify(codec.encode(3))')).toBe('["primitive",3]')
  })
  it('defines exact own object fields and array slots without author Object.defineProperty', () => {
    expect(
      run(
        "const value=codec.decode(['object',1,[['__proto__',['primitive','own']],['array',['array',2,[['primitive',7],['undefined']]]]]]);return JSON.stringify([Reflect.getOwnPropertyDescriptor(value,'__proto__').value,value.array.length,value.array[0],Reflect.getOwnPropertyDescriptor(value.array,'1').value===undefined])",
      ),
    ).toBe('["own",2,7,true]')
  })
  it('keeps genuine getter receiver validation and rejects self-window author object spoofing', () => {
    expect(
      run(
        'const spoof={};spoof.window=spoof;return JSON.stringify(codec.encode({actual:globalThis,spoof}))',
      ),
    ).toContain('ref: <Window>')
    expect(
      run('const spoof={};spoof.window=spoof;return JSON.stringify(codec.encode(spoof))'),
    ).not.toContain('ref: <Window>')
  })
  it('does not hide genuine enumerable author getter failures', () => {
    expect(() =>
      run(
        "const value={get actual(){throw new Error('Actual author getter failed')}};return codec.encode(value)",
      ),
    ).toThrow('Actual author getter failed')
  })
})
