// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include <memory>
#include <vector>
#include "owner_frame_controls.h"
int main(){
 using Controls=suitecut::OwnerFrameControls;
 {double now=0;int errors=0;Controls c([&]{return now;},[&](const std::string&){++errors;});auto call=c.Begin();assert(call&&c.PhysicalSize()==1&&Controls::ProcessPhysicalSize()==1&&c.NeedsTick());call->Settle();call->Settle();assert(!c.PhysicalSize()&&!Controls::ProcessPhysicalSize()&&!c.NeedsTick()&&!errors);}
 {double now=0;Controls a([&]{return now;},[](const std::string&){assert(false);}),b([&]{return now;},[](const std::string&){assert(false);});std::vector<std::shared_ptr<Controls::Physical>>held;for(size_t i=0;i<64;++i){auto call=(i%2?a:b).Begin();assert(call);held.push_back(std::move(call));}assert(Controls::ProcessPhysicalSize()==64);auto extra=a.Begin();assert(extra&&Controls::ProcessPhysicalSize()==65);held.front()->Settle();assert(Controls::ProcessPhysicalSize()==64);auto admitted=b.Begin();assert(admitted&&Controls::ProcessPhysicalSize()==65);admitted.reset();extra.reset();held.clear();assert(!Controls::ProcessPhysicalSize());}
 {double now=0;int errors=0;Controls c([&]{return now;},[&](const std::string&message){++errors;assert(message.find("physical settlement")!=std::string::npos);});auto call=c.Begin();now=29999;c.Tick();assert(!errors);now=30000;c.Tick();c.Tick();assert(errors==1&&c.Healthy()&&!c.PhysicalSize()&&!Controls::ProcessPhysicalSize()&&!c.NeedsTick());call.reset();assert(!Controls::ProcessPhysicalSize());auto again=c.Begin();assert(again&&Controls::ProcessPhysicalSize()==1);again.reset();assert(!Controls::ProcessPhysicalSize());}
 {double now=0;Controls c([&]{return now;},[](const std::string&){assert(false);});auto call=c.Begin();c.Close();c.Close();assert(!c.Begin()&&!c.PhysicalSize()&&!Controls::ProcessPhysicalSize());call->Settle();assert(!Controls::ProcessPhysicalSize());}
 {std::shared_ptr<Controls::Physical>late;{Controls c([]{return 0;},[](const std::string&){assert(false);});late=c.Begin();assert(Controls::ProcessPhysicalSize()==1);}assert(!Controls::ProcessPhysicalSize());late.reset();assert(!Controls::ProcessPhysicalSize());}
 {double now=0;Controls remote([&]{return now;},[](const std::string&){assert(false);});std::vector<std::shared_ptr<Controls::Physical>>physical;for(size_t i=0;i<64;++i)physical.push_back(remote.Begin());bool logical_origin_open=false;assert(!logical_origin_open);Controls relaunched_origin([&]{return now;},[](const std::string&){assert(false);});auto relaunched=relaunched_origin.Begin();assert(relaunched&&remote.PhysicalSize()==64&&Controls::ProcessPhysicalSize()==65);physical.clear();relaunched.reset();assert(!Controls::ProcessPhysicalSize()&&relaunched_origin.Begin());}
 {double now=0;int errors=0;Controls c([&]{return now;},[&](const std::string&message){++errors;assert(message=="Actual release failed");});auto call=c.Begin();c.Failed("Actual release failed");assert(errors==1&&c.Healthy()&&c.PhysicalSize()==1&&Controls::ProcessPhysicalSize()==1);call->Settle();assert(!Controls::ProcessPhysicalSize());}
 std::cout<<"PASS7 peer frame-owner physical ACK admission,uncapped process count,watchdog retention,realchanneldeath,latecallback andlogical-origin close isolation\n";
}
