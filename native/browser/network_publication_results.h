// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>
namespace suitecut {
// Author operation results wait only the publication cursor captured when their
// actual renderer/action result arrived. Future traffic cannot extend this fence.
// Source retains the original operation deadline and owns cancellation.
class NativeNetworkPublicationResults {
 public:
  struct Result{int owner;uint64_t generation,cursor;std::string json;};
  enum class Admission{Queued,Duplicate,Full};
  Admission Hold(int owner,uint64_t generation,uint64_t cursor,const std::string&json){
    if(pending_.contains(owner))return Admission::Duplicate;
    if(owner<1||!generation||!cursor||pending_.size()>=64||json.size()>16777216-bytes_)return Admission::Full;
    pending_.emplace(owner,Result{owner,generation,cursor,json});bytes_+=json.size();return Admission::Queued;
  }
  std::vector<Result> Ready(uint64_t published,const std::function<bool(int,uint64_t)>& current){
    std::vector<Result> ready;
    for(auto it=pending_.begin();it!=pending_.end();){
      if(!current(it->second.owner,it->second.generation)){bytes_-=it->second.json.size();it=pending_.erase(it);}
      else if(it->second.cursor<=published){bytes_-=it->second.json.size();ready.push_back(std::move(it->second));it=pending_.erase(it);}
      else ++it;
    }return ready;
  }
  void Cancel(int owner){const auto found=pending_.find(owner);if(found==pending_.end())return;bytes_-=found->second.json.size();pending_.erase(found);}
  void Clear(){pending_.clear();bytes_=0;}
  size_t size()const{return pending_.size();}size_t bytes()const{return bytes_;}
 private:std::map<int,Result>pending_;size_t bytes_=0;
};
} // namespace suitecut
