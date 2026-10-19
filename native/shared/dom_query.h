// SPDX-License-Identifier: MIT
#pragma once

// Authored DOM query logic. C++ supplies the query, execution world, operation
// identity, retry scheduling and deadline; no external browser driver is used.
inline constexpr char kSuiteCutDomQuery[] = R"JS(function(steps,start=document,options={},nativeQueryIntrinsics){
  // This optional object exists only in Source-authored strict main-dispatch
  // callbacks. It is captured before author code, never accepted from selector
  // IPC, exposed on globalThis, or passed to a registered engine callback.
  const nativeRegexExec=(expression,value)=>nativeQueryIntrinsics?nativeQueryIntrinsics.regexExec(expression,value):expression.exec(value);
  const nativeRegexTest=(expression,value)=>nativeQueryIntrinsics?nativeQueryIntrinsics.regexExec(expression,value)!==null:expression.test(value);
  const nativeRegexReplace=(value,expression,replacement)=>nativeQueryIntrinsics?nativeQueryIntrinsics.regexReplace(value,expression,replacement):value.replace(expression,replacement);
  const nativeRegexSplit=(value,expression)=>nativeQueryIntrinsics?nativeQueryIntrinsics.regexSplit(value,expression):value.split(expression);
  const nativeRegexCreate=(pattern,flags)=>nativeQueryIntrinsics?nativeQueryIntrinsics.regexCreate(pattern,flags):new RegExp(pattern,flags);
  const nativeNodeType=node=>nativeQueryIntrinsics?nativeQueryIntrinsics.nodeType(node):node.nodeType;
  const nativeOwnerDocument=node=>nativeQueryIntrinsics?nativeQueryIntrinsics.ownerDocument(node):node.ownerDocument;

  if(!Array.isArray(steps)||steps.length>64)throw new Error('Invalid native locator query');
  let wireWork=0;
  const wireStrings=['selector','text','pattern','flags','attribute','role','name','message'];
  const wireLists=['query','has','hasNot','descendants'];
  const wirePatterns=['name','description','hasText','hasNotText'];
  const wire=(value,encode=false,depth=0)=>{
    if(++wireWork>100000||depth>256)throw new Error('Native query wire limit exceeded');
    if(Array.isArray(value))return value.map(item=>wire(item,encode,depth+1));
    if(!value||typeof value!=='object')throw new Error('Invalid native query wire value');
    const result={...value};
    for(const key of wireStrings){
      const encoded=key+'JSON';
      if(encode){if(typeof value[key]==='string'&&!value[key].isWellFormed()){result[encoded]=JSON.stringify(value[key]);delete result[key];}}
      else if(Object.hasOwn(value,encoded)){
        if(Object.hasOwn(value,key)||typeof value[encoded]!=='string'||value[encoded].length>393218)throw new Error('Invalid native query string wire field');
        const decoded=JSON.parse(value[encoded]);if(typeof decoded!=='string'||decoded.length>65536)throw new Error('Invalid native query string wire field');
        result[key]=decoded;delete result[encoded];
      }
    }
    for(const key of wireLists)if(Array.isArray(value[key]))result[key]=wire(value[key],encode,depth+1);
    for(const key of wirePatterns)if(value[key]&&typeof value[key]==='object'&&!Array.isArray(value[key]))result[key]=wire(value[key],encode,depth+1);
    return result;
  };
  steps=wire(steps);
  let work=0,textBytes=0,nameBytes=0;
  // Native diagnostics shares this private budget across candidate alias queries.
  // It is constructed in the utility world, never read from selector IPC data.
  const diagnosticBudget=options.nativeDiagnosticsBudget;
  if(diagnosticBudget!==undefined&&(!diagnosticBudget||!Number.isSafeInteger(diagnosticBudget.remaining)||diagnosticBudget.remaining<0||diagnosticBudget.remaining>50000))throw new Error('Invalid native diagnostic query budget');
  const tick=()=>{if(++work>1000000||(diagnosticBudget!==undefined&&diagnosticBudget.remaining===0))throw new Error('Native DOM query work limit exceeded');if(diagnosticBudget!==undefined)--diagnosticBudget.remaining;};
  const validating=options.validateOnly===true,validationElement=validating?document.createElement('div'):null;
  const parent=e=>{tick();return e.parentElement||(e.getRootNode().host||null);};
  const children=(root,light=false)=>{tick();return [...(root.children||[]),...(!light&&root.shadowRoot?root.shadowRoot.children:[])];};
  const enumerate=(root,light=false)=>{
    const out=[],stack=children(root,light).reverse();
    while(stack.length){tick();const e=stack.pop();out.push(e);if(out.length>100000)throw new Error('Native DOM query node limit exceeded');const nested=children(e,light);for(let i=nested.length-1;i>=0;i--)stack.push(nested[i]);}
    return out;
  };
  const split=(text,commas)=>{
    const result=[];let start=0,depth=0,quote='',escape=false;
    for(let i=0;i<text.length;i++){
      const c=text[i];if(escape){escape=false;continue;}if(c==='\\'){escape=true;continue;}
      if(quote){if(c===quote)quote='';continue;}if(c==='"'||c==="'"){quote=c;continue;}
      if(c==='['||c==='(')depth++;if(c===']'||c===')')depth--;
      if(!depth&&commas&&c===','){result.push(text.slice(start,i).trim());start=i+1;}
    }
    result.push(text.slice(start).trim());return result;
  };
  const tokens=text=>{
    const out=[],brackets=[];let part='',depth=0,quote='',escape=false,pending='',comment=false;
    const emit=()=>{if(part.trim()){if(out.length)out.push(pending||' ');out.push(part.trim());part='';pending='';}};
    for(let index=0;index<text.length;index++){const c=text[index];
      if(comment){part+=c;if(c==='*'&&text[index+1]==='/'){part+='/';index++;comment=false;}continue;}
      if(escape){part+=c;escape=false;continue;}if(c==='\\'){part+=c;escape=true;continue;}
      if(quote){part+=c;if(c===quote)quote='';continue;}if(c==='"'||c==="'"){quote=c;part+=c;continue;}
      if(c==='/'&&text[index+1]==='*'){part+='/*';index++;comment=true;continue;}
      if(c==='!'&&!depth)throw new Error('Unexpected token \"!\" while parsing css selector '+JSON.stringify(text));
      if(c==='#'&&text[index+1]==='#')throw new Error('Unexpected token "#" while parsing css selector '+JSON.stringify(text)+'. Did you mean to CSS.escape it?');
      if(c==='['||c==='('){brackets.push(c);depth++;}
      if(c===']'||c===')'){if(brackets.pop()!==(c===']'?'[':'('))throw new Error('Unexpected token '+JSON.stringify(c)+' while parsing css selector '+JSON.stringify(text));depth--;}
      if(!depth&&nativeRegexTest(/\s/,c)){emit();if(!pending)pending=' ';continue;}
      if(!depth&&['>','+','~'].includes(c)){emit();pending=c;continue;}part+=c;
    }
    emit();if(out.length>257)throw new Error('Native CSS selector nesting limit exceeded');if(!out.length||pending.trim())throw new Error('Invalid native CSS selector');return out;
  };
  let cssDepth=0,simpleDepth=0;
  const cssCache=new Map();
  const layoutQueries=new WeakMap();
  const layoutRects=new WeakMap();
  const layoutRect=node=>{tick();if(!layoutRects.has(node))layoutRects.set(node,node.getBoundingClientRect());return layoutRects.get(node);};
  const layoutDistance=(kind,node,target,maximum)=>{
    if(node===target)return undefined;
    const a=layoutRect(node),b=layoutRect(target);let gap,offset;
    if(kind==='near'){
      const x=Math.max(b.left-a.right,a.left-b.right,0),y=Math.max(b.top-a.bottom,a.top-b.bottom,0),distance=x+y;
      return distance<=maximum?distance:undefined;
    }
    if(kind==='right-of'||kind==='left-of'){
      gap=kind==='right-of'?a.left-b.right:b.left-a.right;
      offset=Math.max(b.bottom-a.bottom,0)+Math.max(a.top-b.top,0);
    }else{
      gap=kind==='above'?b.top-a.bottom:a.top-b.bottom;
      offset=Math.max(b.right-a.right,0)+Math.max(a.left-b.left,0);
    }
    return gap>=0&&gap<=maximum?gap+offset:undefined;
  };
  const topScope=value=>{
    let depth=0,quote='',escaped=false;
    for(let index=0;index<value.length;index++){
      const c=value[index];if(escaped){escaped=false;continue;}if(c==='\\'){escaped=true;continue;}
      if(quote){if(c===quote)quote='';continue;}if(c==='"'||c==="'"){quote=c;continue;}
      if(c==='['||c==='(')depth++;if(c===']'||c===')')depth--;
      if(!depth&&value.startsWith(':scope',index)&&!nativeRegexTest(/[\w-]/,value[index+6]||''))return true;
    }
    return false;
  };


  const scopeEligibilityMemo=new Map();
  const scopeEligible=(value,depth=0)=>{
    if(depth>64)throw new Error('Native CSS selector nesting limit exceeded');
    if(scopeEligibilityMemo.has(value))return scopeEligibilityMemo.get(value);
    let result=topScope(value);
    if(!result){
      let nesting=0,quote='',escaped=false;
      for(let i=0;i<value.length&&!result;i++){
        tick();const c=value[i];if(escaped){escaped=false;continue;}if(c==='\\'){escaped=true;continue;}
        if(quote){if(c===quote)quote='';continue;}if(c==='"'||c==="'"){quote=c;continue;}
        if(c==='['||c==='('){nesting++;continue;}if(c===']'||c===')'){nesting--;continue;}
        if(nesting||c!==':')continue;const match=nativeRegexExec(/^:(?:is|where)\(/i,value.slice(i));if(!match)continue;
        const start=i+match[0].length;let end=start,level=1,innerQuote='',innerEscape=false;
        for(;end<value.length;end++){
          tick();const inner=value[end];if(innerEscape){innerEscape=false;continue;}if(inner==='\\'){innerEscape=true;continue;}
          if(innerQuote){if(inner===innerQuote)innerQuote='';continue;}if(inner==='"'||inner==="'"){innerQuote=inner;continue;}
          if(inner==='(')level++;if(inner===')'&&!--level)break;
        }
        if(level)throw new Error('Invalid native CSS pseudo selector');
        result=split(value.slice(start,end),true).some(part=>scopeEligible(tokens(part).at(-1),depth+1));i=end;
      }
    }
    scopeEligibilityMemo.set(value,result);return result;
  };

  const css=(root,selector,light=false)=>{
    if(++cssDepth>64){cssDepth--;throw new Error('Native CSS selector nesting limit exceeded');}
    try{
    if(selector.startsWith('id='))selector='#'+CSS.escape(selector.slice(3));
    if(selector.startsWith('css='))selector=selector.slice(4);
    const scope=validating?validationElement:nativeNodeType(root)===9?root.documentElement:root;
    // Preserve the exact selected scope without walking all of its descendants.
    if(selector.trim()===':scope')return validating?[]:scope!==null&&nativeNodeType(scope)===1?[scope]:[];
    const cssParent=e=>{if(light){tick();return e.parentElement;}return parent(e);};
    let relativeSibling=nativeRegexTest(/^[+~]/,selector.trim());
    if(nativeRegexTest(/^[>+~]/,selector.trim()))selector=':scope '+selector;
    const parsed=split(selector,true).map(tokens);
    // Ordinary atomic selectors need no authored pseudo/combinator machinery.
    // Keep composed enumeration order and every duplicate ID, including shadows.
    if(parsed.length===1&&parsed[0].length===1&&!selector.includes(':')){
      if(validating){validationElement.matches(selector);return [];}
      return enumerate(root,light).filter(e=>{tick();return e.matches(selector);});
    }
    relativeSibling=relativeSibling||parsed.some(parts=>scopeEligible(parts[0])&&['+','~'].includes(parts[1]));
    let rootLayoutQueries=layoutQueries.get(root);if(!rootLayoutQueries){rootLayoutQueries=new Map();layoutQueries.set(root,rootLayoutQueries);}
    const layoutScores=new Map();let usesLayout=false;
    const simple=(e,s)=>{
      if(++simpleDepth>64){simpleDepth--;throw new Error('Native CSS selector nesting limit exceeded');}
      try{
        let native='',position=0,score=0;const conditions=[];
        for(let i=0;i<s.length;i++){
          if(validating)tick();
          if(s[i]==='\\'){i++;continue;}
          if(s[i]==='/'&&s[i+1]==='*'){const end=s.indexOf('*/',i+2);i=end<0?s.length:end+1;continue;}
          if(s[i]==='['){let quote='',escaped=false;for(i++;i<s.length;i++){const c=s[i];if(escaped){escaped=false;continue;}if(c==='\\'){escaped=true;continue;}if(quote){if(c===quote)quote='';continue;}if(c==='"'||c==="'"){quote=c;continue;}if(c===']')break;}continue;}
          if(s[i]!==':')continue;const match=nativeRegexExec(/^:([a-z-]+)/i,s.slice(i));if(!match)continue;const key=match[1].toLowerCase(),begin=i;let end=i+match[0].length,arg='';
          if(s[end]==='('){let nesting=1,quote='',escaped=false;const start=++end;for(;end<s.length;end++){const c=s[end];if(escaped){escaped=false;continue;}if(c==='\\'){escaped=true;continue;}if(quote){if(c===quote)quote='';continue;}if(c==='"'||c==="'"){quote=c;continue;}if(c==='(')nesting++;if(c===')'&&!--nesting)break;}if(end===s.length)throw new Error('Invalid native CSS pseudo selector');arg=s.slice(start,end);end++;}
          if(!['scope','visible','has-text','text','text-is','text-matches','has','is','not','where','nth-match','right-of','left-of','above','below','near'].includes(key)){i=end-1;continue;}
          native+=s.slice(position,begin);position=end;i=end-1;
          if(key==='scope')conditions.push(()=>e===scope);
          else if(key==='visible')conditions.push(()=>visible(e));
          else if(key==='has-text'||key==='text'||key==='text-is'){
            let value;try{value=cssString(arg.trim());}catch{throw new Error(JSON.stringify(key)+' engine expects a single string');}const needle={text:value,exact:key==='text-is',legacy:key==='text-is',light};
            conditions.push(()=>key==='has-text'?textMatch(e,needle):smallestTextMatch(e,needle));
          }else if(key==='text-matches'){
            const args=split(arg,true);if(!args.length||args.length>2)throw new Error('Invalid native CSS text regular expression');const needle={pattern:cssString(args[0]),flags:args[1]?cssString(args[1]):''};if(validating)nativeRegexCreate(needle.pattern,needle.flags);conditions.push(()=>smallestTextMatch(e,needle));
          }else if(key==='has'){if(validating)css(root,arg,light);conditions.push(()=>css(e,arg,light).length>0);}
          else if(['right-of','left-of','above','below','near'].includes(key)){
            const args=split(arg,true);let maximum=key==='near'?50:Infinity;
            if(args.length>1&&nativeRegexTest(/^[+-]?(?:\d+(?:\.\d*)?|\.\d+)$/,args.at(-1)))maximum=Number(args.pop());
            if(!args.length||args.some(value=>!value)||args.length===1&&nativeRegexTest(/^[+-]?(?:\d+(?:\.\d*)?|\.\d+)$/,args[0])||maximum<0)throw new Error(JSON.stringify(key)+' engine expects a selector list and optional maximum distance in pixels');
            const query=args.join(',');if(validating)css(root,query,light);const cacheKey='layout:'+(light?'light:':'deep:')+query;usesLayout=true;
            conditions.push(()=>{if(!rootLayoutQueries.has(cacheKey))rootLayoutQueries.set(cacheKey,css(root,query,light));let best;for(const target of rootLayoutQueries.get(cacheKey)){const distance=layoutDistance(key,e,target,maximum);if(distance!==undefined&&(best===undefined||distance<best))best=distance;}if(best===undefined)return false;score+=best;return true;});
          }
          else if(key==='nth-match'){
            const args=split(arg,true);if(args.length<2||args.slice(0,-1).some(value=>!value))throw new Error('\"nth-match\" engine expects non-empty selector list and an index argument');const index=Number(args.pop());if(!Number.isInteger(index)||index<1)throw new Error('\"nth-match\" engine expects a one-based index as the last argument');const query=args.join(',');if(validating)css(root,query,light);conditions.push(()=>{const cacheKey=(light?'light:':'deep:')+query;if(!cssCache.has(cacheKey))cssCache.set(cacheKey,css(document,query,light));return cssCache.get(cacheKey)[index-1]===e;});
          }else{
            const alternatives=split(arg,true).map(tokens);if(validating)for(const alternative of alternatives)for(let index=0;index<alternative.length;index+=2)simple(validationElement,alternative[index]);conditions.push(()=>{const matched=alternatives.some(t=>matches(e,t,t.length-1));return key==='not'?!matched:matched;});
          }
        }
        native+=s.slice(position);if(validating){validationElement.matches(native.trim()||'*');return true;}const matched=e.matches(native.trim()||'*')&&conditions.every(condition=>condition());if(matched){let scores=layoutScores.get(e);if(!scores){scores=new Map();layoutScores.set(e,scores);}scores.set(s,score);}return matched;
      }finally{simpleDepth--;}
    };
    // Cache each actual element/selector-prefix result for this synchronous
    // query. Descendant and general-sibling backtracking otherwise revisits
    // the same prefix exponentially on ordinary deep branching documents.
    const matchMemo=new Map();
    const matches=(e,t,index)=>{
      if(!e)return false;
      let slots=matchMemo.get(t);if(!slots){slots=[];matchMemo.set(t,slots);}
      const cache=slots[index]||(slots[index]=new WeakMap());
      if(cache.has(e))return cache.get(e);
      tick();let result=false;
      if(simple(e,t[index])){
        if(!index)result=e!==scope||nativeNodeType(root)===9||scopeEligible(t[index]);
        else{
          const relation=t[index-1];
          if(e===scope&&(relation==='>'||relation===' '))result=false;
          else if(relation==='>')result=matches(cssParent(e),t,index-2);
          else if(relation==='+')result=matches(e.previousElementSibling,t,index-2);
          else if(relation==='~'){for(let p=e.previousElementSibling;p;p=p.previousElementSibling)if(matches(p,t,index-2)){result=true;break;}}
          else for(let p=cssParent(e);p;p=cssParent(p)){if(matches(p,t,index-2)){result=true;break;}if(p===scope)break;}
        }
      }
      cache.set(e,result);return result;
    };
    if(validating){for(const parts of parsed)for(let index=0;index<parts.length;index+=2)simple(validationElement,parts[index]);return [];}
    const all=enumerate(relativeSibling?(root.parentNode||root):root,light);if(parsed.some(parts=>scopeEligible(parts.at(-1)))&&scope!==null&&nativeNodeType(scope)===1&&!all.includes(scope))all.unshift(scope);
    const result=all.filter(e=>parsed.some(t=>matches(e,t,t.length-1)));
    if(usesLayout){const score=e=>Math.min(...parsed.filter(t=>matches(e,t,t.length-1)).map(t=>layoutScores.get(e)?.get(t.at(-1))||0));result.sort((a,b)=>score(a)-score(b));}
    return result;
    }finally{cssDepth--;}
  };
  const normalize=text=>nativeRegexReplace(nativeRegexReplace(String(text),/[\u200b\u00ad]/g,''),/\s+/g,' ').trim();
  // Selector text includes both light and open shadow descendants.
  const selectorSkipMemo=new Map();
  const selectorTextSkipped=node=>{
    const path=[];let cursor=node,skip=false;
    while(cursor){
      tick();if(selectorSkipMemo.has(cursor)){skip=selectorSkipMemo.get(cursor);break;}
      path.push(cursor);if(['script','style','head'].includes(cursor.localName)){skip=true;break;}
      cursor=cursor.parentNode||cursor.host||null;
    }
    for(const entry of path)selectorSkipMemo.set(entry,skip);return skip;
  };
  const selectorTextMemo=[new Map(),new Map()];
  const selectorText=(node,light=false)=>{
    const memo=selectorTextMemo[light?1:0];
    if(memo.has(node))return memo.get(node);
    const stack=[[node,false]];let visits=0;
    const save=(entry,full,chunks=[],skip=false)=>{textBytes+=(full.length+chunks.reduce((sum,value)=>sum+value.length,0))*2;if(textBytes>32*1024*1024)throw new Error('Native DOM text byte limit exceeded');memo.set(entry,{full,chunks,skip});};
    while(stack.length){
      tick();const [entry,finished]=stack.pop();if(memo.has(entry))continue;
      if(++visits>300000)throw new Error('Native DOM text node limit exceeded');
      if(nativeNodeType(entry)===3){save(entry,entry.nodeValue||'');continue;}
      if(nativeNodeType(entry)!==1&&nativeNodeType(entry)!==11){save(entry,'',[],true);continue;}
      if(nativeNodeType(entry)===1&&selectorTextSkipped(entry)){save(entry,'',[],true);continue;}
      if(entry.localName==='input'&&['button','submit','reset'].includes(entry.type)){save(entry,entry.value,[entry.value]);continue;}
      const nodes=[...entry.childNodes,...(!light&&entry.shadowRoot?[entry.shadowRoot]:[])];
      if(!finished){stack.push([entry,true]);for(let index=nodes.length-1;index>=0;index--)stack.push([nodes[index],false]);continue;}
      let full='',chunk='';const chunks=[];
      for(const child of nodes){const value=memo.get(child)?.full||'';full+=value;if(nativeNodeType(child)===3)chunk+=value;else if(nativeNodeType(child)!==8){if(chunk)chunks.push(chunk);chunk='';}}
      if(chunk)chunks.push(chunk);save(entry,full,chunks);
    }
    return memo.get(node);
  };
  const textMatch=(node,step)=>{
    const value=selectorText(node,!!step.light);if(value.skip)return false;
    if(step.legacy&&step.exact)return normalize(step.text)===''?normalize(value.full)==='':value.chunks.some(chunk=>matchText(chunk,step));
    return matchText(value.full,step);
  };
  const smallestTextMatch=(node,step)=>textMatch(node,step)&&!children(node,!!step.light).some(child=>textMatch(child,step));

  const matchText=(value,step)=>{
    if(step.pattern!==undefined)return nativeRegexTest(nativeRegexCreate(step.pattern,step.flags||''),value);
    const left=normalize(value),right=normalize(step.text);if(step.operator){const a=step.caseSensitive?left:left.toLowerCase(),b=step.caseSensitive?right:right.toLowerCase();return step.operator==='='?a===b:step.operator==='*='?a.includes(b):step.operator==='^='?a.startsWith(b):step.operator==='$='?a.endsWith(b):step.operator==='~='?nativeRegexSplit(a,/\s+/).includes(b):step.operator==='|='?(a===b||a.startsWith(b+'-')):false;}return step.exact?left===right:left.toLowerCase().includes(right.toLowerCase());
  };
  const validRoles=new Set('alert alertdialog application article banner blockquote button caption cell checkbox code columnheader combobox complementary contentinfo definition deletion dialog directory document emphasis feed figure form generic grid gridcell group heading img insertion link list listbox listitem log main marquee math menu menubar menuitem menuitemcheckbox menuitemradio meter navigation none note option paragraph presentation progressbar radio radiogroup region row rowgroup rowheader scrollbar search searchbox separator slider spinbutton status strong subscript superscript switch tab table tablist tabpanel term textbox time timer toolbar tooltip tree treegrid treeitem'.split(' '));
  const implicitRoles={button:'button',textarea:'textbox',option:'option',optgroup:'group',table:'table',tr:'row',td:'cell',thead:'rowgroup',tbody:'rowgroup',tfoot:'rowgroup',ul:'list',ol:'list',menu:'list',li:'listitem',h1:'heading',h2:'heading',h3:'heading',h4:'heading',h5:'heading',h6:'heading',progress:'progressbar',meter:'meter',dialog:'dialog',details:'group',nav:'navigation',main:'main',article:'article',aside:'complementary',fieldset:'group',figure:'figure',hr:'separator',output:'status',p:'paragraph',blockquote:'blockquote',caption:'caption',code:'code',dd:'definition',dt:'term',del:'deletion',ins:'insertion',em:'emphasis',strong:'strong',sub:'subscript',sup:'superscript',time:'time',html:'document',svg:'img',address:'group'};
  const inputRoles={button:'button',image:'button',submit:'button',reset:'button',checkbox:'checkbox',radio:'radio',range:'slider',number:'spinbutton'};
  const named=node=>!!normalize(node.getAttribute('aria-label')||node.getAttribute('aria-labelledby')||node.getAttribute('title')||'');
  const role=node=>{
    const explicit=nativeRegexSplit((node.getAttribute('role')||'').trim(),/\s+/).find(value=>validRoles.has(value));
    if(explicit&&(!['none','presentation'].includes(explicit)||(node.tabIndex<0&&!node.isContentEditable&&!node.hasAttribute('aria-label')&&!node.hasAttribute('aria-labelledby'))))return explicit;
    const tag=node.localName;
    if(tag==='input'){if(['search','email','tel','url','text'].includes(node.type))return node.hasAttribute('list')?'combobox':node.type==='search'?'searchbox':'textbox';return Object.hasOwn(inputRoles,node.type)?inputRoles[node.type]:'';}
    if(tag==='th')return node.getAttribute('scope')==='row'||node.getAttribute('scope')==='rowgroup'?'rowheader':'columnheader';
    if(tag==='header'||tag==='footer'){for(let p=parent(node);p;p=parent(p))if(['article','aside','main','nav','section'].includes(p.localName))return '';return tag==='header'?'banner':'contentinfo';}
    if(tag==='section')return named(node)?'region':'';
    if(tag==='form')return named(node)?'form':'';
    if(tag==='summary')return '';
    if(tag==='a'||tag==='area')return node.hasAttribute('href')?'link':'';
    if(tag==='select')return node.multiple||node.size>1?'listbox':'combobox';
    if(tag==='img')return node.getAttribute('alt')===''&&!named(node)?'presentation':'img';
    return Object.hasOwn(implicitRoles,tag)?implicitRoles[tag]:'';
  };
  // Custom selector callbacks may synchronously change styles between steps.
  // Otherwise these fields are reusable only during this atomic observation.
  const hiddenStyles=options.accessibilityTools||options.engines&&Object.keys(options.engines).length?null:new Map();
  const hiddenStyle=node=>{if(!hiddenStyles)return getComputedStyle(node);let style=hiddenStyles.get(node);if(!style){const actual=getComputedStyle(node);style={display:actual.display,visibility:actual.visibility};hiddenStyles.set(node,style);}return style;};
  const hidden=node=>{
    if(node.localName!=='option'){const ownStyle=hiddenStyle(node);if(ownStyle.visibility==='hidden'||ownStyle.visibility==='collapse')return true;}
    for(let e=node;e;e=e.assignedSlot||parent(e)){
      const p=parent(e);if(p?.shadowRoot&&!e.assignedSlot&&e.getRootNode()!==p.shadowRoot)return true;
      if(e.getAttribute('aria-hidden')==='true'||hiddenStyle(e).display==='none')return true;
      if(p?.localName==='details'&&!p.open&&e!==p.querySelector('summary'))return true;
    }
    return false;
  };
  const visible=(node,geometry=false)=>{
    if(!(node instanceof Element)||!node.isConnected)return geometry?null:false;
    const stack=[node],seen=new Set(),boxes=[];let nodes=0;
    const measure=box=>{if(box.width<=0||box.height<=0)return false;if(!geometry)return true;boxes.push([box.x,box.y,box.right,box.bottom]);return false;};
    const flatParent=entry=>entry.assignedSlot||entry.parentElement||entry.getRootNode().host||null;
    const check=entry=>typeof Element.prototype.checkVisibility!=='function'||Element.prototype.checkVisibility.call(entry,{visibilityProperty:true});
    while(stack.length){
      tick();const entry=stack.pop();if(seen.has(entry))continue;seen.add(entry);if(++nodes>100000)throw new Error('Native visibility node limit exceeded');
      if(nativeNodeType(entry)===3){
        let ancestor=flatParent(entry),style=null,depth=0;
        while(ancestor){tick();if(++depth>256)throw new Error('Native visibility ancestor limit exceeded');style=getComputedStyle(ancestor);if(style.visibility==='hidden'||style.visibility==='collapse')break;if(style.display!=='contents')break;ancestor=flatParent(ancestor);}
        if(!ancestor||style.visibility==='hidden'||style.visibility==='collapse'||style.contentVisibility==='hidden'||!check(ancestor))continue;
        const range=nativeOwnerDocument(entry).createRange();range.selectNodeContents(entry);const box=range.getBoundingClientRect();range.detach();if(measure(box))return true;continue;
      }
      if(!(entry instanceof Element))continue;const style=getComputedStyle(entry);if(style.display==='none')continue;
      if(style.display==='contents'){
        const assigned=entry instanceof HTMLSlotElement?entry.assignedNodes({flatten:true}):null;
        const nested=assigned&&assigned.length?assigned:entry.shadowRoot?entry.shadowRoot.childNodes:entry.childNodes;
        for(const child of nested){tick();if(stack.length>=100000)throw new Error('Native visibility pending node limit exceeded');stack.push(child);}continue;
      }
      if(style.visibility==='hidden'||style.visibility==='collapse'||!check(entry))continue;const box=entry.getBoundingClientRect();if(measure(box))return true;
    }
    if(!geometry)return false;if(!boxes.length)return null;
    let left=Infinity,top=Infinity,right=-Infinity,bottom=-Infinity;for(const box of boxes){left=Math.min(left,box[0]);top=Math.min(top,box[1]);right=Math.max(right,box[2]);bottom=Math.max(bottom,box[3]);}return [left,top,right-left,bottom-top];
  };
  const byId=(node,id)=>{const root=node.getRootNode();return root.getElementById?root.getElementById(id):root.querySelector('#'+CSS.escape(id));};
  const contentRoles=new Set('button cell checkbox columnheader gridcell heading link menuitem menuitemcheckbox menuitemradio option radio row rowheader switch tab tooltip treeitem'.split(' '));
  // CSS generated-content alternative text follows an unquoted slash. Read
  // genuine computed strings, decoding CSS escapes without treating syntax as
  // visible text. Non-string image/counter expressions need explicit alternate
  // text rather than invented text from CSS tokens.
  const pseudoContent=value=>{
    if(value.length>65536)throw new Error('Native pseudo-content text limit exceeded');
    let quote='',depth=0,start=0;
    for(let i=0;i<value.length;i++){const c=value[i];if(quote){if(c==='\\')i++;else if(c===quote)quote='';continue;}if(c==='"'||c==="'")quote=c;else if(c==='(')depth++;else if(c===')')depth=Math.max(0,depth-1);else if(c==='/'&&!depth){start=i+1;break;}}
    let result='',i=start;const whitespace=c=>c===' '||c==='\t'||c==='\r'||c==='\n'||c==='\f';
    while(i<value.length){while(whitespace(value[i]))i++;if(i===value.length)break;const q=value[i++];if(q!=='"'&&q!=="'")return '';let closed=false;
      while(i<value.length){let c=value[i++];if(c===q){closed=true;break;}if(c!=='\\'){result+=c;continue;}if(i===value.length)return '';c=value[i++];if(c==='\r'||c==='\n'||c==='\f'){if(c==='\r'&&value[i]==='\n')i++;continue;}
        if(nativeRegexTest(/[0-9a-fA-F]/,c)){let hex=c;while(hex.length<6&&nativeRegexTest(/[0-9a-fA-F]/,value[i]||''))hex+=value[i++];if(whitespace(value[i])){if(value[i++]==='\r'&&value[i]==='\n')i++;}let code=parseInt(hex,16);if(!code||code>1114111||(code>=55296&&code<=57343))code=65533;result+=String.fromCodePoint(code);}else result+=c;
      }if(!closed)return '';
    }return result;
  };
  const pseudo=(node,kind)=>{const style=getComputedStyle(node,kind),value=style.content;if(style.display==='none'||style.visibility==='hidden'||style.visibility==='collapse'||!value||value==='none'||value==='normal')return '';const content=pseudoContent(value);return style.display!=='contents'&&!style.display.startsWith('inline')?' '+content+' ':content;};
  // Record a title consumed by this exact root name computation. Equal strings
  // from separate label/content sources still permit a real title description.
  const nameTitle=(node,titleSource)=>{const value=node.getAttribute('title')||'';if(value&&titleSource)titleSource.used=true;return value;};
  const computeName=(node,context={referenced:false,embedded:false},seen=new Set(),depth=0,titleSource)=>{
    tick();if(depth>512)throw new Error('Native accessible-name depth limit exceeded');
    if(nativeNodeType(node)===3)return node.nodeValue||'';
    if(nativeNodeType(node)!==1||seen.has(node))return '';
    if(!context.referenced&&hidden(node))return '';
    seen.add(node);
    try{
      const ids=nativeRegexSplit((node.getAttribute('aria-labelledby')||'').trim(),/\s+/).filter(Boolean);
      const references=ids.map(id=>byId(node,id)).filter(Boolean);
      if(references.length&&!context.labelledby)return normalize(references.map(target=>name(target,{referenced:true,labelledby:true},seen,depth+1)).join(' '));
      const aria=node.getAttribute('aria-label');if(aria&&normalize(aria))return normalize(aria);
      const tag=node.localName,r=role(node);
      if(context.embedded&&['textbox','searchbox','combobox','listbox','slider','spinbutton','scrollbar'].includes(r)){
        if(r==='textbox'||r==='searchbox')return node.value||node.textContent||'';
        if(r==='combobox'||r==='listbox'){const selected=tag==='select'?[...node.selectedOptions]:enumerate(node).filter(e=>role(e)==='option'&&e.getAttribute('aria-selected')==='true');return selected.map(e=>name(e,{referenced:true},seen,depth+1)).join(' ');}
        return node.getAttribute('aria-valuetext')||node.getAttribute('aria-valuenow')||node.value||'';
      }
      if(node.labels?.length)return normalize([...node.labels].map(label=>name(label,{referenced:true},seen,depth+1)).join(' '));
      if(['img','area'].includes(tag))return normalize(node.getAttribute('alt')||nameTitle(node,titleSource)||'');
      if(tag==='input'&&node.type==='file')return normalize(options.nativeControls?.get(node)?.[0]?.name||nameTitle(node,titleSource)||'');
      if(tag==='input'&&node.type==='image')return normalize(node.getAttribute('alt')||nameTitle(node,titleSource)||'Submit');
      if(tag==='input'&&['button','submit','reset'].includes(node.type))return normalize(node.value||({submit:'Submit',reset:'Reset'})[node.type]||'');
      const special=tag==='fieldset'?'legend':tag==='figure'?'figcaption':tag==='table'?'caption':tag==='svg'?'title':null;
      if(special){const child=[...node.children].find(e=>e.localName===special);if(child)return normalize(name(child,{referenced:true},seen,depth+1));}
      if(tag==='iframe')return normalize(nameTitle(node,titleSource)||'');
      if(context.referenced||context.embedded||contentRoles.has(r)){
        let nodes=node.localName==='slot'&&node.assignedNodes().length?node.assignedNodes({flatten:true}):node.shadowRoot?[...node.shadowRoot.childNodes]:[...node.childNodes];
        const owned=nativeRegexSplit((node.getAttribute('aria-owns')||'').trim(),/\s+/).filter(Boolean).map(id=>byId(node,id)).filter(e=>e&&!nodes.includes(e));nodes=[...nodes,...owned];
        let result=pseudo(node,'::before');for(const child of nodes){const value=name(child,{referenced:context.referenced,embedded:true},seen,depth+1);const display=nativeNodeType(child)===1?getComputedStyle(child).display:'';const block=owned.includes(child)||(!!display&&display!=='contents'&&!display.startsWith('inline'));result+=(block?' ':'')+value+(block?' ':'');}result+=pseudo(node,'::after');
        if(normalize(result))return context.embedded?result:normalize(result);
      }
      return normalize(nameTitle(node,titleSource)||(['textbox','searchbox'].includes(r)?node.getAttribute('placeholder'):'')||'');
    }finally{seen.delete(node);}
  };
  const name=(...args)=>{const value=computeName(...args);nameBytes+=value.length*2;if(nameBytes>32*1024*1024)throw new Error('Native accessible-name byte limit exceeded');return value;};
  const disabled=node=>{
    if(node.matches(':disabled'))return true;
    for(let e=node;e;e=parent(e)){const value=e.getAttribute('aria-disabled');if(value==='true')return true;if(value==='false')return false;}return false;
  };
  const state=(node,key)=>{
    if(key==='disabled')return disabled(node);
    if(key==='level'){const value=node.getAttribute('aria-level');return value!==null?Number(value):nativeRegexTest(/^h[1-6]$/,node.localName)?Number(node.localName[1]):undefined;}
    if(key==='checked'&&node.localName==='input'&&['checkbox','radio'].includes(node.type))return node.indeterminate?'mixed':node.checked;
    if(key==='selected'&&node.localName==='option')return node.selected;
    if(key==='expanded'&&node.localName==='summary'&&node.parentElement?.localName==='details')return node.parentElement.open;
    const value=node.getAttribute('aria-'+key);return value==='true'?true:value==='false'?false:value==='mixed'?'mixed':['checked','pressed','selected'].includes(key)?false:undefined;
  };
  const description=node=>{const ids=nativeRegexSplit((node.getAttribute('aria-describedby')||'').trim(),/\s+/).filter(Boolean),references=ids.map(id=>byId(node,id)).filter(Boolean);if(references.length)return normalize(references.map(e=>name(e,{referenced:true})).join(' '));const explicit=node.getAttribute('aria-description');if(explicit&&normalize(explicit))return normalize(explicit);const title=normalize(node.getAttribute('title')||'');if(!title)return '';const titleSource={used:false};name(node,{},undefined,0,titleSource);return titleSource.used?'':title;};
  const roleMatches=(node,step)=>{
    const actual=role(node);if(actual!==step.role||(!step.includeHidden&&hidden(node)))return false;
    if(step.name&&!matchText(name(node,{referenced:!!step.includeHidden}),step.name))return false;
    if(step.description&&!matchText(description(node),step.description))return false;
    for(const key of ['checked','disabled','expanded','level','pressed','selected'])if(step[key]!==undefined&&state(node,key)!==step[key])return false;
    return true;
  };
  const quoted=value=>{
    if(value.startsWith('"'))return nativeRegexReplace(value.slice(1,-1),/\\([\\"])/g,'$1');
    if(value.startsWith("'"))return nativeRegexReplace(value.slice(1,-1),/\\([\\'])/g,'$1');
    return value;
  };
  const cssString=value=>{
    if(value.length<2||!['"',"'"].includes(value[0])||value.at(-1)!==value[0])throw new Error('CSS text argument must be a string');
    let out='';const quote=value[0];
    for(let index=1;index<value.length-1;index++){
      const c=value[index];if(c===quote||c==='\n'||c==='\r'||c==='\f')throw new Error('CSS text argument must be a string');
      if(c!=='\\'){out+=c;continue;}
      const next=value[++index];if(next===undefined)throw new Error('Invalid CSS string escape');
      if(next==='\n'||next==='\f')continue;if(next==='\r'){if(value[index+1]==='\n')index++;continue;}
      if(nativeRegexTest(/[0-9a-f]/i,next)){
        let hex=next;while(hex.length<6&&nativeRegexTest(/[0-9a-f]/i,value[index+1]||''))hex+=value[++index];
        const code=Number.parseInt(hex,16);out+=!code||code>0x10ffff||(code>=0xd800&&code<=0xdfff)?'\ufffd':String.fromCodePoint(code);
        if(nativeRegexTest(/[\t\n\r\f ]/,value[index+1]||'')){index++;if(value[index]==='\r'&&value[index+1]==='\n')index++;}
      }else out+=next;
    }
    return out;
  };
  const legacyQuoted=value=>nativeRegexReplace(value.slice(1,-1),/\\([\s\S])/g,'$1');
  const pattern=value=>{
    if(value.startsWith('/')){let end=value.length-1;while(end>0&&nativeRegexTest(/[a-z]/i,value[end]))end--;if(value[end]!=='/')throw new Error('Invalid native text regular expression');return {pattern:value.slice(1,end),flags:value.slice(end+1)};}
    const exact=value.length>1&&(value[0]==='"'||value[0]==="'")&&value.at(-1)===value[0];return {text:exact?legacyQuoted(value):value,exact};
  };
  const selectorParts=value=>{
    const parts=[];let start=0,quote='',escaped=false,depth=0,regex=false,characterClass=false;
    const bodyStart=()=>{const match=nativeRegexExec(/^\s*\*?text(?::light)?\s*=/,value.slice(start));return match?start+match[0].length:-1;};
    let textStart=bodyStart();
    for(let i=0;i<value.length;i++){
      const c=value[i];if(escaped){escaped=false;continue;}if(c==='\\'){escaped=true;continue;}
      if(quote){if(c===quote)quote='';continue;}
      if(regex){if(c==='[')characterClass=true;if(c===']')characterClass=false;if(c==='/'&&!characterClass)regex=false;continue;}
      if(((c==='"'||c==="'")&&(textStart<0||i===textStart))||(c==='`'&&i===textStart)){quote=c;continue;}
      if(c==='/'&&((textStart>=0&&i===textStart)||(textStart<0&&value.slice(start,i).trim().endsWith('=')))){regex=true;continue;}
      if(textStart<0){if(c==='['||c==='(')depth++;if(c===']'||c===')')depth--;}
      if(!depth&&c==='>'&&value[i+1]==='>'){parts.push(value.slice(start,i).trim());start=i+2;i++;textStart=bodyStart();}
    }
    parts.push(value.slice(start).trim());if(parts.some(part=>!part))throw new Error('Invalid native selector chain');return parts;
  };
  const parseRole=value=>{
    const index=value.indexOf('['),r=(index<0?value:value.slice(0,index)).trim();if(!r)throw new Error('Role must not be empty');
    const step={engine:'role',role:r};let rest=index<0?'':value.slice(index);
    const supports={checked:['checkbox','menuitemcheckbox','menuitemradio','radio','switch'],pressed:['button'],selected:['gridcell','option','row','tab','rowheader','columnheader','treeitem'],expanded:['application','button','checkbox','combobox','gridcell','link','listbox','menuitem','row','rowheader','tab','treeitem'],level:['heading','listitem','row','treeitem']};
    while(rest){
      if(rest[0]!=='[')throw new Error('Invalid native role selector');let end=1,quote='',escaped=false,regex=false;
      for(;end<rest.length;end++){const c=rest[end];if(escaped){escaped=false;continue;}if(c==='\\'){escaped=true;continue;}if(quote){if(c===quote)quote='';continue;}if(regex){if(c==='/')regex=false;continue;}if(c==='"'||c==="'"){quote=c;continue;}if(c==='/'&&rest.slice(1,end).trim().endsWith('=')){regex=true;continue;}if(c===']')break;}
      if(end===rest.length)throw new Error('Invalid native role selector');const attribute=rest.slice(1,end).trim();rest=rest.slice(end+1).trim();
      const match=nativeRegexExec(/^([\w.\s-]+?)(?:([*^$~|]?=)([\s\S]*))?$/,attribute);if(!match)throw new Error('Invalid native role attribute');
      const key=nativeRegexReplace(match[1],/\s/g,''),op=match[2],raw=match[3]?.trim();
      if(!['checked','description','disabled','expanded','include-hidden','level','name','pressed','selected'].includes(key))throw new Error('Unknown attribute "'+key+'", must be one of "checked", "description", "disabled", "expanded", "include-hidden", "level", "name", "pressed", "selected"');
      if(supports[key]&&!supports[key].includes(r))throw new Error('"'+key+'" attribute is only supported for roles: '+supports[key].map(role=>'"'+role+'"').join(', '));
      if(key==='name'||key==='description'){
        if(!op||raw===undefined)throw new Error('"'+key+'" attribute must have a value');
        const suffix=nativeRegexExec(/\s+([is])$/,raw),value=suffix?raw.slice(0,suffix.index):raw;
        step[key]=value.startsWith('/')?pattern(value):{text:quoted(value),operator:op,caseSensitive:suffix?.[1]!=='i'};continue;
      }
      let value=op?quoted(raw):true;if(raw==='true')value=true;if(raw==='false')value=false;
      if(key==='level'){if(!op||!nativeRegexTest(/^[0-9]+$/,raw))throw new Error('"level" attribute must be compared to a number');value=Number(raw);}
      else if(![true,false].includes(value)&&(!['checked','pressed'].includes(key)||value!=='mixed'))throw new Error('"'+key+'" must be one of true, false'+(['checked','pressed'].includes(key)?', "mixed"':''));
      if(op&&op!=='=')throw new Error('cannot use '+op+' in attribute with non-string matching value');step[key==='include-hidden'?'includeHidden':key]=value;
    }
    return step;
  };
  let customEngines;
  if(!validating&&typeof options.registryKey==='string'){
    if(!options.registryKey.length||options.registryKey.length>192)throw new Error('Native selector context registry key is invalid');
    customEngines=Reflect.get(globalThis,options.registryKey);
    if(customEngines===undefined){customEngines=new Map();Object.defineProperty(globalThis,options.registryKey,{value:customEngines});}
    if(!(customEngines instanceof Map))throw new Error('Native selector context registry is invalid');
  }else customEngines=new Map();
  const captured=part=>nativeRegexTest(/^\*[A-Za-z0-9_:-]+\s*=/,part);
  let parseDepth=0;
  const parseSelector=selector=>{
    if(++parseDepth>64){parseDepth--;throw new Error('Native selector parsing nesting limit exceeded');}
    try{
    const parts=selectorParts(selector).map(part=>nativeRegexReplace(part,/^(\*?[A-Za-z0-9_:-]+)\s*=/,'$1=')),capture=parts.findIndex(captured);
    if(capture>=0){
      if(parts.filter(captured).length>1)throw new Error('Only one selector capture is allowed');
      if(parts.some(part=>part.startsWith('nth=')))throw new Error("Can't query n-th element in a captured selector");
      const prefix=parts.slice(0,capture+1);prefix[capture]=prefix[capture].slice(1);
      return [{engine:'capture',query:parseSelector(prefix.join(' >> ')),descendants:parts.slice(capture+1).length?parseSelector(parts.slice(capture+1).join(' >> ')):[]}];
    }
    return parts.map((part,index)=>{
    const nested=nativeRegexExec(/^internal:(has-not|has|and|or|chain)=([\s\S]*)$/,part);
    if(nested){
      let value;try{value=JSON.parse(nested[2]);}catch{throw new Error('Malformed selector: '+part);}
      if(typeof value!=='string')throw new Error('Malformed selector: '+part);
      const kind=nested[1];
      if((kind==='has'||kind==='has-not')&&!index)throw new Error(JSON.stringify('internal:'+kind)+' selector cannot be first');
      const query=parseSelector(value);return kind==='has'||kind==='has-not'?{engine:'filter',[kind==='has'?'has':'hasNot']:query}:{engine:kind,query};
    }
    if(part.startsWith('visible=')){const value=part.slice(8);if(value!=='true'&&value!=='false')throw new Error('Invalid native visibility selector');return {engine:'filter',visible:value==='true'};}
    if(part.startsWith('role='))return parseRole(part.slice(5));
    if(part===String.fromCharCode(34)||part===String.fromCharCode(39))throw new Error('Unterminated quoted text selector');
    if(part.startsWith('css:light='))return {engine:'css-light',selector:part.slice(10)};
    const lightAttribute=nativeRegexExec(/^(id|data-testid|data-test-id|data-test):light=([\s\S]*)$/,part);if(lightAttribute)return {engine:'testId',attribute:lightAttribute[1],text:lightAttribute[2],light:true};
    if((part.startsWith('"')&&part.endsWith('"'))||(part.startsWith("'")&&part.endsWith("'")))return {engine:'text',...pattern(part),legacy:true};
    if(part.startsWith('text='))return {engine:'text',...pattern(part.slice(5)),legacy:true};
    if(part.startsWith('text:light='))return {engine:'text',...pattern(part.slice(11)),legacy:true,light:true};
    if(part.startsWith('nth=')){const index=Number(part.slice(4));if(!Number.isInteger(index))throw new Error('Invalid native locator index');return {engine:'nth',index};}
    if(part.startsWith('xpath:light='))return {engine:'xpath',selector:part.slice(12)};
    if(part.startsWith('xpath=')||part.startsWith('//')||part.startsWith('..')||nativeRegexTest(/^(?:\(\s*)+\/\//,part))return {engine:'xpath',selector:part.startsWith('xpath=')?part.slice(6):part};
    const custom=nativeRegexExec(/^([A-Za-z0-9_-]+)=([\s\S]*)$/,part);if(custom&&options.engines&&Object.hasOwn(options.engines,custom[1]))return {engine:'custom',name:custom[1],selector:custom[2]};
    if(custom&&['data-testid','data-test-id','data-test'].includes(custom[1]))return {engine:'testId',attribute:custom[1],text:custom[2]};
    if(custom&&!['css','id'].includes(custom[1]))throw new Error('Unknown engine '+JSON.stringify(custom[1])+' while parsing selector '+selector);
    return {engine:'css',selector:part};
    });
    }finally{parseDepth--;}
  };
  const normalizePlan=(query,depth=0)=>{
    if(depth>64||!Array.isArray(query)||query.length>64)throw new Error('Native locator query nesting limit exceeded');
    if(query.some(step=>step.engine==='nth')&&query.some(step=>step.engine==='css'&&selectorParts(step.selector).some(captured)))throw new Error("Can't query n-th element in a captured selector");
    const out=[];
    for(const original of query){
      if(!original||typeof original.engine!=='string'||original.engine==='selector-wait')throw new Error('Invalid native locator query step');
      let stage=[original];
      if(original.engine==='css'){
        if(typeof original.selector!=='string'||!original.selector.trim())throw new Error('Invalid native CSS selector');
        const parsed=parseSelector(original.selector);
        if(parsed.length!==1||parsed[0].engine!=='css')stage=parsed;
      }
      for(const raw of stage){
        const step={...raw};
        if(step.engine==='reference'&&(typeof step.ref!=='string'||step.ref.length>192||!nativeRegexTest(/^r[A-Za-z0-9]{1,64}_[1-9][0-9]*_(?:0|[1-9][0-9]*)$/,step.ref)))throw new Error('Invalid native snapshot reference token');
        if(['and','or','chain','capture'].includes(step.engine))step.query=normalizePlan(step.query,depth+1);
        if(step.engine==='capture')step.descendants=normalizePlan(step.descendants,depth+1);
        if(step.engine==='filter'){
          if(step.has!==undefined)step.has=normalizePlan(step.has,depth+1);
          if(step.hasNot!==undefined)step.hasNot=normalizePlan(step.hasNot,depth+1);
        }
        out.push(step);if(out.length>64)throw new Error('Native locator query nesting limit exceeded');
      }
    }
    return out;
  };
  if(validating){
    const checkPattern=value=>{if(value&&typeof value==='object'&&value.pattern!==undefined)nativeRegexCreate(value.pattern,value.flags||'');};
    const validate=(query,depth=0)=>{
      if(depth>64)throw new Error('Native locator query nesting limit exceeded');
      for(const step of query){
        tick();
        if(['css','css-light'].includes(step.engine)){
          if(typeof step.selector!=='string'||!step.selector.trim())throw new Error('Invalid native CSS selector');
          css(document,step.selector,step.engine==='css-light');
        }else if(step.engine==='xpath'){
          if(typeof step.selector!=='string')throw new Error('Invalid native XPath selector');document.createExpression(step.selector,null);
        }else if(step.engine==='custom'){
          const registration=options.engines?.[step.name];
          if(!options.engines||!Object.hasOwn(options.engines,step.name)||!registration||typeof registration.source!=='string')throw new Error('Unknown native selector engine: '+step.name);
          if(typeof step.selector!=='string')throw new Error('Invalid native custom selector');
        }else if(['text','role','label','placeholder','alt','title','testId'].includes(step.engine)){
          checkPattern(step);checkPattern(step.name);checkPattern(step.description);
        }else if(step.engine==='filter'){
          checkPattern(step.hasText);checkPattern(step.hasNotText);
          if(step.has!==undefined)validate(step.has,depth+1);if(step.hasNot!==undefined)validate(step.hasNot,depth+1);
        }else if(['and','or','chain','capture'].includes(step.engine)){
          validate(step.query,depth+1);if(step.engine==='capture')validate(step.descendants,depth+1);
        }else if(step.engine==='reference'){
          // A finite opaque-token grammar check never calls a DOM query or selector factory.
        }else if(step.engine==='nth'){
          if(!Number.isInteger(step.index))throw new Error('Invalid native locator index');
        }else if(step.engine==='invalid')throw new Error(step.message);
        else throw new Error('Unsupported native locator query engine');
      }
    };
    validate(normalizePlan(steps));return true;
  }
  if(options.planOnly)return wire(normalizePlan(steps),true);
  const run=(steps,start,depth=0)=>{
  if(depth>64||!Array.isArray(steps)||steps.length>64)throw new Error('Native locator query nesting limit exceeded');
  if(steps.some(step=>step.engine==='nth')&&steps.some(step=>step.engine==='css'&&selectorParts(step.selector).some(captured)))throw new Error("Can't query n-th element in a captured selector");
  let roots=[start];
  for(const step of steps){
    if(step.engine==='css'||step.engine==='css-light'){
      if(step.engine==='css-light'){const found=new Set();for(const root of roots)for(const node of css(root,step.selector,true))found.add(node);roots=[...found];continue;}
      if(typeof step.selector!=='string'||!step.selector.trim())throw new Error('Invalid native CSS selector');
      const parsed=parseSelector(step.selector);
      if(parsed.length!==1||parsed[0].engine!=='css'){const found=new Set();for(const root of roots)for(const e of run(parsed,root,depth+1))found.add(e);roots=[...found];continue;}
      const found=new Set();for(const root of roots)for(const e of css(root,step.selector))found.add(e);roots=[...found];
    }else if(step.engine==='custom'){
      if(hiddenStyles)hiddenStyles.clear();
      if(!customEngines.has(step.name)){const registration=options.engines?.[step.name];if(!options.engines||!Object.hasOwn(options.engines,step.name)||!registration||typeof registration.source!=='string')throw new Error('Unknown native selector engine: '+step.name);const factory=(0,eval)('('+registration.source+')');const engine=typeof factory==='function'?factory():factory;if(!engine||typeof engine.query!=='function'||typeof engine.queryAll!=='function')throw new Error('Native selector engine must define query and queryAll');customEngines.set(step.name,engine);}
      const engine=customEngines.get(step.name),found=new Set();for(const root of roots){const nodes=engine.queryAll(root,step.selector);if(!nodes||typeof nodes[Symbol.iterator]!=='function')throw new Error('Native selector queryAll must return an iterable of elements');for(const node of nodes){tick();if(nativeQueryIntrinsics)nativeQueryIntrinsics.validateCustomNode(node,root);else if(!(node instanceof Element)||nativeOwnerDocument(node)!==(nativeNodeType(root)===9?root:nativeOwnerDocument(root)))throw new Error('Native selector returned an element outside its frame');found.add(node);if(found.size>100000)throw new Error('Native DOM query node limit exceeded');}}roots=[...found];
      if(hiddenStyles)hiddenStyles.clear();
    }else if(step.engine==='xpath'){
      const found=new Set();for(const root of roots){const expression=nativeNodeType(root)!==9&&step.selector.startsWith('/')?'.'+step.selector:step.selector;let result;try{result=document.evaluate(expression,root,null,XPathResult.ORDERED_NODE_ITERATOR_TYPE);}catch(cause){const original=nativeRegexReplace(nativeRegexReplace(step.selector,/\\/g,'\\\\'),/'/g,"\\'");throw new Error("Invalid XPath selector '"+original+"'",{cause});}let node;while((node=result.iterateNext()))if(nativeNodeType(node)===1)found.add(node);}roots=[...found];
    }else if(['text','role','label','placeholder','alt','title','testId'].includes(step.engine)){
      const found=new Set();
      for(const root of roots)for(const e of step.engine==='text'?[...(nativeNodeType(root)===1?[root]:[]),...enumerate(root,!!step.light)]:enumerate(root,!!step.light)){
        let matched=false;
        if(step.engine==='text')matched=smallestTextMatch(e,step);
        if(step.engine==='role')matched=roleMatches(e,step);
        if(step.engine==='label')matched=!!(e.labels?.length||e.getAttribute('aria-label')||e.getAttribute('aria-labelledby'))&&matchText(name(e,{referenced:true}),step);
        if(step.engine==='placeholder')matched=matchText(e.getAttribute('placeholder')||'',step)&&e.hasAttribute('placeholder');
        if(step.engine==='alt')matched=matchText(e.getAttribute('alt')||'',step)&&e.hasAttribute('alt');
        if(step.engine==='title')matched=matchText(e.getAttribute('title')||'',step)&&e.hasAttribute('title');
        if(step.engine==='testId')matched=(step.attribute||'data-testid').split(',').some(attribute=>{const value=e.getAttribute(attribute.trim());return value!==null&&(step.pattern!==undefined?nativeRegexTest(nativeRegexCreate(step.pattern,step.flags||''),value):value===step.text);});
        if(matched)found.add(e);
      }
      roots=[...found];
    }else if(step.engine==='filter'){
      roots=roots.filter(e=>(step.hasText===undefined||textMatch(e,step.hasText))&&(step.hasNotText===undefined||!textMatch(e,step.hasNotText))&&(step.has===undefined||run(step.has,e,depth+1).length>0)&&(step.hasNot===undefined||run(step.hasNot,e,depth+1).length===0)&&(step.visible===undefined||visible(e)===step.visible));
    }else if(step.engine==='capture'){
      const found=new Set();for(const root of roots)for(const e of run(step.query,root,depth+1))if(!step.descendants.length||run(step.descendants,e,depth+1).length)found.add(e);roots=[...found];
    }else if(step.engine==='chain'){
      const found=new Set();for(const root of roots)for(const e of run(step.query,root,depth+1))found.add(e);roots=[...found];
    }else if(step.engine==='and'||step.engine==='or'){
      const alternate=run(step.query,start,depth+1),members=new Set(alternate);
      roots=step.engine==='and'?roots.filter(e=>members.has(e)):[...new Set([...roots,...alternate])];
      roots.sort((a,b)=>a===b?0:a.compareDocumentPosition(b)&Node.DOCUMENT_POSITION_PRECEDING?1:-1);
    }else if(step.engine==='nth'){
      if(!Number.isInteger(step.index))throw new Error('Invalid native locator index');
      const index=step.index<0?roots.length+step.index:step.index;roots=index>=0&&index<roots.length?[roots[index]]:[];
    }else if(step.engine==='invalid')throw new Error(step.message);
    else throw new Error('Unsupported native locator query engine');
  }
  return roots;
  };
  if(options.elementStateOnly){
    if(!(start instanceof Element))throw new Error('Native element state requires an element');
    // State checks follow genuine browser control relationships. Visibility
    // and all direct DOM getters retain the original selected node identity.
    let target=start,control=false;
    if(!start.isContentEditable)for(let ancestor=start;ancestor;ancestor=parent(ancestor)){
      tick();if(ancestor.matches('input,textarea,select,button,option,optgroup,fieldset,a[href],[role=button],[role=checkbox],[role=radio],[role=link]')){target=ancestor;control=true;break;}
    }
    if(!control)for(let ancestor=start;ancestor;ancestor=parent(ancestor)){
      tick();if(ancestor.localName==='label'){if(ancestor.control)target=ancestor.control;break;}
    }
    const nativeForm=['input','textarea','select'].includes(target.localName),r=role(target);
    const editable=nativeForm||target.isContentEditable||['checkbox','combobox','grid','gridcell','listbox','radiogroup','slider','spinbutton','textbox','searchbox','columnheader','rowheader','treegrid'].includes(r);
    const enabled=!disabled(target),readonly=!!target.readOnly||target.getAttribute('aria-readonly')==='true';
    const box=visible(start,true);return {visible:!!box,box,enabled,editable:editable?enabled&&!readonly:undefined};
  }
  if(options.visibilityOnly)return {visible:visible(start)};
  if(options.accessibilityTools)return {role,name,state,hidden,normalize,byId,pseudo};
  if(options.metadata){if(!(start instanceof Element))throw new Error('Native accessibility metadata requires an element');const invalid=start.getAttribute('aria-invalid'),ids=nativeRegexSplit((start.getAttribute('aria-errormessage')||'').trim(),/\s+/).filter(Boolean);return {role:role(start),name:name(start),description:description(start),errorMessage:((invalid&&invalid!=='false')||start.validity?.valid===false)?normalize(ids.map(id=>byId(start,id)).filter(Boolean).map(node=>name(node,{referenced:true})).join(' ')):''};}
  return run(steps,start);
})JS";
