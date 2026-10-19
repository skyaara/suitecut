// SPDX-License-Identifier: MIT
#pragma once
// Observation only: C++ owns strategy ranking and final recipe selection.
inline constexpr char kSuiteCutLocatorSuggestionObserve[]=R"JS(function(query){
 const target=this;if(!(target instanceof Element)||target.ownerDocument!==document||!target.isConnected)throw new Error('Locator suggestions require a connected Element in its captured document');
 const budget={remaining:40000},tools=query([],document,{nativeDiagnosticsBudget:budget,accessibilityTools:true}),candidates=[],omitted=[];
 const value=text=>typeof text==='string'&&text.length<=2048&&text.isWellFormed();
 const add=(kind,steps)=>{if(candidates.length>=24)throw new Error('Native locator suggestion candidate bound exceeded');candidates.push({kind,query:steps});};
 const attribute=name=>{const text=target.getAttribute(name);if(text===null)return;if(!value(text)||text.includes('\0')){omitted.push({kind:'testId',reason:'Attribute text exceeds supported scalar bounds'});return;}add('testId',[{engine:'testId',attribute:name,text,exact:true}]);};
 for(const name of ['data-testid','data-test-id','data-test'])attribute(name);
 let data=0;for(let i=0;i<Math.min(target.attributes.length,64);++i){const name=target.attributes.item(i)?.name;if(name&&/^data-[a-z0-9_-]+$/u.test(name)&&!['data-testid','data-test-id','data-test'].includes(name)&&data++<5)attribute(name);}
 const name=tools.name(target),role=tools.role(target);
 if(role&&value(name))add('role',[{engine:'role',role,name:{text:name,exact:true}}]);
 if(target.labels?.length||target.hasAttribute('aria-label')||target.hasAttribute('aria-labelledby')){const label=tools.name(target,{referenced:true});if(value(label)&&label)add('label',[{engine:'label',text:label,exact:true}]);}
 for(const [attr,engine]of [['placeholder','placeholder'],['alt','alt'],['title','title']]){const text=target.getAttribute(attr);if(value(text))add('attribute',[{engine,text,exact:true}]);}
 // Read a bounded light-DOM text candidate. Full selector semantics and open
 // shadow traversal remain the verifier's authority, never this observation.
 let raw='',textNode=target.firstChild,textWork=0,textAvailable=true;
 while(textNode){if(++textWork>10000){textAvailable=false;break;}if(textNode.nodeType===3){const part=textNode.nodeValue||'';if(raw.length+part.length>2048){textAvailable=false;break;}raw+=part;}if(textNode.firstChild){textNode=textNode.firstChild;continue;}while(textNode&&!textNode.nextSibling&&textNode.parentNode!==target)textNode=textNode.parentNode;if(!textNode||(!textNode.nextSibling&&textNode.parentNode===target))break;textNode=textNode.nextSibling;}
 const text=textAvailable?tools.normalize(raw):'';if(value(text)&&text)add('text',[{engine:'text',text,exact:true}]);
 if(!textAvailable)omitted.push({kind:'text',reason:'Text observation exceeded its bounded node/character window'});
 if(target.id&&value(target.id)&&!target.id.includes('\0'))add('id',[{engine:'css',selector:'#'+CSS.escape(target.id)}]);
 let classes=0,classWork=0;for(const item of target.classList){if(classes>=2||++classWork>32)break;if(/^[a-zA-Z_-][a-zA-Z_-]*$/u.test(item)){add('css',[{engine:'css',selector:CSS.escape(target.localName)+'.'+CSS.escape(item)}]);++classes;}}
 const segments=[];let node=target,depth=0,work=0;
 while(node){const parts=[];let ancestor=node;while(ancestor){if(++depth>64)throw new Error('Native locator suggestion ancestry depth exceeded');let index=1;for(let previous=ancestor.previousElementSibling;previous;previous=previous.previousElementSibling){if(++work>100000)throw new Error('Native locator suggestion ancestry work exceeded');if(previous.localName===ancestor.localName&&previous.namespaceURI===ancestor.namespaceURI)++index;}parts.unshift(CSS.escape(ancestor.localName)+':nth-of-type('+index+')');if(!ancestor.parentElement)break;ancestor=ancestor.parentElement;}segments.unshift({engine:'css',selector:parts.join(' > ')});const root=ancestor?.getRootNode();node=root instanceof ShadowRoot?root.host:null;if(!node&&root!==document)throw new Error('Native locator suggestion document changed');}
 add('path',segments);
 omitted.push({kind:'stability',reason:'Uniqueness is verified at this observation; future DOM stability is not asserted'});
 return [target,JSON.stringify({candidates,omitted})];
})JS";
// One synchronous verification boundary for ALL native-ranked candidates. Each
// query shares one finite DOM work budget and compares the exact selected node.
inline constexpr char kSuiteCutLocatorSuggestionVerify[]=R"JS(function(query,recipes){
 if(!(this instanceof Element)||this.ownerDocument!==document||!this.isConnected)throw new Error('Native locator suggestion target was detached or adopted');
 const budget={remaining:40000},results=[];
 for(const steps of recipes){const nodes=query(steps,document,{nativeDiagnosticsBudget:budget});results.push({matches:nodes.length,same:nodes.length===1&&nodes[0]===this});}
 return [this,JSON.stringify({results})];
})JS";
