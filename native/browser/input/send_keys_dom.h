// SPDX-License-Identifier: MIT
#pragma once

// Standards send-keys preparation on a genuine resolved utility-world node.
// This differs from Playwright-style type, whose existing caret semantics stay
// unchanged. Native C++ owns frame epochs, deadline and physical input ACKs.
inline constexpr const char* kSuiteCutSendKeysDom = R"JS((element, text) => {
  if (!element.isConnected) throw new Error('Element is not attached to the DOM');
  if (!(element instanceof HTMLElement)) throw new Error('Element not interactable: not an HTML element');
  const document = element.ownerDocument;
  const style = getComputedStyle(element);
  if (!element.getClientRects().length || style.visibility === 'hidden' || style.visibility === 'collapse' || element.matches(':disabled'))
    throw new Error('Element not interactable: hidden or disabled');
  const input = element instanceof HTMLInputElement;
  const textarea = element instanceof HTMLTextAreaElement;
  if (input && element.type === 'file') throw new Error('File send-keys requires the native file upload pipeline');
  const root = element.getRootNode();
  const active = root instanceof ShadowRoot ? root.activeElement : document.activeElement;
  const alreadyFocused = active === element;
  // Scroll only enough to reveal the real element; an obscured or transparent
  // keyboard target does not require pointer hit-testing or opacity checks.
  element.scrollIntoView({block: 'nearest', inline: 'nearest', behavior: 'instant'});
  if (!alreadyFocused) element.focus({preventScroll: true});
  const focused = root instanceof ShadowRoot ? root.activeElement : document.activeElement;
  // Body is the document's keyboard viewport. A native body.focus() no-op
  // must preserve the real focused descendant and sequential Tab origin.
  // Every other target still requires actual platform focus acquisition.
  if (focused !== element && element !== document.body) throw new Error('Element not interactable: platform focus was not acquired');
  if (input && ['color', 'date', 'time', 'datetime-local', 'month', 'range', 'week'].includes(element.type) && !element.readOnly) {
    const probe = element.cloneNode();
    probe.value = text;
    if (text !== '' && (probe.value === '' || (element.type === 'color' && probe.value.toLowerCase() !== text.toLowerCase())))
      throw new Error('Invalid send-keys value for input type ' + element.type);
    // These real DOM setter events are programmatic and remain isTrusted=false.
    // Chromium performs its own value sanitization; trust is never overridden.
    element.value = text;
    element.dispatchEvent(new Event('input', {bubbles: true, composed: true}));
    element.dispatchEvent(new Event('change', {bubbles: true}));
    return {ready: true, done: true};
  }
  let caretEnd = false;
  if (!alreadyFocused) {
    if ((input || textarea) && !element.readOnly) {
      try { element.setSelectionRange(element.value.length, element.value.length); }
      catch (error) {
        if (!(error instanceof DOMException) || error.name !== 'InvalidStateError') throw error;
        caretEnd = true; // Email/number require a genuine physical End key.
      }
    } else if (element.isContentEditable) {
      const range = document.createRange();
      range.selectNodeContents(element);
      range.collapse(false);
      const selection = document.getSelection();
      selection.removeAllRanges();
      selection.addRange(range);
    }
  }
  return {ready: true, caretEnd};
})JS";
