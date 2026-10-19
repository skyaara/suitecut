// SPDX-License-Identifier: MIT
#pragma once
// Authored diagnostic projection over the exact already matched DOM nodes.
// This function runs in the utility realm. Generated builtin aliases are
// accepted only after a real query proves the original node identity.
inline constexpr char kSuiteCutQueryDiagnostics[] = R"JS(function(nodes,expression,tools){
  let work=0,queries=0;const cache=new Map(),bounded=value=>String(value).slice(0,1024);
  const spend=()=>++work<=20000;
  const literal=value=>{let out="'";for(const char of String(value)){const code=char.charCodeAt(0);out+=char==="'"?"\\'":char==='\\'?'\\\\':char.length===1&&(code<32||code>=0xd800&&code<=0xdfff)?JSON.stringify(char).slice(1,-1):char;}return out+"'";};
  const display=value=>{let out='';for(const char of String(value)){const code=char.charCodeAt(0);out+=char.length===1&&code>=0xd800&&code<=0xdfff?JSON.stringify(char).slice(1,-1):char;}return out;};
  const normalize=value=>String(value).replace(/[\u200b\u00ad]/g,'').replace(/\s+/g,' ').trim();
  const text=node=>{let value='',visited=0;const stack=[node];while(stack.length&&value.length<256&&visited++<100&&spend()){const current=stack.pop();if(current.nodeType===3){value+=(current.nodeValue||'').slice(0,256-value.length);continue;}if(current.nodeType!==1&&current.nodeType!==11||['SCRIPT','STYLE','HEAD'].includes(current.nodeName))continue;const children=current.childNodes;for(let index=Math.min(children.length,100)-1;index>=0;--index)stack.push(children[index]);}return normalize(value);};
  const preview=node=>{
    if(node.nodeType!==1)return display(bounded(node.nodeName+': '+text(node)));
    const tag=node.localName.length>128?node.localName.slice(0,128)+'…':node.localName,attributes=[];for(let index=0;index<Math.min(node.attributes.length,32)&&spend();++index){const attribute=node.attributes[index];if(attribute.name!=='style')attributes.push({name:attribute.name.length>128?attribute.name.slice(0,128)+'…':attribute.name,value:attribute.value});}
    attributes.sort((a,b)=>Number(b.value==='')-Number(a.value===''));let result='<'+tag;for(const attribute of attributes)result+=' '+attribute.name+(attribute.value===''?'':'='+JSON.stringify(attribute.value.slice(0,128)));
    if(['area','base','br','col','embed','hr','img','input','link','meta','param','source','track','wbr'].includes(tag))return display(bounded(result+'/>'));
    return display(bounded(result+'>'+text(node)+'</'+tag+'>'));
  };
  const resolve=query=>{const key=JSON.stringify(query);if(cache.has(key))return cache.get(key);let result=[];if(queries++<32&&spend()&&tools.remaining()>0)try{const actual=tools.query(query);if(Array.isArray(actual))result=actual;}catch{}cache.set(key,result);return result;};
  const exact=(query,node)=>{const actual=resolve(query);return actual.length===1&&actual[0]===node;};
  const indexed=(query,alias,node,firstOnly=false)=>{const actual=resolve(query),index=actual.indexOf(node);if(index<0||firstOnly&&index!==0)return undefined;return actual.length===1?alias:index===0?alias+'.first()':alias+'.nth('+index+')';};
  const parent=node=>node.parentElement||(node.getRootNode().host||null);
  const css=node=>{const values=[];if(node.id&&node.id.length<=128)values.push('#'+CSS.escape(node.id));if(node.localName.length<=128)values.push(CSS.escape(node.localName));for(let index=0;index<Math.min(node.classList.length,3);++index){const value=node.classList[index];if(value.length<=128)values.push('.'+CSS.escape(value));}return [...new Set(values)];};
  const structure=node=>{
    const parts=[];let current=node;
    for(let depth=0;current&&depth<24&&spend();++depth){if(current.localName.length>128)return undefined;const tag=CSS.escape(current.localName),above=parent(current);let part=tag;
      if(current.localName==='body'){parts.unshift('body');return parts.join(' > ');}
      if(above){const root=current.parentElement||current.getRootNode(),siblings=root.children;let index=0;while(index<Math.min(siblings.length,1000)&&siblings[index]!==current){index++;if(!spend())return undefined;}if(index===1000||siblings[index]!==current)return undefined;if(siblings.length>1)part+=':nth-child('+(index+1)+')';}
      parts.unshift(part);current=above;
    }
    return !current?parts.join(' > '):undefined;
  };
  // Current human descriptions intentionally abbreviate some rich AST options.
  // Only complete raw-selector/index descriptions may serve as fallback aliases.
  const originalFaithful=Array.isArray(tools.originalQuery)&&tools.originalQuery.length>0&&tools.originalQuery.every(step=>step&&['css','nth'].includes(step.engine));
  const originalAlias=originalFaithful&&typeof expression==='string'&&expression.length>0&&expression.length<=512&&expression!=='native locator'&&!expression.includes('.contentFrame()')?expression:undefined;
  const aliases=(node,index)=>{
    if(node.nodeType!==1)return{};
    const value=text(node),semantic=[];
    if(value&&value.length<=128)semantic.push({query:[{engine:'text',text:value}],alias:'getByText('+literal(value)+')',text:value});
    let role,name;try{role=tools.role(node);if(role&&!['generic','none','presentation'].includes(role)){name=tools.name(node);if(name&&name.length<=128)semantic.push({query:[{engine:'role',role,name:{text:name,exact:true}}],alias:'getByRole('+literal(role)+', { name: '+literal(name)+', exact: true })'});semantic.push({query:[{engine:'role',role}],alias:'getByRole('+literal(role)+')'});}}catch{}
    for(const candidate of semantic)if(exact(candidate.query,node))return{alias:candidate.alias};
    for(const candidate of semantic){const alias=indexed(candidate.query,candidate.alias,node,true);if(alias)return{alias};}
    let ancestor=parent(node);
    for(let depth=0;ancestor&&depth<4&&spend();++depth,ancestor=parent(ancestor))for(const selector of css(ancestor))for(const candidate of semantic){
      const prefix=[{engine:'css',selector}],textFilter=candidate.text?[{engine:'filter',hasText:{text:candidate.text}}]:[],query=[...prefix,...textFilter,...candidate.query];
      const alias='locator('+literal(selector)+')'+(candidate.text?'.filter({ hasText: '+literal(candidate.text)+' })':'')+'.'+candidate.alias;
      if(alias.length<=512&&exact(query,node))return{alias};
    }
    for(let above=parent(node),depth=0;above&&depth<4&&spend();above=parent(above),++depth)for(const ancestor of css(above))for(const target of css(node)){const selector=ancestor+' '+target,alias='locator('+literal(selector)+')';if(alias.length<=512&&exact([{engine:'css',selector}],node))return{alias};}
    for(const selector of css(node)){const alias='locator('+literal(selector)+')';if(alias.length<=512&&exact([{engine:'css',selector}],node))return{alias};}
    for(const candidate of semantic){const alias=indexed(candidate.query,candidate.alias,node);if(alias)return{alias};}
    if(index===0&&originalAlias)return{alias:originalAlias+'.first()'};
    const path=structure(node),actualPath=path&&path.length<=512&&exact([{engine:'css',selector:path}],node)?path:undefined;
    return{alias:actualPath?'locator('+literal(actualPath)+')':undefined,path:actualPath};
  };
  const title='strict mode violation: '+(typeof expression==='string'&&expression.length>0&&expression.length<=1024?display(expression):'native locator')+' resolved to '+nodes.length+' elements:';
  const lines=[];
  for(let index=0;index<Math.min(nodes.length,5)&&spend();++index){const node=nodes[index];let details={};try{details=aliases(node,index);}catch{}let alias=details.alias;
    if(!alias&&originalAlias)alias=originalAlias+(index===0?'.first()':'.nth('+index+')');
    // A separately verified structural path is useful even when a shorter
    // identity-preserving semantic alias is available.
    let path=details.path;if(!path&&node.nodeType===1){const value=structure(node);if(value&&value.length<=512&&exact([{engine:'css',selector:value}],node))path=value;}
    lines.push('    '+(index+1)+') '+preview(node)+(alias?' aka '+display(alias):'')+(path&&(!alias||!alias.includes(literal(path)))?' [actual CSS: '+display(path)+']':''));
  }
  let output=title;for(const line of lines){if(output.length+line.length+48>16384){output+='\n    … diagnostic output truncated';break;}output+='\n'+line;}if(nodes.length>5)output+='\n    …';return output;
})JS";
