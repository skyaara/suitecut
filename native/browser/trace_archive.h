// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace suitecut {
// An authored ZIP32 writer. Stored entries preserve the exact native capture
// bytes; the archive is readable by standard ZIP tools without a custom codec.
class TraceArchive {
 public:
  static constexpr size_t kEntries=4096,kEntryBytes=32*1024*1024,kBytes=128*1024*1024;
  static bool ValidName(const std::string& name);
  bool Add(std::string name,std::vector<uint8_t> bytes,std::string& error);
  bool AddText(std::string name,const std::string& text,std::string& error);
  bool Encode(std::vector<uint8_t>& output,std::string& error) const;
  const std::map<std::string,std::vector<uint8_t>>& Entries() const{return entries_;}
  void Clear(){entries_.clear();bytes_=0;}
  size_t Bytes() const{return bytes_;}
  size_t Count() const{return entries_.size();}
 private:
  std::map<std::string,std::vector<uint8_t>> entries_;
  size_t bytes_=0;
};
} // namespace suitecut
