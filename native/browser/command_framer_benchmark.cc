// SPDX-License-Identifier: MIT
// Isolated framing throughput; this does not measure whole-browser latency.
#include "command_framer.h"
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>
int main(){
 for(size_t length:{128U,4096U,60000U})for(size_t chunk:{67U,65536U}){
  std::string payload(length,'x');payload+='\n';size_t lines=0;const auto begin=std::chrono::steady_clock::now();
  for(int iteration=0;iteration<50000;++iteration){SuiteCutCommandFramer f;for(size_t offset=0;offset<payload.size();offset+=chunk)if(!f.Push(std::string_view(payload).substr(offset,chunk),[&](std::string value){lines+=value.size()==length;return true;}))return 1;if(!f.End())return 2;}
  const auto seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();std::printf("{\"length\":%zu,\"chunk\":%zu,\"lines\":%zu,\"seconds\":%.9f,\"MiBps\":%.3f}\n",length,chunk,lines,seconds,50000.0*payload.size()/1048576.0/seconds);
 }
}
