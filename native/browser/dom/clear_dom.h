// SPDX-License-Identifier: MIT
#pragma once

// A clear intent has browser reset semantics, distinct from inserting an empty
// string. This helper executes on the genuinely resolved utility-world DOM node.
// C++ retains the operation's frame, deadline, physical callback and cancellation.
inline constexpr const char* kSuiteCutClearDom = R"JS((element) => {
  const input = element instanceof HTMLInputElement;
  const textarea = element instanceof HTMLTextAreaElement;
  if (input || textarea) {
    if (element.readOnly)
      return {ready: false, reason: 'element to be editable'};
    if (input && !['text', 'search', 'url', 'tel', 'email', 'password',
                  'number', 'color', 'date', 'time', 'datetime-local',
                  'month', 'range', 'week'].includes(element.type))
      throw new Error('Input of type ' + JSON.stringify(element.type) + ' cannot be cleared');
    if (element.value === '' && !element.validity.badInput)
      return {ready: true, done: true};

    if (input && ['color', 'date', 'time', 'datetime-local', 'month',
                  'range', 'week'].includes(element.type)) {
      element.focus();
      // Chromium's actual value setter performs the type's reset sanitization,
      // including range midpoint/step constraints and the color default.
      element.value = '';
      // These are actual programmatic DOM events with isTrusted=false, matching
      // the existing non-text fill path. Trust is never forged or overridden.
      element.dispatchEvent(new Event('input', {bubbles: true, composed: true}));
      element.dispatchEvent(new Event('change', {bubbles: true}));
      return {ready: true, done: true};
    }
    element.focus();
    element.select();
    // Ordinary text controls use the C++ input dispatcher after this response.
    return {ready: true, done: false};
  }

  if (element instanceof HTMLElement && element.isContentEditable) {
    if (!element.firstChild) return {ready: true, done: true};
    element.focus();
    const range = element.ownerDocument.createRange();
    range.selectNodeContents(element);
    range.deleteContents();
    // Deleting the real DOM range leaves no browser editing placeholder <br>.
    // Focus events come from the platform; no synthetic input events are made.
    return {ready: true, done: true};
  }
  throw new Error('Element is not an <input>, <textarea> or [contenteditable] element');
})JS";
