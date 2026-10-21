// SPDX-License-Identifier: MIT
#include "capture_clock.h"
#include <cmath>
#include <cstdio>
int main(){
  const auto near=[](double a,double b){return std::abs(a-b)<.001;};
  SuiteCutCaptureClock clock;
  if(!near(clock.Map(0,100,false,0),100))return 1;
  // New document capture can restart at zero after a zero-only blank stream.
  if(!near(clock.Map(0,200,true,0),200))return 2;
  if(!near(clock.Map(8.333,209,true,1),208.333))return 3;
  // Late delivery preserves original source time and genuine missing slots.
  if(!near(clock.Map(16.666,300,true,2),216.666))return 4;
  if(!near(clock.Map(33.333,320,true,4),233.333))return 5;
  if(!near(clock.Map(0,400,true,0),400))return 6;
  // Older metadata without counters still detects a decreasing timestamp.
  SuiteCutCaptureClock legacy;
  if(!near(legacy.Map(50,100,false),100))return 7;
  if(!near(legacy.Map(0,200,true),200))return 8;
  SuiteCutCaptureClock burst;burst.Map(0,200,true,0);
  if(!near(burst.Map(8.333,204,true,1),204))return 12;
  if(!near(burst.Map(16.666,212.333,true,2),212.333))return 13;
  SuiteCutCaptureClock cadence;cadence.Map(0,0,true,0,120);
  // Three minutes of CEF's integer-microsecond cadence must occupy 21600
  // distinct CFR slots, rather than coalescing after about 104 seconds.
  for(uint64_t i=1;i<=21600;++i){
    const double mapped=cadence.Map(i*8.333,i*1000.0/120+1,true,i,120);
    if(std::llround(mapped*120/1000)!=static_cast<long long>(i))return 9;
  }
  const double missing=cadence.Map(21602*8.333,180020,true,21602,120);
  if(std::llround(missing*120/1000)!=21602)return 10;
  // A genuine arbitrary quiet interval must retain its original duration.
  if(!near(cadence.Map(21602*8.333+100,180120,true,21603,120)-missing,100))return 11;
  std::puts("Native capture clock tests passed");
}
