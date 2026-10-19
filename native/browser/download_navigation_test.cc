// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "download_navigation.h"
#include <cassert>
#include <thread>
int main(){
 using Requests=suitecut::DownloadNavigationRequests;
 using Class=Requests::Classification;
 assert(Requests::ResponseDownloads(false,true," \tAttachment ; filename=real.txt","text/html"));
 assert(!Requests::ResponseDownloads(false,true,"inline; filename=attachment.html","text/html"));
 assert(!Requests::ResponseDownloads(false,true,"x-attachment; filename=real.txt","text/html"));
 assert(!Requests::ResponseDownloads(false,true,"inline; attachment=true","text/html"));
 assert(Requests::ResponseDownloads(false,true,"inline","Application/Octet-Stream"));
 assert(!Requests::ResponseDownloads(false,false,"attachment","application/octet-stream"));
 assert(Requests::ResponseDownloads(true,false,"inline","text/html"));
 Requests requests;
 const auto baseline=requests.Sequence();
 auto one=requests.Begin(1,11,"native-main-a",true,"GET","http://a/start");
 auto match=requests.Lookup(1,true,"http://a/start",baseline);
 assert(match.present&&!match.terminal&&match.classification==Class::pending);
 requests.Redirect(one,"http://a/final");requests.Response(one,"http://a/final",true);
 match=requests.Lookup(1,true,"http://a/start",baseline);
 assert(match.classification==Class::download&&match.native_id==11);
 requests.End(one);assert(requests.Lookup(1,true,"http://a/final",baseline).terminal);
 const auto after=requests.Sequence();
 assert(!requests.Lookup(1,true,"http://a/final",after).present);
 auto next=requests.Begin(1,12,"native-main-b",true,"GET","http://a/final");
 requests.Response(next,"http://a/final",false);
 match=requests.Lookup(1,true,"http://a/final",baseline);
 assert(!match.ambiguous&&match.native_id==12&&match.classification==Class::document);
 assert(!requests.Lookup(2,true,"http://a/final",0).present);
 assert(!requests.Lookup(1,false,"http://a/final",0).present);
 auto child1=requests.Begin(1,13,"child-a",false,"GET","http://a/same");
 auto child2=requests.Begin(1,14,"child-b",false,"GET","http://a/same");
 assert(requests.Lookup(1,false,"http://a/same",0).ambiguous);
 requests.End(child1);requests.End(child2);
 assert(requests.Lookup(1,false,"http://a/same",0).classification==Class::document);
 auto redirects=requests.Begin(1,15,"bounded",true,"GET","http://a/0");
 for(int index=1;index<100;++index)requests.Redirect(redirects,"http://a/"+std::to_string(index));
 assert(requests.Lookup(1,true,"http://a/31",0).present);
 assert(!requests.Lookup(1,true,"http://a/32",0).present);
 requests.PageClosed(1);assert(requests.Size()==0);
 auto longurl=requests.Begin(1,16,"main",true,"GET",std::string(9000,'x'));
 assert(!requests.Lookup(1,true,std::string(9000,'x'),0).present);requests.End(longurl);
 requests.Clear();assert(requests.Size()==0);
 assert(!requests.Begin(1,17,std::string(129,'x'),true,"GET","http://a/") );
 auto worker=[&requests](int page){for(int index=0;index<10000;++index){auto entry=requests.Begin(page,index,"frame",true,"GET","http://a/thread");if(entry){requests.Response(entry,"http://a/thread",true);requests.End(entry);}requests.Lookup(page,true,"http://a/thread",0);}};
 std::thread first(worker,2),second(worker,3);first.join();second.join();
 assert(requests.Size()<=Requests::kCapacity);requests.Clear();
 for(size_t index=0;index<Requests::kCapacity;++index)assert(requests.Begin(4,index,"frame",true,"GET","http://a/full"));
 assert(!requests.Begin(4,999,"frame",true,"GET","http://a/full"));
 requests.PageClosed(4);assert(requests.Size()==0);
}
