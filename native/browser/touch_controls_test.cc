// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "touch_controls.h"
#include <cassert>
#include <limits>
#include <vector>
int main(){
  using C=SuiteCutTouchControls;
  std::deque<std::pair<C::Packet,C::Reply>> pending;
  std::vector<std::pair<int,bool>> completed;
  C controls({[&](C::Packet packet,C::Reply reply){pending.emplace_back(packet,std::move(reply));},
    [&](int owner,bool ok,const std::string&){completed.emplace_back(owner,ok);}});
  auto ack=[&](bool ok){auto call=std::move(pending.front());pending.pop_front();call.second(ok,ok?"":"actual failure");};
  controls.Tap(1,1,2);assert(completed.back()==std::make_pair(1,false));
  controls.Configure(2,true);assert(controls.physical()==1&&!controls.enabled());
  ack(true);assert(controls.enabled());
  controls.Tap(3,10,20);assert(pending.front().first.kind==C::Kind::Start);
  controls.Cancel(3);controls.Tap(4,30,40);
  assert(controls.physical()==1&&controls.queued()==1);
  ack(true);assert(pending.front().first.kind==C::Kind::Cancel&&controls.held());
  ack(true);assert(pending.front().first.kind==C::Kind::Start);
  ack(true);assert(pending.front().first.kind==C::Kind::End);
  ack(true);assert(!controls.held()&&controls.physical()==0&&completed.back()==std::make_pair(4,true));
  for(auto result:completed)assert(result.first!=3);
  controls.Tap(5,0,0);ack(false);assert(pending.front().first.kind==C::Kind::Cancel);ack(true);
  assert(completed.back()==std::make_pair(5,false)&&!controls.held());
  controls.Configure(6,false);controls.Cancel(6);ack(true);assert(!controls.enabled());
  controls.Tap(7,std::numeric_limits<double>::quiet_NaN(),0);assert(completed.back()==std::make_pair(7,false));
  controls.Configure(8,true);ack(true);
  controls.Tap(9,0,0);for(int i=10;i<73;++i)controls.Tap(i,0,0);
  controls.Tap(73,0,0);assert(completed.back()==std::make_pair(73,false)&&controls.queued()==63);
  controls.Close();assert(controls.queued()==0&&controls.physical()==1);
  ack(true);assert(pending.front().first.kind==C::Kind::Cancel);ack(true);
  assert(controls.physical()==0&&!controls.held());
}
