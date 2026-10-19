// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "context_video.h"
#include "context_video_files.h"
#include "context_video_policy.h"
#include "include/cef_api_hash.h"
#include <cassert>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
using suitecut::NativeContextVideo;
using Dict=CefRefPtr<CefDictionaryValue>;
namespace fs=std::filesystem;
namespace {
Dict Options(const fs::path&dir){auto d=CefDictionaryValue::Create();d->SetString("dir",dir.string());return d;}
Dict Action(const std::string&id,const std::string&action,const fs::path&path={}){auto d=CefDictionaryValue::Create();d->SetString("videoId",id);d->SetString("action",action);if(!path.empty())d->SetString("path",path.string());return d;}
std::string Read(const fs::path&p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
void Write(const fs::path&p,const std::string&bytes){std::ofstream f(p,std::ios::binary);f<<bytes;f.close();assert(f);}
struct Result{int calls=0;bool ok=false;std::string path,error;NativeContextVideo::Reply Callback(){return [this](bool yes,const std::string&p,const std::string&e){++calls;ok=yes;path=p;error=e;};}};
struct Fixture{
  double now=100;std::deque<std::function<void()>>jobs,owners;std::vector<suitecut::MediaPipelineOptions>started;std::shared_ptr<NativeContextVideo>video;int next=0,settled=0;
  explicit Fixture(){NativeContextVideo::Hooks h;h.now=[this]{return now;};h.background=[this](std::function<void()>job){jobs.push_back(std::move(job));return true;};h.owner=[this](std::function<void()>job){owners.push_back(std::move(job));return true;};h.start=[this](int page,int context,suitecut::MediaPipelineOptions o){assert(page>0&&context>0);started.push_back(std::move(o));return "recording-"+std::to_string(++next);};h.settled=[this](int){++settled;};video=std::make_shared<NativeContextVideo>(std::move(h));}
  ~Fixture(){video->Clear();Drain();video.reset();}
  void Owners(){while(!owners.empty()){auto f=std::move(owners.front());owners.pop_front();f();}}
  void One(){assert(!jobs.empty());{auto f=std::move(jobs.front());jobs.pop_front();f();}Owners();}
  void Drain(){for(int turn=0;!jobs.empty()||!owners.empty();++turn){assert(turn<2048);if(!jobs.empty())One();else Owners();}}
  NativeContextVideo::Prepared Prepare(const fs::path&dir,int owner=1){NativeContextVideo::Prepared p;bool called=false;assert(video->Prepare(owner,1,Options(dir),0,[&](auto v,const std::string&e){assert(e.empty());called=true;p=std::move(v);}));assert(!called&&video->Physical()==1);Drain();assert(called&&p&&!video->Physical());return p;}
  void Configure(const fs::path&dir,int context=1){auto p=Prepare(dir);std::string error;assert(video->Configure(context,p,error));}
  std::string Page(int page=1,int context=1,std::optional<suitecut::NativeContextVideoSize>size={}){(void)size;suitecut::MediaPipelineOptions o;o.launcher_path="/usr/bin/true";o.source_width=640;o.source_height=480;o.output_width=320;o.output_height=240;o.start_timestamp_ms=now;o.first_frame_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);std::optional<NativeContextVideo::Descriptor>d;std::string error;assert(video->PageCreated(page,context,o,d,error));assert(d);return d->id;}
  void Complete(int page,const std::string&id,const std::string&bytes="real-file-bytes"){const auto&option=started.at(static_cast<size_t>(page-1));Write(option.output_path,bytes);suitecut::MediaPipelineResult r;r.output_path=option.output_path;r.output_bytes=bytes.size();video->Observe(page,id,"complete",r);}
};
void DirectoryReplacement(const fs::path&root){
  fs::path replacement;
  {Fixture f;f.Configure(root/"directory");const auto id=f.Page();const fs::path path=f.started[0].output_path;replacement=path.parent_path();const fs::path moved=replacement.string()+".old";fs::rename(replacement,moved);fs::create_directory(replacement);Write(moved/path.filename(),"original-owned");suitecut::MediaPipelineResult r;r.output_path=path;r.output_bytes=14;f.video->Observe(1,id,"complete",r);f.Drain();assert(!f.video->Error(1).empty());}
  assert(fs::is_directory(replacement)&&fs::is_empty(replacement));
}
}
int main(int argc,char**argv){
  cef_api_hash(CEF_API_VERSION,0);assert(argc==2||argc==3);const fs::path root=argv[1];fs::create_directories(root);
  if(argc==3){assert(std::string(argv[2])=="directory-ownership");DirectoryReplacement(root);std::cout<<"ContextVideo affected directory ownership control PASS (no browser acceptance)\n";return 0;}
  unsigned controls=0;
  // Disabled context performs no preparation, producer admission or file job.
  {Fixture f;std::optional<NativeContextVideo::Descriptor>d;std::string error;assert(f.video->PageCreated(1,1,{},d,error)&&!d&&f.started.empty()&&f.jobs.empty());++controls;}
  // Every malformed/admission rejection delivers its supplied callback once.
  {Fixture f;for(const auto&options:{Dict{},CefDictionaryValue::Create()}){int called=0;assert(!f.video->Prepare(1,1,options,0,[&](auto p,const std::string&e){assert(!p&&!e.empty());++called;}));assert(called==1);}auto bad=Options(root);bad->SetString("dir",CefString(std::u16string{u'/',u'a',char16_t(0),u'b'}));int called=0;assert(!f.video->Prepare(1,1,bad,0,[&](auto,const std::string&){++called;}));assert(called==1&&!f.video->Physical());++controls;}
  // Logical cancel cannot retire the queued job, nor its executed retained body.
  {Fixture f;int called=0;assert(f.video->Prepare(1,1,Options(root/"cancel"),0,[&](auto,const std::string&){++called;}));f.video->Cancel(1,1);auto held=std::move(f.jobs.front());f.jobs.pop_front();held();f.video->Tick();assert(f.video->Physical()==1&&!called);held={};f.Owners();assert(!f.video->Physical()&&!called);++controls;}
  // A returned file job cannot publish preparation after its original deadline.
  {Fixture f;bool failed=false;assert(f.video->Prepare(1,1,Options(root/"late"),150,[&](auto p,const std::string&e){failed=!p&&!e.empty();}));{auto held=std::move(f.jobs.front());f.jobs.pop_front();held();}f.now=151;f.Owners();assert(failed&&!f.video->Physical());++controls;}
  // Native one-shot preparations cannot be replayed or cross controller owners.
  {Fixture f,other;auto p=f.Prepare(root/"owner");std::string e;assert(!other.video->Configure(1,p,e));assert(f.video->Configure(1,p,e));assert(!f.video->Configure(2,p,e));++controls;}
  // Default fit uses actual CSS viewport, independently of the DPR capture canvas.
  {Fixture f;f.Configure(root/"css");const auto id=f.Page();assert(!id.empty());const auto&o=f.started.back();assert(o.source_width==320&&o.source_height==240&&o.output_width==320&&o.output_height==240&&o.top_left_padding&&!o.audio&&o.dynamic_viewport&&!o.i420&&!o.max_content_width&&!o.max_content_height);++controls;}
  // Explicit larger canvases cannot upscale a genuine smaller CSS viewport.
  {Fixture f;auto options=Options(root/"explicit-canvas");auto size=CefDictionaryValue::Create();size->SetInt("width",800);size->SetInt("height",800);options->SetDictionary("size",size);NativeContextVideo::Prepared p;assert(f.video->Prepare(1,1,options,0,[&](auto value,const std::string&e){assert(e.empty());p=value;}));f.Drain();std::string e;assert(f.video->Configure(1,p,e));f.Page();const auto&o=f.started.back();assert(o.output_width==800&&o.output_height==800&&o.dynamic_viewport&&!o.i420&&!o.max_content_width&&!o.max_content_height);++controls;}
  // Public logical path waits do not block context close; real sealing jobs do.
  {Fixture f;f.Configure(root/"wait");const auto id=f.Page();Result r;f.video->Handle(20,1,1,Action(id,"path"),0,r.Callback());assert(!r.calls&&f.video->ContextReady(1)&&!f.video->Physical());f.Complete(1,id);assert(!f.video->ContextReady(1)&&f.video->Physical()==1&&!r.calls);f.Drain();assert(r.calls==1&&r.ok&&Read(r.path)=="real-file-bytes"&&f.video->ContextReady(1));f.video->Removed(1);assert(f.video->Metadata(1)->id==id);++controls;}
  // Native per-video FIFO orders copy before delete, with actual parent creation.
  {Fixture f;f.Configure(root/"fifo");const auto id=f.Page();Result copy,del,path,again;const auto target=root/"created/parents/saved.webm";f.video->Handle(20,1,1,Action(id,"saveAs",target),0,copy.Callback());f.video->Handle(21,1,1,Action(id,"delete"),0,del.Callback());f.video->Handle(22,1,1,Action(id,"path"),0,path.Callback());f.Complete(1,id,"owned-fifo");f.Drain();assert(copy.ok&&del.ok&&Read(target)=="owned-fifo"&&!path.ok&&path.calls==1&&!fs::exists(f.started[0].output_path));f.video->Handle(23,1,1,Action(id,"delete"),0,again.Callback());assert(again.ok);++controls;}
  // Copy cancellation drops publication, retains real job/captures until return.
  {Fixture f;f.Configure(root/"copy-cancel");const auto id=f.Page();f.Complete(1,id);f.Drain();Result r;const auto target=root/"copy-canceled.webm";f.video->Handle(20,1,1,Action(id,"saveAs",target),0,r.Callback());auto held=std::move(f.jobs.front());f.jobs.pop_front();held();f.video->Cancel(20,1);f.video->Tick();assert(f.video->Physical()==1&&!r.calls&&fs::exists(target));held={};f.Owners();assert(!f.video->Physical()&&!r.calls);++controls;}
  // Successful copy side effects are not reported as success after a delayed owner.
  {Fixture f;f.Configure(root/"late-copy");const auto id=f.Page();f.Complete(1,id);f.Drain();Result r;const auto target=root/"late-copy.webm";f.video->Handle(20,1,1,Action(id,"saveAs",target),150,r.Callback());{auto held=std::move(f.jobs.front());f.jobs.pop_front();held();}f.now=151;f.Owners();assert(r.calls==1&&!r.ok&&fs::exists(target));++controls;}
  // Exact native context is mandatory, even though the page may already be gone.
  {Fixture f;f.Configure(root/"context");const auto id=f.Page();Result r;f.video->Handle(20,1,2,Action(id,"path"),0,r.Callback());assert(r.calls==1&&!r.ok&&!f.video->Physical());++controls;}
  // No page/video publication or backend start after the actual creation cap.
  {Fixture f;f.Configure(root/"expired");suitecut::MediaPipelineOptions o;o.first_frame_deadline=std::chrono::steady_clock::now()-std::chrono::seconds(1);std::optional<NativeContextVideo::Descriptor>d;std::string error;assert(!f.video->PageCreated(1,1,o,d,error)&&!d&&f.started.empty()&&!f.video->Error(1).empty());++controls;}
  // Preserve first actual native terminal cause, not ID/order-derived replacement.
  {Fixture f;f.Configure(root/"fail");auto id=f.Page();suitecut::MediaPipelineResult r;r.output_path=f.started[0].output_path;r.failure="actual encoder failed";f.video->Observe(1,id,"failed",r);Result out;f.video->Handle(20,1,1,Action(id,"path"),0,out.Callback());assert(out.calls==1&&!out.ok&&f.video->Error(1)==r.failure);std::optional<NativeContextVideo::Descriptor>d;std::string e;assert(!f.video->PageCreated(1,1,{},d,e));assert(f.video->Error(1)==r.failure);++controls;}
  // Encoder terminal byte identity must describe a real regular owned output file.
  {Fixture f;f.Configure(root/"wrong-bytes");const auto id=f.Page();Write(f.started[0].output_path,"three");suitecut::MediaPipelineResult r;r.output_path=f.started[0].output_path;r.output_bytes=99;f.video->Observe(1,id,"complete",r);f.Drain();assert(!f.video->Error(1).empty());++controls;}
  // FD copy never reads a replacement pathname; path/delete reject foreign inode.
  {Fixture f;f.Configure(root/"replace");const auto id=f.Page();f.Complete(1,id,"original-retained");f.Drain();const fs::path path=f.started[0].output_path;fs::rename(path,path.string()+".old");Write(path,"foreign");Result get,copy,del;f.video->Handle(20,1,1,Action(id,"path"),0,get.Callback());f.video->Handle(21,1,1,Action(id,"saveAs",root/"retained.webm"),0,copy.Callback());f.video->Handle(22,1,1,Action(id,"delete"),0,del.Callback());f.Drain();assert(!get.ok&&copy.ok&&!del.ok&&Read(root/"retained.webm")=="original-retained"&&Read(path)=="foreign");++controls;}
  // Clear cancels logical waits while retaining final file reconciliation.
  {Fixture f;f.Configure(root/"clear");const auto id=f.Page();Result r;f.video->Handle(20,1,1,Action(id,"path"),0,r.Callback());f.video->Clear();assert(r.calls==1&&!r.ok);f.Complete(1,id);assert(f.video->Physical());f.Drain();assert(!f.video->Physical()&&f.video->ContextReady(1));++controls;}
  // Unique retained canonical directories prevent previous-run artifact overwrite.
  {Fixture a,b;a.Configure(root/"same-dir");b.Configure(root/"same-dir");a.Page();b.Page();assert(a.started[0].output_path!=b.started[0].output_path);++controls;}
  // Retained directory replacement cannot redirect sealing or artifact operations.
  {DirectoryReplacement(root);++controls;}
  // Undeleted identities are never evicted; explicit delete reclaims capacity.
  {Fixture f;f.Configure(root/"capacity");std::string first;for(int page=1;page<=256;++page){auto id=f.Page(page);if(page==1)first=id;}suitecut::MediaPipelineOptions o;std::optional<NativeContextVideo::Descriptor>d;std::string e;assert(!f.video->PageCreated(257,1,o,d,e)&&f.started.size()==256);f.Complete(1,first);f.Drain();Result del;f.video->Handle(20,1,1,Action(first,"delete"),0,del.Callback());f.Drain();assert(del.ok);assert(!f.Page(257).empty()&&f.started.size()==257);Result again,path;f.video->Handle(21,1,1,Action(first,"delete"),0,again.Callback());f.video->Handle(22,1,1,Action(first,"path"),0,path.Callback());assert(again.ok&&!path.ok&&f.video->Metadata(2));++controls;}
  // Pure policy validates explicit codec bounds and default down-fit.
  {assert((suitecut::ContextVideoCanvas(1600,900,{})==suitecut::NativeContextVideoSize{800,450}));assert((suitecut::ContextVideoCanvas(320,240,suitecut::NativeContextVideoSize{800,800})==suitecut::NativeContextVideoSize{800,800}));for(const auto size:{suitecut::NativeContextVideoSize{0,20},{4000,20},{20,2162}}){bool rejected=false;try{(void)suitecut::ContextVideoCanvas(320,240,size);}catch(const std::exception&){rejected=true;}assert(rejected);}++controls;}
  std::cout<<"ContextVideo "<<controls<<" native controller/file/policy controls PASS (backend lifecycle controlled; no browser acceptance)\n";
}
