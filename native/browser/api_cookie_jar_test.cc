// SPDX-License-Identifier: MIT
#include "api_cookie_jar.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <map>
#include <memory_resource>
#include <string>
#include <vector>
#include <curl/curl.h>

namespace {
using Jar=suitecut::NativeAPICookieJar;
constexpr double kNow=1792022400; // Fixed native wall clock, distinct from monotonic.
const Jar::Clock clock_now{[]{return 100.0;},kNow,200,false};
void Require(bool value,const char* expression,int line){if(!value){std::cerr<<"line "<<line<<": "<<expression<<'\n';std::abort();}}
#define CHECK(value) Require((value),#value,__LINE__)
Jar::Input Domain(std::string name,std::string value,std::string domain,std::string path="/"){
  Jar::Input input;input.name=std::move(name);input.value=std::move(value);input.domain=std::move(domain);input.path=std::move(path);return input;
}
Jar::Input URL(std::string name,std::string value,std::string url){Jar::Input input;input.name=std::move(name);input.value=std::move(value);input.url=std::move(url);return input;}
std::vector<Jar::Cookie> Cookies(Jar& jar,std::vector<std::string> urls={}){std::vector<Jar::Cookie> result;std::string error;CHECK(jar.Cookies(urls,clock_now,result,error));CHECK(error.empty());return result;}
std::string Header(Jar& jar,std::string url,const Jar::Clock& clock=clock_now){std::string result,error;CHECK(jar.Header(url,clock,result,error));CHECK(error.empty());return result;}
void Add(Jar& jar,std::vector<Jar::Input> values){std::string error;CHECK(jar.Import(values,clock_now,error));CHECK(error.empty());}
void Receive(Jar& jar,std::string url,std::vector<std::string> values,const Jar::Clock& clock=clock_now){std::string error;CHECK(jar.Receive(url,values,clock,error));CHECK(error.empty());}
class TrackingResource final : public std::pmr::memory_resource {
 public:
  size_t current=0,peak=0,allocations=0;
 private:
  std::map<void*,std::pair<size_t,size_t>> live;
  void* do_allocate(size_t bytes,size_t alignment)override{void* value=std::pmr::new_delete_resource()->allocate(bytes,alignment);CHECK(live.emplace(value,std::pair(bytes,alignment)).second);current+=bytes;peak=std::max(peak,current);++allocations;return value;}
  void do_deallocate(void* value,size_t bytes,size_t alignment)override{const auto found=live.find(value);CHECK(found!=live.end());CHECK(found->second.first==bytes);CHECK(found->second.second==alignment);current-=bytes;live.erase(found);std::pmr::new_delete_resource()->deallocate(value,bytes,alignment);}
  bool do_is_equal(const std::pmr::memory_resource& other)const noexcept override{return this==&other;}
};
void ImportedScopeAndOrder(){
  auto budget=std::make_shared<Jar::Budget>();Jar jar(budget);
  auto secure=Domain("secure","yes","two.com");secure.secure=true;
  auto strict=Domain("path","two","LOCALHOST","/input");strict.same_site=3;strict.http_only=true;
  Add(jar,{URL("root","one","http://localhost/empty.html"),strict,Domain("sub","three","one.com"),secure});
  CHECK(Header(jar,"http://localhost/input/button")=="root=one; path=two");
  CHECK(Header(jar,"http://localhost/inputted")=="root=one");
  CHECK(Header(jar,"http://sub.one.com/").empty());
  Jar dotted(std::make_shared<Jar::Budget>());Add(dotted,{Domain("sub","three",".one.com")});CHECK(Header(dotted,"http://sub.one.com/")=="sub=three");
  CHECK(Header(jar,"http://two.com/").empty());
  CHECK(Header(jar,"https://two.com/")=="secure=yes");
  const auto cookies=Cookies(jar,{"http://SUB.ONE.COM/","https://two.com/"});CHECK(cookies.size()==2);CHECK(cookies[0].domain=="one.com");CHECK(cookies[1].domain=="two.com");
  const auto all=Cookies(jar);CHECK(all.size()==4);CHECK(all[0].domain=="localhost");CHECK(all[0].path=="/");CHECK(all[0].expires==-1);CHECK(!all[0].secure);CHECK(all[1].http_only);CHECK(all[1].same_site==3);
}
void WireScopePSLAndExpiry(){
  Jar jar(std::make_shared<Jar::Budget>());
  Receive(jar,"http://a.b.one.com/setcookie.html",{"a=b","c=d; expires=Fri, 31 Dec 2100 08:00:00 GMT; domain=b.one.com; path=/input","e=f; domain=b.one.com; path=/input/subfolder","invalid=v; domain=other.com","public=v; domain=com","partition=v; Secure; Partitioned"});
  CHECK(jar.Size()==3);CHECK(Header(jar,"http://b.one.com/input/button.html")=="c=d");CHECK(Header(jar,"http://a.b.one.com/input/subfolder/file")=="a=b; c=d; e=f");CHECK(Header(jar,"http://www.a.b.one.com/").empty());
  const auto all=Cookies(jar);CHECK(all[1].expires==4133923200.0);CHECK(all[1].domain==".b.one.com");CHECK(all[1].identity>all[0].identity);
  // Host-only provenance is part of the authoritative wire store even though
  // the public storage-state format has only domain spelling.
  // Public URL selection uses a domain criterion; actual sending still
  // respects host-only provenance, as independently checked above.
  CHECK(Cookies(jar,{"http://sub.a.b.one.com/"}).size()==1);
  Receive(jar,"http://a.b.one.com/expire",{"a=; Expires=1 Jan 1000 00:00:00 +0000 (UTC)","c=; Max-Age=-9999999999999999999999999; Domain=b.one.com; Path=/input"});CHECK(jar.Size()==1);
  Receive(jar,"http://a.b.one.com/expire",{"e=; Max-Age=0; Domain=b.one.com; Path=/input/subfolder"});CHECK(jar.Size()==0);
  Receive(jar,"http://a.b.one.com/input/file",{"a=v; max-age=3600; expires=Thu, 01 Jan 1970 00:00:00 GMT","b=v; expires=Thu, 01 Jan 1970 00:00:00 GMT; max-age=3600","c=v; max-age=-1; expires=Fri, 31 Dec 2100 08:00:00 GMT","d=v; max-age=-1abc","eq=value == value=; Path=/some=value; HttpOnly; SameSite=Strict"});
  CHECK(Header(jar,"http://a.b.one.com/input/other")=="a=v; b=v; d=v");const auto updated=Cookies(jar);CHECK(updated.size()==4);CHECK(updated[0].path=="/input");CHECK(updated[0].expires==kNow+3600);CHECK(updated[2].expires==-1);CHECK(updated[3].value=="value == value=");CHECK(updated[3].http_only);CHECK(updated[3].same_site==3);
  const Jar::Clock epoch_zero{[]{return 101.0;},0,200,false};Receive(jar,"http://a.b.one.com/",{"zero=session","zero=gone; Max-Age=0"},epoch_zero);CHECK(Header(jar,"http://a.b.one.com/",epoch_zero).empty());
  const Jar::Clock later{[]{return 101.0;},kNow+3600,200,false};CHECK(Header(jar,"http://a.b.one.com/input/other",later)=="d=v");CHECK(jar.Size()==2);
}
void ExportImportWireProvenance(){
  auto budget=std::make_shared<Jar::Budget>();Jar first(budget),restored(budget);
  Add(first,{Domain("host","imported","a.b.one.com"),Domain("wide","imported",".b.one.com"),URL("url","imported","https://a.b.one.com/scope/file")});
  CHECK(Header(first,"https://a.b.one.com/scope/file")=="host=imported; wide=imported; url=imported");
  CHECK(Header(first,"https://www.a.b.one.com/scope/file")=="wide=imported");
  Receive(first,"https://a.b.one.com/",{"fresh=wire; Path=/","spread=wire; Domain=b.one.com; Path=/"});
  const auto snapshot=Cookies(first);CHECK(snapshot.size()==5);CHECK(snapshot[0].domain=="a.b.one.com");CHECK(snapshot[1].domain==".b.one.com");CHECK(snapshot[2].domain=="a.b.one.com");CHECK(snapshot[3].domain=="a.b.one.com");CHECK(snapshot[4].domain==".b.one.com");
  std::vector<Jar::Input> imported;
  for(const auto&cookie:snapshot){auto item=Domain(cookie.name,cookie.value,cookie.domain,cookie.path);item.expires=cookie.expires;item.secure=cookie.secure;item.http_only=cookie.http_only;item.same_site=cookie.same_site;imported.push_back(std::move(item));}
  std::string error;CHECK(restored.Reset(imported,clock_now,error));
  for(const std::string host:{"a.b.one.com","www.a.b.one.com","b.one.com"})CHECK(Header(restored,"https://"+host+"/scope/file")==Header(first,"https://"+host+"/scope/file"));
  CHECK(Cookies(restored,{"https://www.a.b.one.com/scope/file"}).size()==5);
}
void ReplacementAndExactSnapshot(){
  Jar jar(std::make_shared<Jar::Budget>());Add(jar,{Domain("first","old","one.com"),Domain("second","v","one.com"),Domain("","nameless","one.com")});
  const auto before=Cookies(jar);Add(jar,{Domain("first","new","one.com")});const auto after=Cookies(jar);CHECK(after.size()==3);CHECK(after[0].value=="new");CHECK(after[0].identity!=before[0].identity);CHECK(after[1].identity==before[1].identity);
  std::string error;const std::vector<uint64_t> stale{before[0].identity};CHECK(jar.Remove(stale,clock_now,error));CHECK(jar.Size()==3);const std::vector<uint64_t> exact{after[2].identity};CHECK(jar.Remove(exact,clock_now,error));CHECK(Header(jar,"http://one.com/")=="first=new; second=v");
  auto bad=Domain("bad","v","com");const auto old_bytes=jar.AllocatedBytes();const std::vector<Jar::Input> transaction{Domain("third","v","one.com"),bad};CHECK(!jar.Import(transaction,clock_now,error));CHECK(error.find("public suffix")!=std::string::npos);CHECK(jar.Size()==2);CHECK(jar.AllocatedBytes()==old_bytes);CHECK(Header(jar,"http://one.com/")=="first=new; second=v");
  auto expired=Domain("first","delete","one.com");expired.expires=0;Add(jar,{expired});CHECK(Header(jar,"http://one.com/")=="second=v");
  const std::vector<Jar::Input> reset_invalid{Domain("reset","v","one.com"),bad};CHECK(!jar.Reset(reset_invalid,clock_now,error));CHECK(Header(jar,"http://one.com/")=="second=v");const std::vector<Jar::Input> reset_ok{Domain("reset","v","one.com")};CHECK(jar.Reset(reset_ok,clock_now,error));CHECK(Header(jar,"http://one.com/")=="reset=v");
}
void SecureLoopbackPrefixesAndPartitions(){
  Jar jar(std::make_shared<Jar::Budget>());
  for(const std::string host:{"localhost","a.b.localhost","127.0.0.9","[::1]"}){Receive(jar,"http://"+host+"/",{"secure=v; Secure","plain=v"});CHECK(Header(jar,"http://"+host+"/")=="secure=v; plain=v");}
  Receive(jar,"http://nonlocal.example/",{"__Secure-bad=v; Secure","__Host-bad=v; Secure; Path=/","ordinary=v; Secure"});CHECK(Header(jar,"https://nonlocal.example/")=="ordinary=v");
  Receive(jar,"https://prefix.example/",{"__Secure-ok=v; Secure","__Host-ok=v; Secure; Path=/","__Host-domain=v; Secure; Path=/; Domain=prefix.example","__Host-path=v; Secure; Path=/other"});CHECK(Header(jar,"https://prefix.example/")=="__Secure-ok=v; __Host-ok=v");
  auto partition=Domain("part","seed","partition.example");partition.secure=true;partition.partition_key="https://top.example";partition.partition_cross_site=false;
  Add(jar,{partition,URL("host","seed","https://partition.example/")});CHECK(Header(jar,"https://partition.example/")=="host=seed");const auto cookies=Cookies(jar,{"https://partition.example/"});CHECK(cookies.size()==2);CHECK(cookies[0].partition_key=="https://top.example");CHECK(!cookies[0].partition_cross_site);CHECK(cookies[1].secure);
  std::string error;auto wrong=Domain("__Host-x","v","127.0.0.1");wrong.secure=true;const std::vector<Jar::Input> invalid{wrong};CHECK(!jar.Import(invalid,clock_now,error));
  // An IP origin must not scope cookies to a dotted tail that happens to look
  // like a DNS suffix; real PSL validation alone is not an IP policy.
  Receive(jar,"http://127.0.0.1/",{"own=v; Domain=127.0.0.1","tail=v; Domain=0.0.1"});CHECK(Header(jar,"http://127.0.0.1/")=="own=v");
}
void OriginalClockAndCancellation(){
  Jar jar(std::make_shared<Jar::Budget>());Add(jar,{Domain("a","b","one.com")});const auto before=jar.AllocatedBytes();std::string error,header;std::vector<Jar::Cookie> output;
  for(const Jar::Clock& invalid:std::vector<Jar::Clock>{{[]{return 200.0;},kNow,200,false},{[]{return 100.0;},kNow,200,true},{[]{return std::numeric_limits<double>::quiet_NaN();},kNow,200,false},{[]{return 100.0;},kNow,std::numeric_limits<double>::infinity(),false},{[]{return 100.0;},std::numeric_limits<double>::quiet_NaN(),200,false},{[]{return -1.0;},kNow,200,false},{std::function<double()>{},kNow,200,false}}){CHECK(!jar.Clear(invalid,error));CHECK(jar.Size()==1);CHECK(jar.AllocatedBytes()==before);CHECK(!jar.Header("http://one.com/",invalid,header,error));CHECK(header.empty());CHECK(!jar.Cookies({},invalid,output,error));CHECK(output.empty());}
  const Jar::Clock unlimited{[]{return 100.0;},kNow,0,false};CHECK(jar.Clear(unlimited,error));CHECK(jar.Size()==0);CHECK(jar.AllocatedBytes()==0);
  Add(jar,{Domain("kept","v","one.com")});size_t calls=0;const Jar::Clock expires_before_commit{[&]{return ++calls==1?100.0:200.0;},kNow,200,false};const std::vector<Jar::Input> late{Domain("late","v","one.com")};const auto retained=jar.AllocatedBytes();CHECK(!jar.Import(late,expires_before_commit,error));CHECK(jar.Size()==1);CHECK(jar.AllocatedBytes()==retained);CHECK(Header(jar,"http://one.com/")=="kept=v");
}
void ActualAllocationBounds(){
  TrackingResource upstream;auto budget=std::make_shared<Jar::Budget>(24000,&upstream);
  {
    Jar first(budget,Jar::Limits{8,14000,65536}),second(budget,Jar::Limits{8,14000,65536});
    std::string error;auto large=Domain("a",std::string(4000,'v'),"one.com",std::string("/")+std::string(4000,'p'));Add(first,{large});CHECK(first.AllocatedBytes()==budget->Bytes());CHECK(upstream.current==budget->Bytes());
    auto second_large=large;second_large.name="b";const std::vector<Jar::Input> two{second_large};CHECK(second.Import(two,clock_now,error));CHECK(upstream.current==first.AllocatedBytes()+second.AllocatedBytes());
    // A transactional replacement retains the old actual allocations until the
    // new list commits. It cannot borrow uncharged temporary capacity.
    const auto old=Cookies(first);const auto bytes=first.AllocatedBytes();Receive(first,"http://one.com/",{"wrong=v; Domain=other.com","invalid"});CHECK(first.AllocatedBytes()==bytes);CHECK(Cookies(first)[0].value==old[0].value);large.value=std::string(3999,'w');const std::vector<Jar::Input> replacement{large};CHECK(!first.Import(replacement,clock_now,error));CHECK(error.find("allocation budget")!=std::string::npos);CHECK(first.AllocatedBytes()==bytes);CHECK(Cookies(first)[0].value==old[0].value);CHECK(budget->Bytes()==upstream.current);CHECK(budget->Peak()==upstream.peak);CHECK(budget->Peak()<=24000);
    CHECK(first.Clear(clock_now,error));CHECK(second.Clear(clock_now,error));CHECK(upstream.current==0);CHECK(budget->Bytes()==0);
  }
  CHECK(upstream.current==0);
  auto cycles=std::make_shared<Jar::Budget>(65536,&upstream);
  {Jar jar(cycles,Jar::Limits{8,32768,65536});std::string error;for(size_t i=0;i<20000;++i){auto entry=Domain("name",i%2?"small":std::string(4000,'v'),"one.com");const std::vector<Jar::Input> input{entry};CHECK(jar.Import(input,clock_now,error));CHECK(jar.AllocatedBytes()==upstream.current);CHECK(cycles->Bytes()==upstream.current);if(i%2)CHECK(jar.AllocatedBytes()<1024);}CHECK(jar.Clear(clock_now,error));CHECK(jar.AllocatedBytes()==0);CHECK(upstream.current==0);CHECK(cycles->Peak()<=32768);CHECK(upstream.allocations>20000);}
  CHECK(cycles->Bytes()==0);CHECK(upstream.current==0);
}
void EntryAndWireAdmission(){
  Jar jar(std::make_shared<Jar::Budget>(),Jar::Limits{2,65536,8});Add(jar,{Domain("a","b","one.com"),Domain("c","d","one.com")});CHECK(Header(jar,"http://one.com/")=="a=b; c=d");std::string error,output;
  const std::vector<Jar::Input> extra{Domain("e","f","one.com")};CHECK(!jar.Import(extra,clock_now,error));CHECK(error.find("entry capacity")!=std::string::npos);CHECK(jar.Size()==2);
  const std::vector<Jar::Input> bigger{Domain("a","bbb","one.com")};CHECK(jar.Import(bigger,clock_now,error));CHECK(!jar.Header("http://one.com/",clock_now,output,error));CHECK(output.empty());CHECK(jar.Size()==2);
  const std::vector<Jar::Input> invalid{URL("x","v","data:text/plain,test")};CHECK(!jar.Import(invalid,clock_now,error));CHECK(jar.Size()==2);
  for(const int field:{0,1,2,3}){auto entry=Domain("x","v","one.com");if(field==0)entry.domain=std::string(255,'x');else if(field==1)entry.path="/"+std::string(8192,'x');else if(field==2)entry.partition_key=std::string(8193,'x');else{entry.domain.reset();entry.path.reset();entry.url=std::string(8193,'x');}const std::vector<Jar::Input> overflow{entry};const auto before=jar.AllocatedBytes();CHECK(!jar.Import(overflow,clock_now,error));CHECK(error=="Native API cookie field capacity exceeded");CHECK(jar.AllocatedBytes()==before);CHECK(jar.Size()==2);}
}
} // namespace
int main(){CHECK(curl_global_init(CURL_GLOBAL_DEFAULT)==CURLE_OK);ImportedScopeAndOrder();WireScopePSLAndExpiry();ExportImportWireProvenance();ReplacementAndExactSnapshot();SecureLoopbackPrefixesAndPartitions();OriginalClockAndCancellation();ActualAllocationBounds();EntryAndWireAdmission();curl_global_cleanup();std::cout<<"native API cookie jar: 8 groups passed; actual allocator balanced after 20000 replacement cycles\n";}
