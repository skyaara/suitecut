// SPDX-License-Identifier: MIT
#ifndef SUITECUT_SELECT_TEXT_H_
#define SUITECUT_SELECT_TEXT_H_

// Native DOM selection only; Source owns every deadline, retry and target.
inline constexpr char kSuiteCutSelectText[] = R"JS(function(element,visible,force){
  if(!element||!element.isConnected)throw new Error('Element is not attached to the DOM');
  if(element.nodeType!==1)throw new Error('Node is not an Element');
  const label=element.closest('label');
  if(label&&label.control)element=label.control;
  if(!element.isConnected)throw new Error('Element is not attached to the DOM');
  if(!force&&!visible(element))return {ready:false,reason:'element is not visible'};
  const document=element.ownerDocument;
  if(element instanceof HTMLInputElement||element instanceof HTMLTextAreaElement){
    element.focus();element.select();
  }else{
    if(typeof element.focus==='function')element.focus();
    const selection=document.defaultView.getSelection();
    if(!selection)throw new Error('Native document selection is unavailable');
    const range=document.createRange();range.selectNodeContents(element);
    selection.removeAllRanges();selection.addRange(range);range.detach();
  }
  return {ready:true};
})JS";

#endif  // SUITECUT_SELECT_TEXT_H_
