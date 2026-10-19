// SPDX-License-Identifier: MIT
#pragma once
#include <atomic>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>
namespace suitecut {
// Read-only positions, never a snapshot of source bytes or mutation authority.
// The owner pins the actual debugger lease and exact current root document.
class DeveloperDebuggerScripts {
 public:
  struct Page {uint64_t revision=0;std::string json="[]",last;size_t count=0;bool more=false;};
  struct Hooks {
    std::string session;
    std::function<double()> now;
    std::function<bool()> current;
    std::function<bool(const std::string&,const uint64_t*,double,Page&,std::string&)> page;
  };
  explicit DeveloperDebuggerScripts(Hooks hooks):hooks_(std::move(hooks)){}
  ~DeveloperDebuggerScripts(){Clear();}
  DeveloperDebuggerScripts(const DeveloperDebuggerScripts&)=delete;
  DeveloperDebuggerScripts&operator=(const DeveloperDebuggerScripts&)=delete;
  void Clear(){positions_-=cursors_.size();cursors_.clear();active_=false;}
  size_t Retained()const{return cursors_.size();}
  static size_t ProcessRetained(){return positions_.load();}
  bool Read(const std::string&cursor,double deadline,std::string&json,std::string&error){
    json.clear();
    if(!std::isfinite(deadline)||deadline<0||Expired(deadline)){error="Native debugger original deadline expired or invalid before script enumeration";return false;}
    if(!hooks_.current()){Clear();error="Native debugger script document is no longer current";return false;}
    std::string after;const uint64_t*expected=nullptr;
    if(cursor.empty()){
      Clear();
      if(enumeration_==UINT64_MAX){error="Native debugger script enumeration identity exhausted";return false;}
      ++enumeration_;active_=true;
    }else{
      const auto position=cursors_.find(cursor);
      if(!active_||position==cursors_.end()){error="Native debugger script cursor is stale or belongs to another enumeration/session";return false;}
      after=position->second.after;expected=&revision_;
    }
    const auto enumeration=enumeration_;
    Page page;
    if(!hooks_.page(after,expected,deadline,page,error)){Clear();return false;}
    if(!active_||enumeration!=enumeration_||!hooks_.current()){
      Clear();error="Native debugger script document/enumeration changed before publication";return false;
    }
    if(Expired(deadline)){error="Native debugger original deadline expired before script page publication";return false;}
    if(page.count>64||page.json.size()>512*1024||page.json.size()<2||page.json.front()!='['||page.json.back()!=']'||
       (page.count&&(page.last.empty()||page.last.size()>256||page.last<=after))||(page.more&&!page.count)||
       (expected&&page.revision!=revision_)){
      Clear();error="Actual native debugger script page identity/bounds failed";return false;
    }
    if(!expected)revision_=page.revision;
    std::string next;
    Position*previous=cursor.empty()?nullptr:&cursors_.at(cursor);
    if(page.more&&previous)next=previous->next;
    const bool create=page.more&&next.empty();
    std::map<std::string,Position>staged;
    std::string successor;
    if(create){
      if(position_==UINT64_MAX){error="Native debugger script cursor identity exhausted";return false;}
      next=hooks_.session+":scripts:"+std::to_string(enumeration_)+":"+std::to_string(position_+1);
      // All throwing allocations precede global quota admission and all
      // mutations of the existing cursor graph, including its parent edge.
      staged.emplace(next,Position{page.last,{}});
      if(previous)successor=next;
    }
    std::string prepared="{\"scripts\":"+page.json+(next.empty()?"":",\"next\":"+Quote(next))+"}";
    if(prepared.size()>512*1024){error="Native debugger script page exceeds total byte bound";return false;}
    if(Expired(deadline)){error="Native debugger original deadline expired during script page serialization";return false;}
    if(create){
      Reservation reservation;
      if(!reservation.Admit()){error="Native debugger script cursor process capacity exceeded";return false;}
      // Extract/insert uses the same allocator and a non-throwing string
      // comparison. No allocation remains after the staged node is accepted.
      auto inserted=cursors_.insert(staged.extract(staged.begin()));
      if(!inserted.inserted){error="Native debugger script cursor identity collision";return false;}
      if(previous)previous->next.swap(successor);
      ++position_;reservation.committed=true;
    }
    json.swap(prepared);
    return true;
  }
 private:
  static std::string Quote(const std::string&text){
    constexpr char hex[]="0123456789abcdef";std::string out="\"";
    for(unsigned char c:text){if(c=='"'||c=='\\'){out+='\\';out+=char(c);}else if(c<32){out+="\\u00";out+=hex[c>>4];out+=hex[c&15];}else out+=char(c);}
    return out+'"';
  }
  struct Reservation{
    bool admitted=false,committed=false;
    bool Admit(){size_t count=positions_.load();while(count<256&&!positions_.compare_exchange_weak(count,count+1)){}admitted=count<256;return admitted;}
    ~Reservation(){if(admitted&&!committed)--positions_;}
  };
  struct Position{std::string after,next;};
  inline static std::atomic<size_t>positions_{0};
  Hooks hooks_;
  std::map<std::string,Position>cursors_;
  uint64_t enumeration_=0,position_=0,revision_=0;
  bool active_=false;
  bool Expired(double deadline)const{return deadline>0&&hooks_.now()>=deadline;}
};
}
