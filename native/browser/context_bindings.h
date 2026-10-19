// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include "binding_name.h"
#include <limits>
#include <map>
#include <string>
#include <utility>

// Context-owned registrations are independent of pages/documents. Native Source
// controllers borrow their identity and own actual Runtime/renderer installations.
class SuiteCutContextBindings {
 public:
  struct Entry {
    int id = 0;
    std::string name, bridge, state, installer, codec, bootstrap;
    bool handle = false;
    size_t bytes = 0;
  };
  static constexpr size_t kMaxEntries = 64, kMaxBytes = 4194304, kMaxEntryBytes = 1048576;
  using Entries = std::map<std::string, Entry>;
  bool Add(const std::string& scope, const std::string& name, std::string installer,
      std::string codec, bool handle, Entry& output, std::string& error) {
    if (name.empty() || SuiteCutBindingNameUnits(name) > 256 ||
        installer.empty() || codec.empty() || installer.find('\0') != std::string::npos || codec.find('\0') != std::string::npos) {
      error = "Invalid native context binding registration"; return false;
    }
    if (entries_.contains(name)) { error = "Function \"" + SuiteCutBindingNameDiagnostic(name) + "\" has been already registered"; return false; }
    const size_t bytes = installer.size() + codec.size();
    if (bytes > kMaxEntryBytes || entries_.size() >= kMaxEntries || bytes > kMaxBytes - bytes_) {
      error = "Native context binding registration capacity exceeded"; return false;
    }
    // Reserve a version increment for removing every active entry, so cleanup
    // remains possible when a counter approaches exhaustion.
    const auto remaining = static_cast<size_t>(std::numeric_limits<int>::max() - version_);
    if (remaining < entries_.size() + 2 || next_id_ == std::numeric_limits<int>::max()) {
      error = "Native context binding version exhausted"; return false;
    }
    Entry entry; entry.id = ++next_id_; entry.name = name;
    entry.bridge = "__suitecut_context_binding_" + scope + "_" + std::to_string(entry.id);
    entry.state = entry.bridge + "_state"; entry.installer = std::move(installer);
    entry.codec = std::move(codec); entry.handle = handle; entry.bytes = bytes;
    bytes_ += bytes; ++version_; entries_.emplace(name, entry); output = std::move(entry); return true;
  }
  bool SetBootstrap(const std::string& name, int id, std::string source) {
    auto found = entries_.find(name); if (found == entries_.end() || found->second.id != id) return false;
    if (source.empty() || !found->second.bootstrap.empty() || source.size() > kMaxEntryBytes || source.size() > kMaxBytes - bytes_) return false;
    bytes_ += source.size(); found->second.bytes += source.size(); found->second.bootstrap = std::move(source); return true;
  }
  bool Remove(const std::string& name, int id) {
    const auto found = entries_.find(name); if (found == entries_.end() || found->second.id != id) return false;
    bytes_ -= found->second.bytes; entries_.erase(found); ++version_; return true;
  }
  const Entry* Find(const std::string& name) const {
    const auto found = entries_.find(name); return found == entries_.end() ? nullptr : &found->second;
  }
  const Entries& All() const { return entries_; }
  int Version() const { return version_; }
  size_t Size() const { return entries_.size(); }
  size_t Bytes() const { return bytes_; }
 private:
  Entries entries_; int next_id_ = 0, version_ = 0; size_t bytes_ = 0;
};
