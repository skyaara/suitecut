// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>
namespace suitecut {
// Retained includes the old vector capacity. Any replacement is reserved
// against simultaneous old+new capacity before allocation or byte mutation.
template<class Fill>
inline bool AppendNativeHTTPUpload(std::vector<uint8_t>&bytes,size_t amount,
    size_t retained,size_t per_upload,size_t budget,Fill fill,std::string&error){
  const auto fail=[&](const char*message){error=message;return false;};
  if(bytes.capacity()>per_upload||bytes.size()>per_upload||amount>per_upload-bytes.size()||retained<bytes.capacity()||retained>budget)return fail("Native API simultaneous upload capacity exceeded");
  if(!amount){error.clear();return true;}
  const size_t old_size=bytes.size(),next_size=old_size+amount;
  if(next_size<=bytes.capacity()){
    bytes.resize(next_size);
    if(!fill(bytes.data()+old_size,amount)){bytes.resize(old_size);return fail("Native API source bytes were not fully copied");}
    error.clear();return true;
  }
  const size_t available=budget-retained;
  if(next_size>available)return fail("Native API simultaneous upload capacity exceeded");
  size_t capacity=std::max(next_size,bytes.capacity()>per_upload/2?per_upload:bytes.capacity()*2);
  if(capacity>available)capacity=next_size;
  std::vector<uint8_t>next(capacity);next.resize(next_size);
  if(next.capacity()>available)return fail("Native API simultaneous upload capacity exceeded");
  std::copy(bytes.begin(),bytes.end(),next.begin());
  if(!fill(next.data()+old_size,amount))return fail("Native API source bytes were not fully copied");
  bytes.swap(next);error.clear();return true;
}
// UI-owned correlation only. The original Engine operation owns the reply and
// original deadline; HTTP State owns all staged bytes and real curl work.
class NativeHTTPUploadPreparations {
 public:
  struct Entry {int owner,context;uint64_t generation;double deadline;};
  bool Reserve(const std::string&token,Entry entry,double now){
    if(token.empty()||token.size()>128||token.find('\0')!=std::string::npos||entry.owner<1||entry.context<1||!entry.generation||!std::isfinite(now)||!std::isfinite(entry.deadline)||entry.deadline<0||(entry.deadline&&now>=entry.deadline)||entries_.size()>=16||entries_.contains(token))return false;
    for(const auto&[key,item]:entries_)if(item.owner==entry.owner)return false;
    entries_.emplace(token,entry);return true;
  }
  std::optional<Entry> Find(const std::string&token,int context)const{const auto found=entries_.find(token);return found==entries_.end()||found->second.context!=context?std::nullopt:std::optional<Entry>(found->second);}
  void Cancel(int owner){std::erase_if(entries_,[owner](const auto&item){return item.second.owner==owner;});}
  bool Contains(const std::string&token)const{return entries_.contains(token);}
  size_t Size()const{return entries_.size();}
 private:std::map<std::string,Entry>entries_;
};
}
