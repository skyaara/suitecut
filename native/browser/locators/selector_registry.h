// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <string_view>

// Engine-owned registrations are immutable and shared by that engine's pages.
// The caller invokes this registry on the browser UI thread.
class SuiteCutSelectorRegistry {
 public:
  struct Entry { std::string source; bool content_script = false; };
  bool Register(const std::string& name, const std::string& source, bool content_script,
                std::string& error) {
    if (name.empty()) { error = "Native selector name must not be empty"; return false; }
    for (const unsigned char character : name) {
      if (!((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') || character == '_' || character == '-')) {
        error = "Native selector names may contain only letters, numbers, underscores, and dashes"; return false;
      }
    }
    for (const std::string_view reserved : {"css", "css:light", "xpath", "xpath:light", "text", "text:light", "id", "data-testid", "data-test-id", "data-test", "nth", "role", "visible", "internal:control"})
      if (name == reserved) { error = "Native selector name is reserved: " + name; return false; }
    if (entries_.contains(name)) { error = "Native selector engine is already registered: " + name; return false; }
    if (source.empty()) { error = "Native selector source must not be empty"; return false; }
    entries_.emplace(name, Entry{source, content_script}); bytes_ += source.size(); return true;
  }
  const std::map<std::string, Entry>& entries() const { return entries_; }
  size_t bytes() const { return bytes_; }
 private:
  std::map<std::string, Entry> entries_;
  size_t bytes_ = 0;
};
