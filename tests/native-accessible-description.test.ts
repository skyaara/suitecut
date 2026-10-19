// SPDX-License-Identifier: MIT
import { readFile } from 'node:fs/promises'
import { Script } from 'node:vm'

import { describe, expect, it } from 'vitest'
import * as z from 'zod'

import { type UntrustedInput } from '../src/untrusted.js'

const header = await readFile(new URL('../native/shared/dom_query.h', import.meta.url), 'utf8')
const helper = /R"JS\(([\s\S]*)\)JS";/u.exec(header)?.[1]
if (!helper) throw new Error('Native query helper unavailable')
interface NodeFixture {
  tag?: string
  attrs?: Record<string, string>
  text?: string
  type?: string
}
// Exact production query literal, controlled DOM boundaries; no browser claim.
function observe(root: NodeFixture, references: NodeFixture[] = []) {
  const value: UntrustedInput = new Script(`(() => {
    const config=${JSON.stringify([root, ...references])}, ids=new Map();
    const document={getElementById:id=>ids.get(id)||null};
    class Element {
      constructor(config){this.nodeType=1;this.localName=config.tag||'button';this.nodeName=this.localName.toUpperCase();this.attrs=config.attrs||{};this.type=config.type||'text';this.children=[];this.labels=[];this.childNodes=config.text===undefined?[]:[{nodeType:3,nodeValue:config.text}];this.ownerDocument=document;}
      getAttribute(key){return Object.hasOwn(this.attrs,key)?this.attrs[key]:null;}
      hasAttribute(key){return Object.hasOwn(this.attrs,key);}
      getRootNode(){return document;}
      matches(){return false;}
    }
    const nodes=config.map(value=>new Element(value));for(const node of nodes)if(node.hasAttribute('id'))ids.set(node.getAttribute('id'),node);
    const getComputedStyle=()=>({display:'inline',visibility:'visible',content:'none'});
    return JSON.stringify((${helper})([],nodes[0],{metadata:true}));
  })()`).runInNewContext() as UntrustedInput
  if (typeof value !== 'string') throw new Error('Native metadata observation unavailable')
  return z.object({ name: z.string(), description: z.string() }).parse(JSON.parse(value))
}

describe('native accessible-description source precedence', () => {
  it('uses referenced text alternatives rather than unconditionally flattening DOM text', () => {
    expect(
      observe({ attrs: { 'aria-describedby': 'hint' }, text: 'Save' }, [
        {
          tag: 'span',
          attrs: { id: 'hint', 'aria-label': 'Actual alternative' },
          text: 'Visible fallback',
        },
      ]),
    ).toEqual({ name: 'Save', description: 'Actual alternative' })
  })
  it('keeps a title description when a separate aria-label supplied an identical name', () => {
    expect(observe({ attrs: { 'aria-label': 'Same', title: 'Same' } })).toEqual({
      name: 'Same',
      description: 'Same',
    })
  })
  it('keeps a title description when the same text names a content-named control', () => {
    expect(observe({ attrs: { title: 'Same' }, text: 'Same' })).toEqual({
      name: 'Same',
      description: 'Same',
    })
  })
  it('does not reuse a title that actually supplied the accessible name', () => {
    expect(observe({ attrs: { title: 'Title' } })).toEqual({ name: 'Title', description: '' })
    expect(
      observe({ tag: 'input', attrs: { title: 'Title', placeholder: 'Placeholder' } }),
    ).toEqual({ name: 'Title', description: '' })
  })
  it('tracks the actual image-title fallback independently of equal alternative text', () => {
    expect(observe({ tag: 'img', attrs: { alt: 'Same', title: 'Same' } })).toEqual({
      name: 'Same',
      description: 'Same',
    })
    expect(observe({ tag: 'img', attrs: { title: 'Only title' } })).toEqual({
      name: 'Only title',
      description: '',
    })
  })
  it('preserves ordered valid references, explicit description precedence and empty reference results', () => {
    const root = {
      attrs: {
        'aria-describedby': 'missing two one',
        'aria-description': 'Lower priority',
        title: 'Lowest',
      },
      text: 'Save',
    }
    expect(
      observe(root, [
        { tag: 'span', attrs: { id: 'one' }, text: 'One' },
        { tag: 'span', attrs: { id: 'two' }, text: 'Two' },
      ]).description,
    ).toBe('Two One')
    expect(observe(root).description).toBe('Lower priority')
    expect(observe(root, [{ tag: 'span', attrs: { id: 'one' } }]).description).toBe('')
  })
})
