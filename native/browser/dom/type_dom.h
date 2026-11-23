// SPDX-License-Identifier: MIT
#pragma once

// Preparation for the native type intent. Keyboard packets remain owned by C++.
// Preserve the real selection when this target already owns focus; a newly
// focused text control starts at its beginning rather than a stale old range.
inline constexpr const char* kSuiteCutTypeDom = R"JS((element) => {
  const root = element.getRootNode();
  const wasFocused = (root instanceof ShadowRoot ? root.activeElement : element.ownerDocument.activeElement) === element;
  if (typeof element.focus === 'function') element.focus();
  if (!wasFocused && (element instanceof HTMLInputElement || element instanceof HTMLTextAreaElement)) {
    try { element.setSelectionRange(0, 0); }
    catch (error) {
      // Number/email and other non-selection controls retain their actual
      // browser focus/caret behavior. Do not fabricate a range or key event.
      if (!(error instanceof DOMException) || error.name !== 'InvalidStateError') throw error;
    }
  }
  return {ready: true, focused: true};
})JS";
