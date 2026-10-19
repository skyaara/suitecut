// SPDX-License-Identifier: MIT
#pragma once
#include "suitecut/json.hpp"
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace suitecut {
// An encrypted, authenticated JSON credential store. The caller supplies the
// master password. No password or credential value is written in plaintext.
// POSIX directories/files are private (0700/0600); existing insecure paths and
// symlinks are rejected. Cooperating writers serialize across threads/processes.
// Keep object lifetime synchronized with calls; individual operations are safe
// to call concurrently and use a bounded five-second lock acquisition deadline.
class CredentialVault {
 public:
  static constexpr std::size_t max_entries = 256;
  static constexpr std::size_t max_name_bytes = 128;
  static constexpr std::size_t max_credential_bytes = 64 * 1024;
  static constexpr std::size_t max_file_bytes = 1024 * 1024;
  // path names a file in a private directory. Missing directories are created
  // privately. Opening an existing vault authenticates it before any mutation.
  CredentialVault(const std::string& path, const std::string& master_password);
  ~CredentialVault();
  CredentialVault(CredentialVault&&) noexcept;
  CredentialVault& operator=(CredentialVault&&) noexcept;
  CredentialVault(const CredentialVault&) = delete;
  CredentialVault& operator=(const CredentialVault&) = delete;
  void put(const std::string& name, Json credential);
  Json get(const std::string& name) const;
  std::vector<std::string> list() const; // Names only; credentials remain private.
  bool remove(const std::string& name);
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace suitecut
