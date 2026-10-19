// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include "include/cef_api_hash.h"
#include "network_body_publication.h"

namespace {
using Publication=suitecut::NativeNetworkBodyPublication;
Publication::Dict Event(int sequence,uint64_t body=0) {
  auto event=CefDictionaryValue::Create();event->SetInt("sequence",sequence);
  if(body){auto request=CefDictionaryValue::Create();request->SetString("postDataBodyId",std::to_string(body));event->SetDictionary("request",request);}return event;
}
}
int main() {
  assert(cef_api_hash(CEF_API_VERSION,0));
  std::vector<int> published;std::vector<uint64_t> delivered,canceled;std::vector<std::string> failures;
  bool writable=true;
  Publication publication([&](uint64_t id,size_t,size_t,std::span<const uint8_t>,bool cancel){if(!writable)return false;(cancel?canceled:delivered).push_back(id);return true;},
    [&](Publication::Dict event){published.push_back(event->GetInt("sequence"));},[&](const std::string& error){failures.push_back(error);});
  const auto first=publication.Begin({0,255,1},100),second=publication.Begin({3,4},100);assert(first && second);
  assert(publication.Queue(Event(1,*first)));assert(publication.Queue(Event(2,*second)));assert(publication.Queue(Event(3)));assert(publication.QueuedCursor()==3&&publication.PublishedCursor()==0);
  writable=false;publication.Tick(1);assert(delivered.empty() && !publication.Received(*first));
  writable=true;publication.Tick(2);assert(delivered.size()==2);assert(publication.Received(*second));assert(published.empty());
  assert(publication.bytes()==5);assert(publication.Received(*first));assert(published==std::vector<int>({1,2,3}));assert(publication.PublishedCursor()==3);
  assert(publication.bytes()==5 && publication.owners()==2);assert(publication.Taken(*first));assert(publication.Taken(*first));assert(publication.Taken(*second));assert(!publication.NeedsTick());
  const auto expiring=publication.Begin({8},10);assert(expiring && publication.Queue(Event(4,*expiring)));
  publication.Tick(10);assert(published.back()==4 && canceled.back()==*expiring && publication.bytes()==0);
  const auto ready=publication.Begin({9},20);assert(ready && publication.Queue(Event(5,*ready)));publication.Tick(11);assert(publication.Received(*ready));
  publication.Tick(20);publication.Tick(21);assert(failures.size()==1);publication.Clear();assert(publication.bytes()==0 && !publication.NeedsTick());
  assert(publication.Queue(Event(6,9999)) && failures.size()==1);publication.Clear();
  // Repeated genuine metadata keeps the consumed body token valid. Gates own
  // original command deadlines and erase entries before synchronous callbacks.
  const auto gated=publication.Begin({0,255},100);assert(gated);bool received=false,taken=false;
  assert(publication.Wait(*gated,1,90,false,[&](bool ok,const std::string&){received=ok;}));
  assert(publication.Wait(*gated,2,90,true,[&](bool ok,const std::string&){taken=ok;}));
  assert(publication.Wait(*gated,3,90,true,[](bool,const std::string&){assert(false);}));publication.CancelWait(3);
  publication.Tick(1);assert(publication.Received(*gated) && received && !taken && publication.waiters()==1);
  assert(publication.Taken(*gated) && taken && publication.waiters()==0);assert(publication.Queue(Event(7,*gated)));
  bool replay=false;assert(publication.Wait(*gated,4,90,true,[&](bool ok,const std::string&){replay=ok;}));assert(replay);
  const auto timeout=publication.Begin({3},100);assert(timeout);bool expired=false;
  assert(publication.Wait(*timeout,5,2,true,[&](bool ok,const std::string& error){expired=!ok && error.find("deadline")!=std::string::npos;}));
  publication.Tick(2);assert(expired && !publication.waiters());publication.Clear();
  // A result cursor snapshots earlier records; an unrelated later body cannot
  // extend it. The native publication callback must return before cursor advances.
  {uint64_t published=0;Publication* actual=nullptr;Publication fenced([](uint64_t,size_t,size_t,std::span<const uint8_t>,bool){return true;},[&](Publication::Dict){assert(actual->PublishedCursor()==published);++published;},[](const std::string&){assert(false);});actual=&fenced;
    const auto first=fenced.Begin({1},100),later=fenced.Begin({2},100);assert(first&&later);assert(fenced.Queue(Event(1,*first)));const auto original=fenced.QueuedCursor();assert(fenced.Queue(Event(2,*later)));fenced.Tick(1);assert(fenced.Received(*first));assert(fenced.PublishedCursor()==original&&fenced.PublishedCursor()<fenced.QueuedCursor());fenced.Clear();
  }
  // Receipt may synchronously trigger metadata consumption during publication.
  Publication* reentrant=nullptr;uint64_t active=0;
  Publication recursive([&](uint64_t id,size_t,size_t,std::span<const uint8_t>,bool cancel){if(!cancel)assert(reentrant->Received(id));return true;},
    [&](Publication::Dict){assert(reentrant->Taken(active));},[&](const std::string&){assert(false);});reentrant=&recursive;
  const auto owned=recursive.Begin({0,255},100);assert(owned);active=*owned;assert(recursive.Queue(Event(1,active)));recursive.Tick(1);assert(recursive.bytes()==0 && !recursive.NeedsTick());
  // Cancellation delivery can synchronously close the owner without stale iterators.
  Publication* closing=nullptr;Publication close_on_cancel([&](uint64_t,size_t,size_t,std::span<const uint8_t>,bool cancel){if(cancel)closing->Clear();return true;},
    [&](Publication::Dict){},[&](const std::string&){assert(false);});closing=&close_on_cancel;
  assert(close_on_cancel.Begin({1},1));close_on_cancel.Tick(2);assert(!close_on_cancel.NeedsTick());
  // Receipt alone does not release the bounded receiver reservation.
  Publication cap([](uint64_t,size_t,size_t,std::span<const uint8_t>,bool){return true;},[](Publication::Dict){},[](const std::string&){assert(false);});
  for(size_t i=0;i<8;++i){const auto id=cap.Begin(std::vector<uint8_t>(2097152,static_cast<uint8_t>(i)),100);assert(id);for(size_t tick=0;tick<86;++tick)cap.Tick(1);assert(cap.Received(*id));}
  assert(cap.bytes()==16777216 && !cap.Begin({1},100));cap.Clear();
  // Asynchronous native POST preparation is admitted into the original result
  // cursor immediately. Exact bytes still need their genuine transport ACK.
  {std::vector<Publication::Dict> actual;Publication prepare([](uint64_t,size_t,size_t,std::span<const uint8_t>,bool){return true;},[&](Publication::Dict event){actual.push_back(event->Copy(false));},[](const std::string&){assert(false);});
    const auto pending=prepare.Prepare(100);assert(pending);auto first=Event(40);auto info=CefDictionaryValue::Create();info->SetBool("postDataUnavailable",true);first->SetDictionary("request",info);assert(prepare.Queue(first,*pending));const auto cursor=prepare.QueuedCursor();assert(prepare.Queue(Event(41)));assert(prepare.PublishedCursor()==0&&prepare.preparations()==1);
    const auto exact=prepare.Begin({0,255,13,10},100);assert(exact);auto fields=CefDictionaryValue::Create();fields->SetString("postDataBodyId",std::to_string(*exact));assert(prepare.Prepared(*pending,fields));assert(prepare.preparations()==0&&actual.empty());prepare.Tick(1);assert(prepare.Received(*exact));assert(actual.size()==2&&actual.front()->GetDictionary("request")->GetString("postDataBodyId")==std::to_string(*exact)&&!actual.front()->GetDictionary("request")->HasKey("postDataUnavailable"));assert(prepare.PublishedCursor()>=cursor);assert(prepare.Taken(*exact));prepare.Clear();
    const auto expired=prepare.Prepare(10);assert(expired);auto second=Event(42);second->SetDictionary("request",CefDictionaryValue::Create());assert(prepare.Queue(second,*expired));prepare.Tick(10);assert(actual.back()->GetDictionary("request")->GetBool("postDataUnavailable")&&prepare.preparations()==0);assert(!prepare.Prepared(*expired,fields));
  }
  // Admission is independently bounded across genuine native page channels;
  // clear refunds only preparation slots, not another channel's bytes/owners.
  {Publication one([](uint64_t,size_t,size_t,std::span<const uint8_t>,bool){return true;},[](Publication::Dict){},[](const std::string&){assert(false);});Publication two([](uint64_t,size_t,size_t,std::span<const uint8_t>,bool){return true;},[](Publication::Dict){},[](const std::string&){assert(false);});Publication three([](uint64_t,size_t,size_t,std::span<const uint8_t>,bool){return true;},[](Publication::Dict){},[](const std::string&){assert(false);});
    for(int i=0;i<64;++i){assert(one.Prepare(100));assert(two.Prepare(100));}assert(!one.Prepare(100)&&!three.Prepare(100));one.Clear();assert(three.Prepare(100));two.Clear();three.Clear();assert(suitecut::GlobalNetworkBodyQuota().preparations==0);
  }

}
