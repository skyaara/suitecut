// SPDX-License-Identifier: MIT
import { readFileSync } from 'node:fs'
import { Script } from 'node:vm'

import { describe, expect, it } from 'vitest'

import { type UntrustedInput } from '../src/untrusted.js'

const header = readFileSync(new URL('../native/shared/dom_query.h', import.meta.url), 'utf8')
const helper = header.slice(header.indexOf('R"JS(') + 5, header.lastIndexOf(')JS";'))

// Execute the production C++ literal unchanged over controlled DOM boundaries.
// These checks establish helper semantics, not real-browser performance.
function observe(body: string): UntrustedInput {
  const value = new Script(`(() => {
    const styleReads=[],matchesReads=[];
    const document={nodeType:9,children:[],documentElement:null,getElementById(id){return all(this).find(node=>node.id===id)||null}};
    const all=root=>{const result=[];for(const node of root.children||[]){result.push(node,...all(node))}return result};
    class Element {
      constructor(tag,attrs={},text=''){
        this.nodeType=1;this.localName=tag;this.nodeName=tag.toUpperCase();this.attrs=attrs;this.id=attrs.id||'';
        this.children=[];this.childNodes=text?[{nodeType:3,nodeValue:text,ownerDocument:document}]:[];this.ownerDocument=document;
        this.parentElement=null;this.parentNode=null;this.shadowRoot=null;this.assignedSlot=null;this.labels=[];
        this.type=attrs.type||'text';this.multiple=false;this.size=0;this.tabIndex=-1;this.isContentEditable=false;this.open=false;
        this.style={display:'block',visibility:'visible',content:'none'};this.before={display:'inline',visibility:'visible',content:'none'};this.after={...this.before};
      }
      append(...nodes){for(const node of nodes){node.parentElement=this;node.parentNode=this;this.children.push(node);this.childNodes.push(node)}}
      getAttribute(key){return Object.hasOwn(this.attrs,key)?this.attrs[key]:null}
      hasAttribute(key){return Object.hasOwn(this.attrs,key)}
      getRootNode(){return this.parentElement?this.parentElement.getRootNode():this.root||document}
      querySelector(selector){return all(this).find(node=>node.matches(selector))||null}
      matches(selector){matchesReads.push([this.id,selector]);if(selector==='*')return true;if(selector.startsWith('#'))return this.id===selector.slice(1).replace(/\\\\(.)/g,'$1');if(selector.startsWith('.'))return (this.attrs.class||'').split(/\\s+/).includes(selector.slice(1));if(selector===':disabled')return false;const attribute=/^\\[([\\w-]+)="([^"]*)"\\]$/.exec(selector);if(attribute)return this.getAttribute(attribute[1])===attribute[2];if(selector==='[')throw new SyntaxError('Invalid selector');return this.localName===selector}
    }
    const html=new Element('html',{id:'html'});document.documentElement=html;document.children=[html];
    const shadow=host=>{const root={nodeType:11,host,children:[],childNodes:[],ownerDocument:document};host.shadowRoot=root;root.append=(...nodes)=>{for(const node of nodes){node.root=root;node.parentNode=root;root.children.push(node);root.childNodes.push(node)}};return root};
    const getComputedStyle=(node,kind)=>{styleReads.push([node.id,kind||'normal']);return {...(kind==='::before'?node.before:kind==='::after'?node.after:node.style)}};
    const CSS={escape:value=>String(value).replace(/[^\\w-]/g,char=>'\\\\'+char)};
    const query=(${helper});
    ${body}
  })()`).runInNewContext() as UntrustedInput
  if (typeof value !== 'string') throw new Error('Native helper fixture did not return JSON')
  return JSON.parse(value) as UntrustedInput
}

describe('native DOM query allocation and traversal fast paths', () => {
  it('returns the exact document or element scope without enumerating descendants', () => {
    expect(
      observe(`
      const child=new Element('section',{id:'scope'});html.append(child);
      Object.defineProperty(html,'children',{get(){throw Error('Scope query must not enumerate')}});
      Object.defineProperty(child,'children',{get(){throw Error('Element scope must not enumerate')}});
      return JSON.stringify({document:query([{engine:'css',selector:':scope'}]).map(node=>node.id),element:query([{engine:'css',selector:':scope'}],child).map(node=>node.id),matchesReads});
    `),
    ).toEqual({ document: ['html'], element: ['scope'], matchesReads: [] })
  })

  it('preserves duplicate IDs, composed traversal order, hidden nodes and root exclusion', () => {
    expect(
      observe(`
      const host=new Element('div',{id:'host'}),light=new Element('span',{id:'dup'}),inside=new Element('button',{id:'dup'}),sibling=new Element('div',{id:'dup'});
      light.attrs['data-order']='light';inside.attrs['data-order']='shadow';sibling.attrs['data-order']='sibling';inside.style.display='none';
      host.append(light);shadow(host).append(inside);html.append(host,sibling);
      return JSON.stringify({ids:query([{engine:'css',selector:'#dup'}]).map(node=>node.getAttribute('data-order')),root:query([{engine:'css',selector:'#host'}],host).length,light:query([{engine:'css-light',selector:'#dup'}]).map(node=>node.getAttribute('data-order'))});
    `),
    ).toEqual({ ids: ['light', 'shadow', 'sibling'], root: 0, light: ['light', 'sibling'] })
  })

  it('preserves escaped IDs and colon-containing attribute values in the fallback matcher', () => {
    expect(
      observe(`
      html.append(new Element('div',{id:'a:b','data-key':'a:b'}));
      return JSON.stringify({escaped:query([{engine:'css',selector:'#a\\\\:b'}]).map(node=>node.id),attribute:query([{engine:'css',selector:'[data-key="a:b"]'}]).map(node=>node.id)});
    `),
    ).toEqual({ escaped: ['a:b'], attribute: ['a:b'] })
  })

  it('retains dynamic and contextual implicit roles when the static mapping is shared', () => {
    expect(
      observe(`
      const tools=query([],document,{accessibilityTools:true});
      const values=[['button',{},'button'],['textarea',{},'textbox'],['a',{},''],['a',{href:'/go'},'link'],['area',{href:'/map'},'link'],['img',{alt:''},'presentation'],['img',{alt:'Picture'},'img'],['input',{type:'search'},'searchbox'],['input',{type:'search',list:'items'},'combobox'],['input',{type:'text'},'textbox'],['input',{type:'password'},''],['input',{type:'checkbox'},'checkbox'],['th',{scope:'row'},'rowheader'],['th',{},'columnheader'],['section',{},''],['section',{'aria-label':'Named'},'region'],['div',{role:'unknown switch'},'switch'],['button',{role:'presentation','aria-label':'Action'},'button']];
      const roles=values.map(([tag,attrs,expected])=>[tools.role(new Element(tag,attrs)),expected]);
      const select=new Element('select');roles.push([tools.role(select),'combobox']);select.multiple=true;roles.push([tools.role(select),'listbox']);
      const section=new Element('section'),header=new Element('header');section.append(header);roles.push([tools.role(header),'']);
      return JSON.stringify(roles.every(([actual,expected])=>actual===expected));
    `),
    ).toBe(true)
  })

  it('shares hidden-style reads during one observation and refreshes the next observation', () => {
    expect(
      observe(`
      const wrapper=new Element('div',{id:'wrapper'}),first=new Element('button',{id:'first'},'Action one'),second=new Element('button',{id:'second'},'Action two');wrapper.append(first,second);html.append(wrapper);
      const steps=[{engine:'css',selector:':scope'},{engine:'role',role:'button',name:{text:'Action',exact:false}}];
      const before=query(steps).map(node=>node.id),readsBefore=styleReads.filter(([id,kind])=>id==='wrapper'&&kind==='normal').length;
      wrapper.style.display='none';const after=query(steps).map(node=>node.id),readsAfter=styleReads.filter(([id,kind])=>id==='wrapper'&&kind==='normal').length;
      return JSON.stringify({before,after,readsBefore,readsAfter});
    `),
    ).toEqual({ before: ['first', 'second'], after: [], readsBefore: 1, readsAfter: 2 })
  })

  it('preserves visibility overrides, collapsed details and shadow-slot hiding', () => {
    expect(
      observe(`
      const wrapper=new Element('div',{id:'wrapper'}),overridden=new Element('button',{id:'override'});wrapper.style.visibility='hidden';overridden.style.visibility='visible';wrapper.append(overridden);
      const details=new Element('details'),summary=new Element('summary'),summaryButton=new Element('button',{id:'summary'}),collapsed=new Element('button',{id:'collapsed'});summary.append(summaryButton);details.append(summary,collapsed);
      const host=new Element('div'),slot=new Element('slot'),slotted=new Element('button',{id:'slotted'}),unslotted=new Element('button',{id:'unslotted'});host.append(slotted,unslotted);shadow(host).append(slot);slotted.assignedSlot=slot;
      html.append(wrapper,details,host);
      return JSON.stringify(query([{engine:'role',role:'button'}]).map(node=>node.id));
    `),
    ).toEqual(['override', 'summary', 'slotted'])
  })

  it('observes style mutations made by registered custom selectors between query steps', () => {
    expect(
      observe(`
      const wrapper=new Element('div',{id:'wrapper'}),button=new Element('button',{id:'target'});wrapper.append(button);html.append(wrapper);
      const source='()=>({query(){return null},queryAll(root){root.ownerDocument.getElementById("wrapper").style.display="none";return [root.ownerDocument.documentElement]}})';
      const actual=query([{engine:'role',role:'button'},{engine:'custom',name:'flip',selector:''},{engine:'role',role:'button'}],document,{engines:{flip:{source}}});
      return JSON.stringify({ids:actual.map(node=>node.id),hidden:wrapper.style.display});
    `),
    ).toEqual({ ids: [], hidden: 'none' })
  })

  it('keeps exported accessibility tools fresh after the original helper returns', () => {
    expect(
      observe(`
      const button=new Element('button');html.append(button);const tools=query([],document,{accessibilityTools:true});const before=tools.hidden(button);button.style.display='none';const after=tools.hidden(button);return JSON.stringify({before,after});
    `),
    ).toEqual({ before: false, after: true })
  })

  it('invalidates styles around factories already retained in a renderer registry', () => {
    expect(
      observe(`
      const wrapper=new Element('div',{id:'wrapper'}),button=new Element('button',{id:'target'});wrapper.append(button);html.append(wrapper);
      const source='()=>({query(){return null},queryAll(root){const doc=root.nodeType===9?root:root.ownerDocument;doc.getElementById("wrapper").style.display="none";return [doc.documentElement]}})';
      query([{engine:'custom',name:'flip',selector:''}],document,{registryKey:'retainedEngines',engines:{flip:{source}}});wrapper.style.display='block';
      const actual=query([{engine:'role',role:'button'},{engine:'custom',name:'flip',selector:''},{engine:'role',role:'button'}],document,{registryKey:'retainedEngines'});
      return JSON.stringify(actual.map(node=>node.id));
    `),
    ).toEqual([])
  })

  it('retains generated content, referenced hidden names and accessible-name cycles', () => {
    expect(
      observe(`
      const generated=new Element('button',{id:'generated'},'Inner');generated.before.content='"Prefix "';generated.after.content='" Suffix"';
      const hiddenLabel=new Element('span',{id:'label'},'Hidden reference');hiddenLabel.style.display='none';const referenced=new Element('button',{id:'reference','aria-labelledby':'label'});
      const cyclic=new Element('button',{id:'cycle','aria-labelledby':'cycle'},'Own text');html.append(generated,hiddenLabel,referenced,cyclic);
      return JSON.stringify({generated:query([{engine:'role',role:'button',name:{text:'Prefix Inner Suffix',exact:true}}]).map(node=>node.id),referenced:query([{engine:'role',role:'button',name:{text:'Hidden reference',exact:true}}]).map(node=>node.id),cycle:query([{engine:'role',role:'button',name:{text:'',exact:true}}]).map(node=>node.id)});
    `),
    ).toEqual({ generated: ['generated'], referenced: ['reference'], cycle: ['cycle'] })
  })
})
