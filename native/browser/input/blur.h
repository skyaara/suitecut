// SPDX-License-Identifier: MIT
#ifndef SUITECUT_BLUR_H_
#define SUITECUT_BLUR_H_

// Actual DOM focus control; the native owner resolves and pins the utility node.
inline constexpr char kSuiteCutBlur[] = R"JS(function(element){
  if(!element||!element.isConnected)throw new Error('Element is not attached to the DOM');
  if(element.nodeType!==1)throw new Error('Node is not an Element');
  if(typeof element.blur!=='function')throw new Error('Element does not support blur');
  element.blur();
  return {ready:true};
})JS";

#endif  // SUITECUT_BLUR_H_
