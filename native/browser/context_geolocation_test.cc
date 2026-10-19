// SPDX-License-Identifier: MIT
#include "context_geolocation.h"
#include <cassert>
#include <limits>
#include <vector>
using suitecut::NativeContextGeolocation;using suitecut::NativeGeolocationValue;
int main(){
 using Result=NativeContextGeolocation::Result;using Coordinates=NativeGeolocationValue::Coordinates;
 std::vector<Result> wire;std::vector<NativeGeolocationValue> values;auto apply=[&](const auto&v,double deadline,Result done){assert(std::isfinite(deadline));values.push_back(v);wire.push_back(std::move(done));};
 NativeContextGeolocation registry;assert(registry.Seed(1,{}));bool ready=false;assert(registry.Attach(1,"a",apply,[&](bool ok,const auto&){assert(ok);ready=true;}));assert(ready&&wire.empty());
 bool done=false;assert(registry.Set(1,1,Coordinates{1,2,3},10,[&](bool ok,const auto&){assert(ok);done=true;}));assert(!done&&registry.inFlight()==1);bool second_ready=false;assert(registry.Attach(1,"b",apply,[&](bool ok,const auto&){assert(ok);second_ready=true;}));assert(registry.inFlight()==2);wire[0](true,"");assert(!done&&registry.pending()==1);wire[1](true,"");assert(done&&second_ready&&registry.pending()==0&&registry.inFlight()==0);
 bool expired=false;assert(registry.Set(2,1,std::nullopt,10,[&](bool ok,const auto&error){assert(!ok&&error.find("deadline")!=std::string::npos);expired=true;}));registry.Tick(11);assert(expired&&registry.logical()==0&&registry.pending()==1&&registry.inFlight()==2);assert(!registry.Set(3,1,Coordinates{4,5,6},20,[](bool,const auto&){}));wire[2](true,"");assert(registry.pending()==1);wire[3](true,"");assert(registry.pending()==0&&registry.inFlight()==0);assert(values[2].configured&&!values[2].coordinates);
 bool canceled=false;assert(registry.Set(4,1,Coordinates{7,8,9},30,[&](bool,const auto&){canceled=true;}));registry.Cancel(4);assert(registry.pending()==1&&registry.logical()==0&&registry.inFlight()==2);registry.Detach("a");assert(registry.inFlight()==1);wire[4](true,"");assert(registry.pending()==1);wire[5](true,"");assert(!canceled&&registry.pending()==0);
 // Initial Source joins are physically serialized with an intervening update.
 assert(registry.Seed(2,{true,Coordinates{10,11,12}}));bool bootstrap_ready=false;assert(registry.Attach(2,"initial",apply,[&](bool ok,const auto&){assert(ok);bootstrap_ready=true;}));assert(registry.inFlight()==1);bool updated=false;assert(registry.Set(5,2,Coordinates{13,14,15},40,[&](bool ok,const auto&){assert(ok);updated=true;}));assert(wire.size()==7);wire[6](true,"");assert(!bootstrap_ready&&!updated&&wire.size()==8&&registry.inFlight()==1);assert(values.back().coordinates->latitude==13);wire[7](true,"");assert(bootstrap_ready&&updated&&registry.pending()==0);
 bool closed=false;assert(registry.Set(6,2,Coordinates{16,17,18},50,[&](bool ok,const auto&){assert(!ok);closed=true;}));registry.Closing(2);assert(closed&&registry.pending()==1&&registry.inFlight()==1);registry.Closed(2);assert(registry.pending()==0&&registry.inFlight()==0);wire[8](true,"");assert(registry.pending()==0);
 // A canceled queued update cannot submit new work after the bootstrap ACK.
 assert(registry.Seed(3,{true,Coordinates{20,21,22}}));bool queued_ready=false;
 assert(registry.Attach(3,"queued",apply,[&](bool ok,const auto&){assert(ok);queued_ready=true;}));
 const auto before=wire.size();assert(registry.Set(8,3,Coordinates{23,24,25},60,[](bool,const auto&){assert(false);}));registry.Cancel(8);
 assert(registry.pending()==1&&registry.inFlight()==1);wire[before-1](true,"");assert(queued_ready&&wire.size()==before&&registry.pending()==0&&registry.inFlight()==0);registry.Closed(3);
 assert(!registry.Set(7,1,Coordinates{91,0,0},0,[](bool,const auto&){}));assert(!registry.Set(7,1,Coordinates{0,0,std::numeric_limits<double>::infinity()},0,[](bool,const auto&){}));
 for(int i=0;i<100000;++i){bool complete=false;assert(registry.Set(100+i,1,Coordinates{double(i%180)-90,double(i%360)-180,0},0,[&](bool ok,const auto&){assert(ok);complete=true;}));wire.back()(true,"");assert(complete&&registry.pending()==0&&registry.inFlight()==0);wire.clear();values.clear();}
 registry.Closed(1);assert(registry.contexts()==0&&registry.targets()==0);
}
