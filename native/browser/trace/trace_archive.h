// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace suitecut {
// An authored ZIP writer. Stored entries preserve the exact native capture
// bytes; the archive is readable by standard ZIP tools without a custom codec.
// It writes plain ZIP32 records and switches to ZIP64 records only where a
// size, offset or the entry count does not fit ZIP32, so there is no count or
// byte cap. The only format bound left is the 16-bit entry-name length field.
class TraceArchive {
 public:
  static bool ValidName(const std::string& name);
  bool Add(std::string name, std::vector<uint8_t> bytes, std::string& error);
  bool AddText(std::string name, const std::string& text, std::string& error);
  bool Encode(std::vector<uint8_t>& output, std::string& error) const;
  const std::map<std::string, std::vector<uint8_t>>& Entries() const { return entries_; }
  void Clear() {
    entries_.clear();
    bytes_ = 0;
  }
  size_t Bytes() const { return bytes_; }
  size_t Count() const { return entries_.size(); }
  // Write ZIP64 records even where ZIP32 fields would fit (still a valid ZIP).
  void ForceZip64(bool value) { force_zip64_ = value; }

 private:
  std::map<std::string, std::vector<uint8_t>> entries_;
  size_t bytes_ = 0;
  bool force_zip64_ = false;
};
}  // namespace suitecut
