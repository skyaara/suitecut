// SPDX-License-Identifier: MIT
#pragma once
// Authored semantic snapshot over real composed DOM/CSS/ARIA. This does not
// alter Chromium's independent raw Accessibility.getFullAXTree capture.
inline constexpr char kSuiteCutDOMSemanticSnapshot[] = R"JS(function(scope,query,options={}){
  if(!(scope instanceof Element))throw new Error('Native semantic snapshot requires an element');
  const tools=query([],scope,{accessibilityTools:true,nativeControls:options.nativeControls});let work=0,visitedCount=0,outputCount=0,textUnits=0;const counted=new WeakSet(),fileControls=[],referenceNodes=new WeakMap(),passwordControls=new Set();
  if(options.captureReferences!==undefined&&typeof options.captureReferences!=='boolean')throw new Error('Invalid native semantic reference capture option');
  const tick=()=>{if(++work>1000000)throw new Error('Native semantic snapshot work limit exceeded');};
  const styles=new WeakMap(),roles=new WeakMap();
  const style=e=>{tick();let value=styles.get(e);if(!value){value=getComputedStyle(e);styles.set(e,value);}return value;};
  const role=e=>{tick();let value=roles.get(e);if(value===undefined){value=['iframe','frame'].includes(e.localName)?'iframe':tools.role(e);roles.set(e,value);}return value;};
  const parent=e=>e.assignedSlot||e.parentElement||e.getRootNode().host||null;
  const hidden=e=>{if(tools.hidden(e))return true;let depth=0;for(let p=e;p;p=parent(p)){tick();if(++depth>64)throw new Error('Native semantic snapshot ancestor depth limit exceeded');const s=style(p);if(s.display==='none'||s.visibility==='hidden'||s.visibility==='collapse'||p.getAttribute('aria-hidden')==='true')return true;const up=parent(p);if(up?.localName==='details'&&!up.open&&p!==up.querySelector('summary'))return true;}return false;};
  const physical=e=>{tick();if(e.localName==='iframe'||e.localName==='frame')return [];if(e.localName==='slot'){const assigned=e.assignedNodes({flatten:true});return assigned.length?[...assigned]:[...e.childNodes];}return [...(e.shadowRoot?e.shadowRoot.childNodes:e.childNodes)];};
  const owners=new WeakMap(),owned=new WeakMap();const indexed=new Set(),index=[scope.ownerDocument.documentElement];
  while(index.length){tick();const e=index.pop();if(!e||indexed.has(e))continue;indexed.add(e);if(++visitedCount>100000)throw new Error('Native semantic snapshot DOM node limit exceeded');if(e.nodeType!==1)continue;
    if(options.ai===true&&e instanceof HTMLInputElement&&e.type==='password')passwordControls.add(e);
    const targets=[];for(const id of (e.getAttribute('aria-owns')||'').trim().split(/\s+/).filter(Boolean)){tick();const target=tools.byId(e,id);if(!target||owners.has(target)||target===e||target.contains(e))continue;let cyclic=false,steps=0;for(let p=e;p;p=owners.get(p)||parent(p)){tick();if(++steps>128)throw new Error('Native semantic ownership depth limit exceeded');if(p===target){cyclic=true;break;}}if(!cyclic){owners.set(target,e);targets.push(target);}}
    owned.set(e,targets);const nested=physical(e);for(let i=nested.length-1;i>=0;--i)index.push(nested[i]);
  }
  // Password values must not enter AI names through embedded textboxes in a
  // label or containing control. The query name helper already tracks visited
  // elements; seed that set with other password controls without touching DOM.
  const name=e=>{if(options.ai!==true)return tools.name(e);const password=passwordControls.delete(e);try{return tools.name(e,{},passwordControls);}finally{if(password)passwordControls.add(e);}};
  const children=e=>{const result=physical(e).filter(node=>!owners.has(node)||owners.get(node)===e);for(const node of owned.get(e)||[])if(!result.includes(node))result.push(node);return result;};
  const block=display=>!!display&&display!=='none'&&display!=='contents'&&!display.startsWith('inline');
  const pseudo=(e,kind)=>{tick();return tools.pseudo(e,kind);};
  const box=e=>{tick();let rect;if(e.nodeType===3){const range=e.ownerDocument.createRange();range.selectNodeContents(e);rect=range.getBoundingClientRect();range.detach();}else rect=e.getBoundingClientRect();return [rect.x,rect.y,rect.width,rect.height];};
  const mergeBox=(a,b)=>{if(!a||!b)return a||b;if(a[2]===0&&a[3]===0)return b;if(b[2]===0&&b[3]===0)return a;const x=Math.min(a[0],b[0]),y=Math.min(a[1],b[1]);return [x,y,Math.max(a[0]+a[2],b[0]+b[2])-x,Math.max(a[1]+a[3],b[1]+b[3])-y];};
  const append=(out,node)=>{tick();if(node.role==='text'&&out.at(-1)?.role==='text'){const last=out.at(-1);last.text+=node.text;if(options.boxes)last.box=mergeBox(last.box,node.box);return;}if(!counted.has(node)){counted.add(node);if(++outputCount>4096)throw new Error('Native semantic snapshot node limit exceeded');}out.push(node);};
  const text=(out,value,e)=>{if(!value)return;if((textUnits+=value.length)>65536)throw new Error('Native semantic snapshot text limit exceeded');const node={role:'text',text:value};if(options.boxes)node.box=box(e);append(out,node);};
  const supports={checked:['checkbox','menuitemcheckbox','menuitemradio','radio','switch'],pressed:['button'],selected:['gridcell','option','row','tab','rowheader','columnheader','treeitem'],expanded:['application','button','checkbox','combobox','gridcell','link','listbox','menuitem','row','rowheader','tab','treeitem'],level:['heading','listitem','row','treeitem']};
  const stack=[{element:scope,entered:false,next:0,result:[],depth:0}],active=new Set(),emitted=new Set();let forest=[];
  while(stack.length){tick();const frame=stack.at(-1),e=frame.element;
    if(!frame.entered){if(frame.depth>64)throw new Error('Native semantic snapshot depth limit exceeded');if(active.has(e))throw new Error('Native semantic snapshot ownership cycle');if(emitted.has(e)){stack.pop();continue;}emitted.add(e);active.add(e);frame.entered=true;
      if(e.nodeType===3){text(frame.result,e.nodeValue||'',e);frame.children=[];}else if(e.nodeType!==1||['script','style','noscript'].includes(e.localName)||hidden(e)){frame.excluded=true;frame.children=[];}else{if(options.collectNativeControls&&e instanceof HTMLInputElement&&e.type==='file'){if(fileControls.length>=128)throw new Error('Native UA control capacity exceeded');fileControls.push(e);}frame.role=role(e);frame.children=children(e);text(frame.result,pseudo(e,'::before'),e);}
    }
    if(frame.next<frame.children.length){const child=frame.children[frame.next++];stack.push({element:child,entered:false,next:0,result:[],depth:frame.depth+1});continue;}
    let result=frame.result;
    if(e.nodeType===1&&!frame.excluded){text(result,pseudo(e,'::after'),e);if(block(style(e).display)){if(result[0]?.role==='text')result[0].text=' '+result[0].text;if(result.at(-1)?.role==='text')result.at(-1).text+=' ';}
      // Actual native AX metadata supplies UA control semantics inaccessible
      // to author DOM (not a localized label inferred from input attributes).
      const native=e instanceof HTMLInputElement&&e.type==='file'?options.nativeControls?.get(e):undefined;
      if(native){
        result=native;const domName=name(e);
        if(result.length===1){const node=result[0];if(domName||options.ai===true)node.name=domName;if(options.captureReferences)referenceNodes.set(node,e);if(options.boxes)node.box=box(e);const invalid=e.getAttribute('aria-invalid');if(node.states?.invalid===true&&['grammar','spelling'].includes(invalid))node.invalidToken=invalid;}
      }
      // AI discovery supplements standard ARIA semantics with real DOM controls.
      // Cursor is inherited: emit a generic pointer scope only where it starts,
      // rather than every decorative descendant inside a clickable container.
      const ai=options.ai===true,up=ai?parent(e):null;
      const aiGeneric=ai&&(!frame.role||['none','presentation','generic'].includes(frame.role))&&(
        e instanceof HTMLInputElement||e===scope||e.tabIndex>=0||e.hasAttribute('tabindex')||e.hasAttribute('onclick')||typeof e.onclick==='function'||
        (e.isContentEditable&&!up?.isContentEditable)||
        (style(e).cursor==='pointer'&&(!up||style(up).cursor!=='pointer')));
      // Password/date/etc inputs have no implicit ARIA textbox role. Keep them
      // discoverable by reference/label without advertising a role query that
      // the standards-based matcher cannot resolve.
      const r=aiGeneric?'generic':frame.role;if(!native&&r&&(aiGeneric||!['none','presentation','generic'].includes(r))){
        const node={role:r,name:name(e),children:result};if(options.captureReferences)referenceNodes.set(node,e);if(node.name.length>65536)throw new Error('Native semantic snapshot name limit exceeded');const states={};if(tools.state(e,'disabled'))states.disabled=true;
        // Snapshot presence is distinct from selector matching's default false:
        // ordinary buttons have no toggle state; explicit false remains real.
        for(const key of ['checked','pressed','selected','expanded','level'])if(supports[key].includes(r)){if(key==='pressed'&&!['true','false','mixed'].includes(e.getAttribute('aria-pressed')))continue;const value=tools.state(e,key);if(value!==undefined&&(key!=='level'||(Number.isInteger(value)&&value>=1&&value<=100000)))states[key]=key==='checked'&&r==='radio'&&value==='mixed'?false:value;}
        const invalid=e.getAttribute('aria-invalid');if(invalid!==null)states.invalid=invalid==='false'?false:['grammar','spelling'].includes(invalid)?invalid:true;else if(e.validity?.valid===false)states.invalid=true;
        if(options.ai===true&&e.ownerDocument.activeElement===e)states.active=true;
        if(options.ai===true&&style(e).cursor==='pointer')node.cursor='pointer';
        if(ai&&(e instanceof HTMLInputElement||e instanceof HTMLTextAreaElement||e instanceof HTMLSelectElement)){
          if(e instanceof HTMLInputElement)node.inputType=e.type;
          node.readOnly=!!e.readOnly||e.getAttribute('aria-readonly')==='true';
          node.editable=query([],e,{elementStateOnly:true}).editable===true;
        }
        if(Object.keys(states).length)node.states=states;
        const properties={};if(r==='link'&&e.hasAttribute('href'))properties.url=e.getAttribute('href');
        if((e instanceof HTMLInputElement||e instanceof HTMLTextAreaElement)&&(['textbox','searchbox'].includes(r)||aiGeneric)){if(!(e instanceof HTMLInputElement&&e.type==='password'))node.text=tools.normalize(e.value);const placeholder=e.getAttribute('placeholder');if(placeholder&&tools.normalize(placeholder)!==tools.normalize(node.name))properties.placeholder=placeholder;node.children=[];}
        if(Object.keys(properties).length)node.properties=properties;if(options.boxes)node.box=box(e);
        if(node.children.length===1&&node.children[0].role==='text'&&tools.normalize(node.children[0].text)===tools.normalize(node.name))node.children=[];
        result=[node];
      }
    }
    active.delete(e);stack.pop();const destination=stack.length?stack.at(-1).result:forest;for(const node of result)append(destination,node);
  }
  if(options.collectNativeControls)return fileControls;
  const lists=[forest];let finalCount=0;while(lists.length){tick();const list=lists.pop();for(let i=0;i<list.length;){if(++finalCount>4096)throw new Error('Native semantic snapshot node limit exceeded');const node=list[i];if(node.role==='text'){node.text=tools.normalize(node.text);if(!node.text){list.splice(i,1);continue;}}if(node.children)lists.push(node.children);++i;}}
  if(new TextEncoder().encode(JSON.stringify(forest)).byteLength>65536)throw new Error('Native semantic snapshot exceeds 64 KiB');if(options.captureReferences){const elements=[],entries=[],elementIndexes=new Map(),pending=[...forest].reverse();while(pending.length){tick();const node=pending.pop(),element=referenceNodes.get(node);if(element){let index=elementIndexes.get(element);if(index===undefined){if(elements.length>=4096)throw new Error('Native semantic reference limit exceeded');index=elements.length;elementIndexes.set(element,index);elements.push(element);}entries.push([node,index]);}if(node.children)for(let i=node.children.length-1;i>=0;--i)pending.push(node.children[i]);}return {forest,elements,entries};}return forest;
})JS";
