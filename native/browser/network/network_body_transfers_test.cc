// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "network/network_body_transfers.h"
#include <cassert>
#include <map>
#include <random>
#include <vector>

int main() {
  using Transfers=suitecut::NativeNetworkBodyTransfers;
  bool backpressure=true;size_t callbacks=0;std::map<uint64_t,std::vector<uint8_t>> received;
  Transfers transfers([&](uint64_t id,size_t offset,size_t total,std::span<const uint8_t> bytes){if(backpressure)return false;auto& data=received[id];assert(data.size()==offset && bytes.size()<=Transfers::kChunkBytes);data.insert(data.end(),bytes.begin(),bytes.end());assert(data.size()<=total);return true;});
  // No body-size, byte or owner cap: any body leaves in kChunkBytes pieces.
  constexpr size_t kBody=3*Transfers::kChunkBytes+17;
  std::vector<uint8_t> original(kBody);std::mt19937 random(0x424f4459);for(auto& byte:original)byte=static_cast<uint8_t>(random());
  uint64_t expected_owner=0;
  const auto id=transfers.Begin(original,100,[&](uint64_t owner,bool ok,const std::string& error){assert(owner==expected_owner && ok && error.empty());assert(transfers.size()==0 && transfers.bytes()==0);++callbacks;});
  assert(id);expected_owner=*id;
  assert(id && transfers.size()==1 && transfers.bytes()==original.size());
  transfers.Tick(0);assert(received.empty() && callbacks==0 && transfers.bytes()==original.size());assert(!transfers.Flush());
  // The last accepted chunk completes the owner and frees its copy at once.
  backpressure=false;for(size_t tick=0;received[*id].size()<original.size();++tick){assert(tick<=kBody/(16*Transfers::kChunkBytes)+1);assert(callbacks==0);transfers.Tick(0);}
  assert(received[*id]==original && callbacks==1 && transfers.size()==0 && transfers.bytes()==0);assert(transfers.Issued(*id) && !transfers.Issued(*id+1) && !transfers.Issued(0));
  backpressure=true;const auto expired=transfers.Begin({0,255,13,10},50,[&](uint64_t,bool ok,const std::string& error){assert(!ok && !error.empty());assert(transfers.bytes()==0);++callbacks;});assert(expired);transfers.Tick(49);assert(callbacks==1);transfers.Tick(50);assert(callbacks==2 && transfers.size()==0);
  std::vector<uint64_t> owners;
  for(size_t i=0;i<4;++i){auto owner=transfers.Begin(original,100,[](uint64_t,bool,const std::string&){});assert(owner);owners.push_back(*owner);}
  assert(transfers.bytes()==4*kBody);assert(transfers.Begin({1},100,[](uint64_t,bool,const std::string&){}));
  assert(transfers.Cancel(owners.front()));assert(transfers.bytes()==3*kBody+1);
  transfers.Clear();assert(transfers.size()==0 && transfers.bytes()==0);
  // Past the former 128-owner table.
  for(size_t i=0;i<300;++i)assert(transfers.Begin({},100,[](uint64_t,bool,const std::string&){}));
  assert(transfers.size()==300);transfers.Clear();
  std::optional<uint64_t> replacement;
  const auto reentrant=transfers.Begin(original,100,[&](uint64_t,bool ok,const std::string&){assert(!ok);assert(transfers.bytes()==0);replacement=transfers.Begin(original,100,[](uint64_t,bool,const std::string&){});});
  assert(reentrant && transfers.Cancel(*reentrant) && replacement && *reentrant!=*replacement);assert(transfers.bytes()==original.size());transfers.Clear();
}
