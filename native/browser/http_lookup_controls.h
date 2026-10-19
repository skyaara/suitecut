// SPDX-License-Identifier: MIT
#pragma once
#include <arpa/inet.h>
#include <cstdint>
#include <cmath>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace suitecut {
// A native lookup is part of the original HTTP operation. The embedder keeps
// that operation's deadline; a late host answer never creates another request.
class HTTPLookupControls {
 public:
  struct Address {std::string address;int family=0;};
  struct Event {bool canceled=false;int owner=0,context=0,port=0;uint64_t generation=0;std::string key,hostname;};
  using Done=std::function<void(std::vector<Address>,const std::string&)>;
  using Emit=std::function<void(const Event&)>;
  explicit HTTPLookupControls(Emit emit):emit_(std::move(emit)){}
  bool Begin(int owner,int context,std::string key,std::string hostname,int port,double deadline,Done done){
    if(owner<1||context<1||key.empty()||key.size()>128||hostname.empty()||hostname.size()>1024||hostname.find_first_of("\r\n")!=std::string::npos||hostname.find('\0')!=std::string::npos||port<1||port>65535||!std::isfinite(deadline)||deadline<0||!done||jobs_.contains(owner)||jobs_.size()>=64||generation_>=9007199254740991ULL)return false;
    Event event{false,owner,context,port,++generation_,std::move(key),std::move(hostname)};
    jobs_.emplace(owner,Job{event,deadline,std::move(done)});emit_(event);return true;
  }
  bool Resolve(int owner,int context,uint64_t generation,double now,std::vector<Address>addresses,const std::string&error){
    if(!std::isfinite(now)||now<0)return false;
    const auto found=jobs_.find(owner);if(found==jobs_.end()||found->second.event.context!=context||found->second.event.generation!=generation)return false;
    if(found->second.deadline&&now>=found->second.deadline){Cancel(owner,"Native HTTP original lookup deadline exceeded");return false;}
    std::string failure=error;
    if(failure.size()>4096)failure="Native HTTP host lookup error exceeds capacity";
    if(failure.empty()&&!Validate(addresses))failure="Native HTTP lookup returned invalid IP addresses";
    auto job=std::move(found->second);jobs_.erase(found);
    job.done(failure.empty()?std::move(addresses):std::vector<Address>{},failure);return true;
  }
  void Cancel(int owner,const std::string&error){const auto found=jobs_.find(owner);if(found==jobs_.end())return;auto job=std::move(found->second);jobs_.erase(found);job.event.canceled=true;emit_(job.event);job.done({},error);}
  void Clear(const std::string&error){std::vector<int>owners;for(const auto&[owner,job]:jobs_)owners.push_back(owner);for(const int owner:owners)Cancel(owner,error);}
  size_t Size()const{return jobs_.size();}
  static bool Validate(const std::vector<Address>&addresses){
    if(addresses.empty()||addresses.size()>16)return false;
    size_t bytes=0;
    for(const auto&entry:addresses){bytes+=entry.address.size();if(bytes>1024||entry.address.find('\0')!=std::string::npos)return false;
      in_addr ipv4{};in6_addr ipv6{};if(entry.family==4){if(inet_pton(AF_INET,entry.address.c_str(),&ipv4)!=1)return false;}else if(entry.family==6){if(inet_pton(AF_INET6,entry.address.c_str(),&ipv6)!=1)return false;}else return false;
    }return true;
  }
 private:
  struct Job {Event event;double deadline;Done done;};
  Emit emit_;uint64_t generation_=0;std::map<int,Job>jobs_;
};
}
