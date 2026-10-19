// SPDX-License-Identifier: MIT
#ifndef SUITECUT_SCROLL_READINESS_H_
#define SUITECUT_SCROLL_READINESS_H_

// This inspects layout only. Source owns retries, cancellation, the original
// deadline and the actual DOM.scrollIntoViewIfNeeded protocol command.
inline constexpr char kSuiteCutScrollReadiness[] = R"JS(async function(){
  const element=this;
  const attached=()=>{
    if(!element.isConnected)throw new Error('Element is not attached to the DOM');
    if(element.nodeType!==1)throw new Error('Node is not an Element');
  };
  const layout=()=>{
    const rects=[...element.getClientRects()];
    if(rects.length)return rects.map(r=>[r.x,r.y,r.width,r.height]);
    if(getComputedStyle(element).display!=='contents')return null;
    const range=document.createRange();range.selectNodeContents(element);
    const contents=[...range.getClientRects()];range.detach();
    return contents.length?contents.map(r=>[r.x,r.y,r.width,r.height]):null;
  };
  attached();
  const first=layout();
  if(!first)return {ready:false,reason:'element is not visible; retrying scroll into view action'};
  await new Promise(requestAnimationFrame);await new Promise(requestAnimationFrame);
  attached();
  const next=layout();
  if(!next)return {ready:false,reason:'element is not visible; retrying scroll into view action'};
  if(JSON.stringify(first)!==JSON.stringify(next))
    return {ready:false,reason:'element position to be stable; retrying scroll into view action'};
  return {ready:true};
})JS";

#endif  // SUITECUT_SCROLL_READINESS_H_
