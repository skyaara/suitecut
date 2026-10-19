// SPDX-License-Identifier: MIT
#include "context_routes.h"
#include "route_registry.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
using Routes=suitecut::NativeContextRoutes;
using Route=suitecut::NativeContextRouteRegistration;
struct Target {
  struct Sent {Routes::Mutation mutation;Routes::Route route;double deadline;Routes::Result reply;};
  std::deque<Sent>sent;suitecut::NativeRouteRegistry registry;
  Routes::Apply apply(){return [this](auto mutation,auto route,double deadline,auto reply){sent.push_back({mutation,std::move(route),deadline,std::move(reply)});};}
  void Ack(bool success=true){assert(!sent.empty());auto pending=std::move(sent.front());sent.pop_front();if(success){if(pending.mutation==Routes::Mutation::Install){if(pending.route->available())assert(registry.RegisterContext(pending.route));}else registry.Remove(pending.route->id());}pending.reply(success,success?"":"Actual command rejected");}
};
int main(){
  double now=100;size_t failures=0;
  {
    Routes routes([&](const auto&,const auto&){++failures;},[&]{return now;});assert(routes.Seed(1)&&routes.Seed(2));
    Target page,broker,future;int ready=0,completed=0;bool success=false;
    assert(routes.Attach(1,"source",page.apply(),[&](bool ok,const auto&){assert(ok);++ready;}));
    assert(routes.Attach(1,"broker",broker.apply(),[&](bool ok,const auto&){assert(ok);++ready;}));assert(ready==2);
    auto policy=Route::Create("shared-times","**/*",2);
    assert(routes.Install(1,1,policy,200,[&](bool ok,const auto&){success=ok;++completed;}));
    assert(routes.physical()==2&&completed==0&&page.sent.front().deadline==200);
    assert(!routes.Install(99,2,policy,200,[](bool,const auto&){}));
    assert(routes.Attach(1,"future",future.apply(),[&](bool ok,const auto&){assert(ok);++ready;}));
    assert(routes.physical()==3&&ready==2);page.Ack();broker.Ack();assert(!completed);future.Ack();assert(success&&completed==1&&ready==3&&routes.physical()==0);
    assert(page.registry.Register("page-priority","**/*",1));auto p=page.registry.Match("https://a.test/a"),b=broker.registry.Match("https://a.test/a"),f=future.registry.Match("https://a.test/a");
    assert(page.registry.Next(p)->registration=="page-priority");assert(!broker.registry.Next(b)->exhausted);assert(future.registry.Next(f)->exhausted);assert(!page.registry.Next(p));
    Target unrelated;assert(routes.Attach(2,"other-context",unrelated.apply(),[](bool ok,const auto&){assert(ok);}));assert(unrelated.sent.empty());
    // Future targets cannot reset an already consumed shared counter.
    Target late;assert(routes.Attach(1,"late",late.apply(),[](bool ok,const auto&){assert(ok);}));assert(late.sent.empty());
    auto unlimited=Route::Create("remove","**/*",0);assert(routes.Install(2,1,unlimited,0,[](bool ok,const auto&){assert(ok);}));page.Ack();broker.Ack();future.Ack();late.Ack();
    auto admitted=page.registry.Match("https://a.test/a");
    completed=0;assert(routes.Remove(3,1,{"remove"},300,[&](bool ok,const auto&){assert(ok);++completed;}));assert(!unlimited->available()&&!page.registry.Next(admitted));
    page.Ack();broker.Ack();future.Ack();assert(!completed&&routes.physical()==1);late.Ack();assert(completed==1&&routes.physical()==0);
  }
  assert(Route::retained_count()==0&&failures==0);
  {
    Routes routes([&](const auto&,const auto&){++failures;},[&]{return now;});assert(routes.Seed(1));Target page;assert(routes.Attach(1,"source",page.apply(),[](bool ok,const auto&){assert(ok);}));
    auto policy=Route::Create("cancelled","**/*",0);int replies=0;
    assert(routes.Install(11,1,policy,200,[&](bool,const auto&){++replies;}));assert(routes.physical()==1);
    routes.Cancel(11);assert(!policy->available()&&routes.pending()==0&&routes.physical()==1&&replies==0);
    page.Ack();assert(routes.physical()==1&&page.sent.front().mutation==Routes::Mutation::Remove);page.Ack();assert(routes.physical()==0&&replies==0);
    auto expired=Route::Create("expired","**/*",0);assert(routes.Install(12,1,expired,110,[&](bool ok,const auto&error){assert(!ok&&error.find("deadline")!=std::string::npos);++replies;}));
    now=110;routes.Tick(now);assert(replies==1&&routes.physical()==1&&!expired->available());page.Ack();assert(routes.physical()==1);page.Ack();assert(routes.physical()==0);
    // Actual receipt at the original deadline cannot become late logical success.
    now=120;assert(routes.Install(13,1,Route::Create("late-ack","**/*",0),130,[&](bool ok,const auto&){assert(!ok);++replies;}));now=130;page.Ack();assert(replies==2&&routes.physical()==1);page.Ack();assert(routes.physical()==0);
  }
  assert(Route::retained_count()==0);
  {
    now=100;auto routes=std::make_unique<Routes>([&](const auto&,const auto&){++failures;},[&]{return now;});assert(routes->Seed(1));Target old,replacement;
    assert(routes->Attach(1,"exact-owner",old.apply(),[](bool ok,const auto&){assert(ok);}));int failed=0;
    assert(routes->Install(20,1,Route::Create("held","**/*",0),200,[&](bool ok,const auto&){assert(!ok);++failed;}));
    routes->Detach("exact-owner");assert(failed==1&&routes->physical()==1);
    assert(routes->Attach(1,"exact-owner",replacement.apply(),[](bool ok,const auto&){assert(ok);}));assert(replacement.sent.empty());
    old.Ack();assert(routes->physical()==0&&replacement.sent.empty());
    assert(routes->Install(21,1,Route::Create("clear-held","**/*",0),0,[&](bool ok,const auto&){assert(!ok);++failed;}));assert(routes->physical()==1);
    routes->Clear();assert(failed==2&&routes->physical()==1);routes.reset();assert(Route::retained_count()>0);replacement.Ack();assert(Route::retained_count()==0);
  }
  {
    now=100;auto routes=std::make_unique<Routes>([&](const auto&,const auto&){++failures;},[&]{return now;});assert(routes->Seed(1));Target page;
    assert(routes->Attach(1,"source",page.apply(),[](bool ok,const auto&){assert(ok);}));
    assert(routes->Install(30,1,Route::Create("reentrant","**/*",0),0,[&](bool ok,const auto&){assert(ok);routes.reset();}));page.Ack();assert(!routes);
  }
  assert(Route::retained_count()==0);
  {
    now=100;Routes routes([&](const auto&,const auto&){++failures;},[&]{return now;});assert(routes.Seed(1));
    assert(routes.Install(40,1,Route::Create("future-held","**/*",0),0,[](bool ok,const auto&){assert(ok);}));
    Target pending;int ready=0;assert(routes.Attach(1,"closing-owner",pending.apply(),[&](bool ok,const auto&error){assert(!ok&&error.find("closed")!=std::string::npos);++ready;}));
    assert(routes.physical()==1&&ready==0);routes.Detach("closing-owner");assert(ready==1&&routes.physical()==1);
    pending.Ack();assert(ready==1&&routes.physical()==0);
  }
  {
    now=100;Routes routes([&](const auto&,const auto&){++failures;},[&]{return now;});assert(routes.Seed(1));Target page;
    assert(routes.Attach(1,"same-owner",page.apply(),[](bool ok,const auto&){assert(ok);}));int replies=0;
    assert(routes.Install(50,1,Route::Create("same-route","**/*",0),200,[&](bool,const auto&){assert(false);}));
    routes.Cancel(50);assert(routes.physical()==1);
    assert(routes.Install(50,1,Route::Create("same-route","**/*",0),200,[&](bool ok,const auto&){assert(ok);++replies;}));
    page.Ack();assert(replies==0&&routes.pending()==1&&routes.physical()==1&&page.sent.front().mutation==Routes::Mutation::Remove);
    page.Ack();assert(replies==0&&routes.pending()==1&&routes.physical()==1&&page.sent.front().mutation==Routes::Mutation::Install);
    page.Ack();assert(replies==1&&routes.pending()==0&&routes.physical()==0);
  }
  {
    now=100;Routes routes([&](const auto&,const auto&){++failures;},[&]{return now;}),unrelated([&](const auto&,const auto&){++failures;},[&]{return now;});assert(routes.Seed(1)&&unrelated.Seed(1));
    auto policy=Route::Create("actual-callback-policy","**/*",1);assert(routes.Install(60,1,policy,0,[](bool ok,const auto&){assert(ok);}));
    auto&leases=suitecut::GlobalRouteCallbackLeases();const auto scope=*leases.Scope(),token=*leases.Issue(scope,"actual-callback",policy);assert(leases.Start(token,scope,"actual-callback"));
    auto snapshot=routes.CaptureCallbacks(1);assert(snapshot.size()==1&&unrelated.CaptureCallbacks(1).empty());
    routes.Closed(1);assert(routes.CaptureCallbacks(1).empty()&&!snapshot.front()->settled());
    assert(leases.Finish(token,scope,"actual-callback")&&snapshot.front()->settled());
  }
  // Aggregate metadata remains charged until the last physical observer drops
  // its exact shared policy, including after manager cancellation/destruction.
  {std::vector<std::shared_ptr<Route>>held;while(auto route=Route::Create(std::to_string(held.size()),std::string(8192,'*'),0))held.push_back(std::move(route));assert(!held.empty()&&held.size()<1024&&Route::retained_bytes()<=4194304);}
  assert(Route::retained_count()==0&&Route::retained_bytes()==0);
  std::cout<<"Native context routes: 9 shared-policy/physical-lifecycle groups passed\n";
}
