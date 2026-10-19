// SPDX-License-Identifier: MIT
#pragma once

// Resolve an associated label only for targets that are not already editable
// controls. This runs on the genuine selected node in its native utility realm;
// the C++ action scheduler retains frame identity, deadline and input ownership.
inline constexpr const char* kSuiteCutEditableTarget = R"JS((element) => {
  if (element instanceof HTMLInputElement || element instanceof HTMLTextAreaElement || element.isContentEditable)
    return element;
  const label = element.closest('label');
  return label && label.control ? label.control : element;
})JS";
