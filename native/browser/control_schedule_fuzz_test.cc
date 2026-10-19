// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "command_framer.h"
#include "operation_registry.h"
#include "action_pauses.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <random>
#include <vector>

// Deliberately retains the simple byte-at-a-time specification. Differential
// tests compare every transport boundary, error state and delivered byte.
struct ReferenceFramer {
  std::string pending; bool failed=false;
  bool Push(std::string_view bytes, std::vector<std::string>& delivered, size_t reject) {
    if(failed)return false;
    for(char byte:bytes){if(byte=='\n'){delivered.push_back(std::move(pending));if(delivered.size()==reject){failed=true;return false;}pending.clear();}
      else {if(byte=='\0'||pending.size()>=65535){failed=true;return false;}pending+=byte;}}
    return true;
  }
};
int main(int argc,char**argv){
  const unsigned count=argc>1?std::stoul(argv[1]):1000000;std::mt19937 random(0x41c0ffee);
  for(unsigned iteration=0;iteration<count;++iteration){
    SuiteCutCommandFramer actual;ReferenceFramer reference;std::vector<std::string>a,b;
    std::string stream;const auto length=iteration%1024==0?65530+random()%20:random()%512;
    for(size_t i=0;i<length;++i){const auto byte=random()%256;stream+=char(byte);}
    // Most cases exercise ordinary split/multiple/empty lines; others include
    // NUL/overflow/unfinished bytes and rejected delivery.
    if(iteration%3==0)for(char&byte:stream)if(byte=='\0')byte='x';
    if(iteration%1024==0)stream.assign(length,'x');
    if(iteration%2==0)stream+='\n';
    const auto reject=iteration%7==0?1+random()%8:0;
    for(size_t offset=0;offset<stream.size();){
      const auto n=std::min(stream.size()-offset,size_t(1+random()%129));auto bytes=std::string_view(stream).substr(offset,n);
      const bool left=actual.Push(bytes,[&](std::string line){a.push_back(std::move(line));return a.size()!=reject;});
      const bool right=reference.Push(bytes,b,reject);assert(left==right&&a==b&&actual.BufferedBytes()==reference.pending.size());
      assert(actual.End()==(!reference.failed&&reference.pending.empty()));offset+=n;
    }
  }
  SuiteCutOperationRegistry operations;std::map<int,SuiteCutOperationRegistry::Entry>model;double now=0;uint64_t last_generation=0;
  for(unsigned iteration=0;iteration<count;++iteration){
    const int id=1+random()%160;const auto action=random()%12;
    if(action<5){const bool control=action==4;double timeout=random()%10?double(random()%100):0;double admitted=now;
      if(iteration%113==0)timeout=std::numeric_limits<double>::quiet_NaN();
      if(iteration%127==0)timeout=-1;
      if(iteration%131==0)admitted=std::numeric_limits<double>::infinity();
      size_t controls=0;for(const auto&[unused,e]:model)controls+=e.control;
      const bool expected=std::isfinite(admitted)&&std::isfinite(timeout)&&timeout>=0&&!model.contains(id)&&model.size()<128&&(control?controls<64:model.size()-controls<64);
      const auto result=control?operations.BeginControl(id,admitted,timeout):operations.Begin(id,admitted,timeout);assert(bool(result)==expected);
      if(result){assert(result->generation>last_generation);last_generation=result->generation;assert(result->control==control&&result->deadline_ms==(timeout==0?0:admitted+timeout));model[id]=*result;}
    }else if(action<8){assert(operations.Finish(id)==bool(model.erase(id)));}
    else if(action==8){const auto found=model.find(id);const uint64_t generation=found==model.end()?0:found->second.generation;const double deadline=random()%2?now+random()%50:0;
      const bool expected=found!=model.end()&&(found->second.deadline_ms==0||(deadline>0&&deadline<=found->second.deadline_ms));assert(operations.BindOriginalDeadline(id,generation,deadline)==expected);if(expected)found->second.deadline_ms=deadline;
    }else if(action==9){now+=1;std::vector<int>expired;for(const auto&[key,e]:model){assert(operations.IsCurrent(key,e.generation));if(e.deadline_ms&&now>=e.deadline_ms)expired.push_back(key);}assert(operations.Expired(now)==expired);}
    else if(action==10&&iteration%97==0){operations.Clear();model.clear();}
    const auto found=model.find(id);const auto actual=operations.Get(id);assert(bool(actual)==(found!=model.end()));if(actual)assert(actual->generation==found->second.generation&&actual->deadline_ms==found->second.deadline_ms);
    assert(operations.Size()==model.size());
  }
  operations.Clear();assert(operations.Size()==0);
  // Never manufacture a non-expiring operation from non-finite/negative clocks.
  for(double bad:{-1.0,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}){
    assert(!operations.Begin(1,0,bad));assert(!operations.BeginControl(1,bad,1));}
  assert(!operations.Begin(1,std::numeric_limits<double>::max(),std::numeric_limits<double>::max()));
  SuiteCutActionPauseRegistry pauses;unsigned resumed=0;
  for(unsigned iteration=0;iteration<count;++iteration){const int id=1+random()%64;const uint64_t generation=1+iteration;auto token=pauses.Pause(id,generation,"actual-token",[&]{++resumed;});
    if(!token){pauses.Cancel(id);continue;}assert(!pauses.Take(id,generation+1,*token,"actual-token"));assert(!pauses.Take(id,generation,*token,"wrong-token"));
    if(random()%2){auto resume=pauses.Take(id,generation,*token,"actual-token");assert(resume);(*resume)();assert(!pauses.Take(id,generation,*token,"actual-token"));}else pauses.Cancel(id);assert(!pauses.Contains(id));
  }
  pauses.Clear();assert(pauses.Size()==0);
  std::printf("%u differential framing, %u operation schedules and %u pause schedules passed (seed 0x41c0ffee, %u resumes)\n",count,count,count,resumed);
}
