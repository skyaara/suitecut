// SPDX-License-Identifier: MIT
#pragma once
// Observation only: C++ owns strategy ranking and final recipe selection.
inline constexpr char kSuiteCutLocatorSuggestionObserve[]=R"JS(function(query){
 const target=this;if(!(target instanceof Element)||target.ownerDocument!==document||!target.isConnected)throw new Error('Locator suggestions require a connected Element in its captured document');
 const tools=query([],document,{accessibilityTools:true}),candidates=[],omitted=[];
 const value=text=>typeof text==='string'&&text.isWellFormed();
 const add=(kind,steps)=>{candidates.push({kind,query:steps});};
 const attribute=name=>{const text=target.getAttribute(name);if(text===null)return;if(!value(text)||text.includes('\0')){omitted.push({kind:'testId',reason:'Attribute text is not well-formed'});return;}add('testId',[{engine:'testId',attribute:name,text,exact:true}]);};
 for(const name of ['data-testid','data-test-id','data-test'])attribute(name);
 let data=0;for(let i=0;i<target.attributes.length;++i){const name=target.attributes.item(i)?.name;if(name&&/^data-[a-z0-9_-]+$/u.test(name)&&!['data-testid','data-test-id','data-test'].includes(name)&&data++<5)attribute(name);}
 const name=tools.name(target),role=tools.role(target);
 if(role&&value(name))add('role',[{engine:'role',role,name:{text:name,exact:true}}]);
 if(target.labels?.length||target.hasAttribute('aria-label')||target.hasAttribute('aria-labelledby')){const label=tools.name(target,{referenced:true});if(value(label)&&label)add('label',[{engine:'label',text:label,exact:true}]);}
 for(const [attr,engine]of [['placeholder','placeholder'],['alt','alt'],['title','title']]){const text=target.getAttribute(attr);if(value(text))add('attribute',[{engine,text,exact:true}]);}
 // Read the light-DOM text candidate. Full selector semantics and open
 // shadow traversal remain the verifier's authority, never this observation.
 let raw='',textNode=target.firstChild;
 while(textNode){if(textNode.nodeType===3)raw+=textNode.nodeValue||'';if(textNode.firstChild){textNode=textNode.firstChild;continue;}while(textNode&&!textNode.nextSibling&&textNode.parentNode!==target)textNode=textNode.parentNode;if(!textNode||(!textNode.nextSibling&&textNode.parentNode===target))break;textNode=textNode.nextSibling;}
 const text=tools.normalize(raw);if(value(text)&&text)add('text',[{engine:'text',text,exact:true}]);
 if(target.id&&value(target.id)&&!target.id.includes('\0'))add('id',[{engine:'css',selector:'#'+CSS.escape(target.id)}]);
 let classes=0;for(const item of target.classList){if(classes>=2)break;if(/^[a-zA-Z_-][a-zA-Z_-]*$/u.test(item)){add('css',[{engine:'css',selector:CSS.escape(target.localName)+'.'+CSS.escape(item)}]);++classes;}}
 const segments=[];let node=target;
 while(node){const parts=[];let ancestor=node;while(ancestor){let index=1;for(let previous=ancestor.previousElementSibling;previous;previous=previous.previousElementSibling){if(previous.localName===ancestor.localName&&previous.namespaceURI===ancestor.namespaceURI)++index;}parts.unshift(CSS.escape(ancestor.localName)+':nth-of-type('+index+')');if(!ancestor.parentElement)break;ancestor=ancestor.parentElement;}segments.unshift({engine:'css',selector:parts.join(' > ')});const root=ancestor?.getRootNode();node=root instanceof ShadowRoot?root.host:null;if(!node&&root!==document)throw new Error('Native locator suggestion document changed');}
 add('path',segments);
 omitted.push({kind:'stability',reason:'Uniqueness is verified at this observation; future DOM stability is not asserted'});
 return [target,JSON.stringify({candidates,omitted})];
})JS";
// One synchronous verification boundary for ALL native-ranked candidates. Each
// query compares the exact selected node.
inline constexpr char kSuiteCutLocatorSuggestionVerify[]=R"JS(function(query,recipes){
 if(!(this instanceof Element)||this.ownerDocument!==document||!this.isConnected)throw new Error('Native locator suggestion target was detached or adopted');
 const results=[];
 for(const steps of recipes){const nodes=query(steps,document);results.push({matches:nodes.length,same:nodes.length===1&&nodes[0]===this});}
 return [this,JSON.stringify({results})];
})JS";
