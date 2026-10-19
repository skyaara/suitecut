// SPDX-License-Identifier: MIT
#pragma once
// Native utility-world builtin query AST generation. These are authored selector
// candidates over the real selected Element, never parsed human diagnostics.
// A candidate is accepted only after exact actual Element identity validation.
inline constexpr char kSuiteCutLocatorNormalization[] = R"JS(function(query){
  const target=this;
  if(!(target instanceof Element)||target.ownerDocument!==document||!target.isConnected)throw new Error('Native locator normalization requires a connected Element in the current document');
  if(typeof query!=='function')throw new Error('Native locator normalization query helper unavailable');
  const budget={remaining:40000},options={nativeDiagnosticsBudget:budget},tools=query([],document,{...options,accessibilityTools:true});
  let candidates=0,work=0;
  const tick=()=>{if(++work>100000)throw new Error('Native locator normalization ancestry work limit exceeded');};
  const accept=(steps,kind)=>{
    if(++candidates>16)throw new Error('Native locator normalization candidate limit exceeded');
    const encoded=JSON.stringify(steps);
    if(encoded.length>16384||new TextEncoder().encode(encoded).byteLength>16384)return null;
    const nodes=query(steps,document,options);
    return nodes.length===1&&nodes[0]===target?JSON.stringify({query:steps,kind}):null;
  };
  const attribute=name=>{if(name.length>256)return null;const value=target.getAttribute(name);return value===null?null:accept([{engine:'testId',attribute:name,text:value,exact:true}],'testId');};
  for(const name of ['data-testid','data-test-id','data-test']){const result=attribute(name);if(result)return result;}
  if(target.id){const result=accept([{engine:'css',selector:'#'+CSS.escape(target.id)}],'id');if(result)return result;}
  const role=tools.role(target);
  if(role){const result=accept([{engine:'role',role,name:{text:tools.name(target),exact:true}}],'role');if(result)return result;}
  for(const [name,engine]of [['placeholder','placeholder'],['alt','alt'],['title','title']]){
    const text=target.getAttribute(name);if(text!==null){const result=accept([{engine,text,exact:true}],'attribute');if(result)return result;}
  }
  // Configured test IDs are explicit attribute data in the returned AST. A
  // bounded actual data-* scan also supports custom SDK test-ID attributes
  // without depending on mutable host-global selector settings afterward.
  const attributes=target.attributes;
  for(let index=0;index<Math.min(attributes.length,64);++index){
    const name=attributes.item(index)?.name;if(typeof name!=='string')continue;
    if(!/^data-/u.test(name)||['data-testid','data-test-id','data-test'].includes(name))continue;
    if(candidates>=15)break;
    const result=attribute(name);if(result)return result;
  }
  const segments=[];let node=target,depth=0;
  while(node){
    const parts=[];let ancestor=node;
    while(ancestor){
      tick();if(++depth>64)throw new Error('Native locator normalization ancestry depth exceeded');
      let index=1;
      for(let previous=ancestor.previousElementSibling;previous;previous=previous.previousElementSibling){tick();if(previous.localName===ancestor.localName&&previous.namespaceURI===ancestor.namespaceURI)++index;}
      parts.unshift(CSS.escape(ancestor.localName)+':nth-of-type('+index+')');
      if(!ancestor.parentElement)break;
      ancestor=ancestor.parentElement;
    }
    segments.unshift({engine:'css',selector:parts.join(' > ')});
    const root=ancestor?.getRootNode();
    node=root instanceof ShadowRoot?root.host:null;
    if(!node&&root!==document)throw new Error('Native locator normalization root changed');
  }
  const result=accept(segments,'path');
  if(!result)throw new Error('Native locator normalization found no unique builtin selector for the actual Element');
  return result;
})JS";
