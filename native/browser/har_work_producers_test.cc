// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include "include/cef_api_hash.h"
#include "har_fetch_work.h"
#include "network_post_preparation.h"
using Book=suitecut::NativeHARWorkLeases;
using Policy=suitecut::NativeHARRouteRegistration;
using Preparation=suitecut::NativeNetworkPostPreparation;
using Publication=suitecut::NativeNetworkBodyPublication;
int main(){assert(cef_api_hash(CEF_API_VERSION,0));
 auto archive=std::make_shared<suitecut::NativeHARReplay>();std::string error;assert(archive->Load(CefParseJSON(R"({"log":{"entries":[]}})",JSON_PARSER_RFC)->GetDictionary(),error));
 auto policy=Policy::Create(archive,false),other=Policy::Create(archive,true);auto&book=suitecut::GlobalHARWorkLeases();assert(policy&&other&&!book.physical());
 auto capture=[&]{return book.Capture([&](const auto&actual){return actual==policy||actual==other;});};
 Publication publication([](uint64_t,size_t,size_t,std::span<const uint8_t>,bool){return true;},[](auto){},[](const auto&){assert(false);});
 // Retaining an actual pending read issues one independent physical receipt
 // per exact policy. Data delivery and metadata invalidation do not settle it.
 {Preparation::Data data;Preparation::Receipt receipt;Preparation prepare(publication,[&](const auto&,const auto&,double,auto actual_data,auto actual_receipt){data=std::move(actual_data);receipt=std::move(actual_receipt);},{},[](const auto&,auto,bool,const auto&,bool){});
  assert(prepare.Prepare("logical","child","request",100));assert(prepare.RetainHAR("logical",1,policy)&&prepare.RetainHAR("logical",1,policy)&&prepare.RetainHAR("logical",1,other));assert(book.physical()==2);prepare.Start("logical");auto selected=capture();assert(selected&&selected->size()==2);prepare.Invalidate("logical");data(true,{1},false,"");assert(book.physical()==2&&!selected->front()->settled());receipt();assert(!book.physical()&&selected->front()->settled()&&!selected->front()->failed());receipt();assert(!book.physical());}
 // An actual read failure is retained as failure, including after logical
 // Clear. Physical settlement still requires the independent raw receipt.
 {Preparation::Data data;Preparation::Receipt receipt;Preparation prepare(publication,[&](const auto&,const auto&,double,auto d,auto r){data=std::move(d);receipt=std::move(r);},{},[](const auto&,auto,bool,const auto&,bool){});
  assert(prepare.Prepare("failed","","request",100)&&prepare.RetainHAR("failed",2,policy));prepare.Start("failed");auto selected=capture();assert(selected&&selected->size()==1);prepare.Clear();data(false,{},false,"actual read rejected");assert(!selected->front()->settled()&&book.physical()==1);receipt();assert(selected->front()->settled()&&selected->front()->failed()&&selected->front()->error()=="actual read rejected"&&!book.physical());}
 // A retirement callback without a genuine data outcome is hard/unknown,
 // never an invented successful read or an uncharged pending operation.
 {Preparation::Receipt receipt;Preparation prepare(publication,[&](const auto&,const auto&,double,auto,auto r){receipt=std::move(r);},{},[](const auto&,auto,bool,const auto&,bool){});
  assert(prepare.Prepare("missing-data","","request",100)&&prepare.RetainHAR("missing-data",3,policy));prepare.Start("missing-data");auto selected=capture();receipt();assert(selected&&selected->front()->failed()&&selected->front()->settled()&&selected->front()->error().find("without its actual data")!=std::string::npos&&!book.physical());}
 // A request crossing HAR fallback policies keeps all actual selected policy
 // pins through one shared terminal ACK, even after no route remains logical.
 {auto work=std::make_shared<suitecut::NativeHARFetchWork>();assert(work->Add(4,"child",policy)&&work->Add(4,"child",policy)&&work->Add(4,"child",other)&&work->size()==2&&book.physical()==2);auto next=work;work.reset();auto selected=capture();assert(selected&&selected->size()==2);next->TerminalACK();assert(!book.physical()&&selected->front()->settled());next->TerminalACK();assert(!book.physical());}
 // A rejected terminal command leaves the real request paused. Failure is
 // observable immediately, but even a rejected cleanup does not retire work.
 {auto work=std::make_shared<suitecut::NativeHARFetchWork>();assert(work->Add(5,"",policy));auto selected=capture();work->Failed("actual fulfill rejected");work->Failed("cleanup also rejected");assert(selected&&selected->front()->failed()&&!selected->front()->settled()&&selected->front()->error()=="actual fulfill rejected"&&book.physical()==1);work.reset();assert(book.physical()==1&&!selected->front()->settled());book.ScopeClosed(5);assert(!book.physical()&&selected->front()->settled());}
 // Actual cleanup ACK retires the request while preserving its first genuine
 // native failure, not relabeling a failed HAR fulfillment as success.
 {suitecut::NativeHARFetchWork work;assert(work.Add(6,"",policy));auto selected=capture();work.Failed("actual continue rejected");work.TerminalACK();assert(selected&&selected->front()->failed()&&selected->front()->settled()&&selected->front()->error()=="actual continue rejected"&&!book.physical());assert(!work.Add(6,"",other));}
 // Independent read and Fetch receipts for one policy retire separately.
 {suitecut::NativeHARFetchWork work;assert(work.Add(7,"",policy));auto read=book.Issue(7,"",policy,Book::Kind::PostRead);assert(read&&book.physical()==2);work.TerminalACK();assert(book.physical()==1&&!read->receipt->settled());read->acknowledge(true,"");assert(!book.physical());}
 std::cout<<"PASS7 actual POST preparation + native HAR Fetch chain producer controls; no Source/browser integration claim\n";
}
