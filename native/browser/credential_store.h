// SPDX-License-Identifier: MIT
#pragma once
#include <map>
#include <set>
#include <string>
#include <vector>
#include "credential_crypto.h"
namespace suitecut {
// Context-owned canonical credential material. Actual browser authenticators are
// separately acknowledged native leases; this registry never fabricates them.
class CredentialStore {
 public:
  static constexpr size_t kMaxCredentials = 128, kMaxBytes = 1048576;
  static size_t Bytes(const VirtualCredential& item) { return item.id.size()+item.rp_id.size()+item.user_handle.size()+item.private_key.size()+item.public_key.size()+sizeof(item.sign_count); }
  bool Add(VirtualCredential item,std::string& error) {
    if (!ValidateVirtualCredential(item,error)) return false;
    if (entries_.contains(item.id)) { error = "WebAuthn credential id is already registered"; return false; }
    const size_t bytes = Bytes(item);
    if (entries_.size() >= kMaxCredentials || bytes > kMaxBytes-bytes_) { error = "Native WebAuthn credential capacity exceeded"; return false; }
    bytes_ += bytes; const auto id = item.id; entries_.emplace(id,std::move(item)); return true;
  }
  bool Replace(const std::vector<VirtualCredential>& items,std::string& error) {
    if (items.size() > kMaxCredentials) { error = "Native WebAuthn credential capacity exceeded"; return false; }
    CredentialStore proposed;
    for (const auto& item : items) if (!proposed.Add(item,error)) return false;
    *this = std::move(proposed); return true;
  }
  bool Remove(const std::string& id) {
    const auto found = entries_.find(id); if (found == entries_.end()) return false;
    bytes_ -= Bytes(found->second); entries_.erase(found); return true;
  }
  const VirtualCredential* Find(const std::string& id) const { const auto found = entries_.find(id); return found == entries_.end() ? nullptr : &found->second; }
  bool Asserted(const std::string& id,uint32_t count) {
    const auto found = entries_.find(id); if (found == entries_.end() || count < found->second.sign_count) return false;
    found->second.sign_count = count; return true;
  }
  std::vector<VirtualCredential> Get(const std::string& rp = "",const std::string& id = "") const {
    std::vector<VirtualCredential> output;
    for (const auto& [key,item] : entries_) if ((rp.empty() || item.rp_id == rp) && (id.empty() || key == id)) output.push_back(item);
    return output;
  }
  size_t Size() const { return entries_.size(); }
  size_t Bytes() const { return bytes_; }
 private:
  std::map<std::string,VirtualCredential> entries_; size_t bytes_ = 0;
};
}
