// SPDX-License-Identifier: MIT
#pragma once

// Native text packets edit the actual focused control/selection. Never admit
// them when focus() failed or an author synchronously redirected that focus.
// Executed on the genuine selected node in its owning native utility realm.
inline constexpr const char* kSuiteCutEditableFocus = R"JS((element) => {
  if (!element.isConnected) return false;
  const root = element.getRootNode();
  const active = root.activeElement;
  if (element instanceof HTMLInputElement || element instanceof HTMLTextAreaElement)
    return active === element;
  if (!(element instanceof HTMLElement) || !element.isContentEditable)
    return false;
  const selection = element.ownerDocument.getSelection();
  if (!selection || !selection.anchorNode || !selection.focusNode ||
      !element.contains(selection.anchorNode) || !element.contains(selection.focusNode))
    return false;
  return active === element || !!(active instanceof HTMLElement &&
    active.isContentEditable && active.contains(element));
})JS";
