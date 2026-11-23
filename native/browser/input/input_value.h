// SPDX-License-Identifier: MIT
#pragma once

// Read actual control state in its native utility realm. Reading does not
// acquire focus, scroll, alter selection, or require an enabled/visible control.
inline constexpr const char* kSuiteCutInputValue = R"JS((element) => {
  const accepts = target => target instanceof HTMLInputElement || target instanceof HTMLTextAreaElement || target instanceof HTMLSelectElement;
  if (!accepts(element) && element && element.nodeType === 1) {
    const label = element.closest('label');
    if (label && label.control) element = label.control;
  }
  if (!accepts(element)) throw new Error('Node is not an <input>, <textarea> or <select> element');
  const value = element.value;
  if (typeof value !== 'string') throw new Error('Native input value is not a string');
  return value;
})JS";
