// SPDX-License-Identifier: MIT
#include "trace_journal.h"
#include <limits>
namespace suitecut {
namespace {std::string Quote(const std::string& text){std::string output="\"";for(unsigned char c:text){if(c=='\"'||c=='\\'){output+='\\';output+=char(c);}else if(c<32){static const char digits[]="0123456789abcdef";output+="\\u00";output+=digits[c>>4];output+=digits[c&15];}else output+=char(c);}output+='\"';return output;}bool Line(std::string& output,const std::string& json,size_t limit,std::string& error){if(json.empty()||json.size()>1048576||json.size()+1>limit-output.size()){error="Native trace journal payload capacity exceeded";return false;}output+=json;output+='\n';return true;}}
bool TraceJournal::Start(int context,Options options,const std::string& context_event,std::string& error){
  if(context<1||sessions_.contains(context)){error="Tracing has been already started";return false;}
  if(sessions_.size()>=kContexts||Bytes()>kGlobalBytes||context_event.size()+1>kGlobalBytes-Bytes()||generation_==std::numeric_limits<uint64_t>::max()||options.name.size()>512){error="Native trace session capacity exceeded";return false;}
  Session session;session.options=std::move(options);session.generation=++generation_;sessions_.emplace(context,std::move(session));if(!StartChunk(context,context_event,error)){sessions_.erase(context);return false;}return true;
}
bool TraceJournal::StartChunk(int context,const std::string& context_event,std::string& error){
  const auto found=sessions_.find(context);if(found==sessions_.end()){error="Must start tracing before starting a chunk";return false;}auto& session=found->second;
  if(session.chunk==std::numeric_limits<uint64_t>::max()||context_event.empty()||context_event.size()>1048576){error="Native trace chunk identity/payload capacity exceeded";return false;}
  size_t reclaimed=session.trace.size()+session.stack_bytes;for(const auto& [name,bytes]:session.local)reclaimed+=bytes.size();const auto retained=Bytes()-reclaimed;if(retained>kGlobalBytes||context_event.size()+1>kGlobalBytes-retained){error="Native trace global journal capacity exceeded";return false;}
  std::string{}.swap(session.trace);session.local.clear();session.calls.clear();session.groups.clear();session.files.clear();std::vector<std::string>{}.swap(session.stacks);session.stack_bytes=0;session.resource_bytes=0;for(const auto& [name,bytes]:session.inherited)session.resource_bytes+=bytes.size();session.active=true;++session.chunk;return Line(session.trace,context_event,kJournalBytes,error);
}
TraceJournal::Lease TraceJournal::SessionLease(int context) const {const auto found=sessions_.find(context);return found!=sessions_.end()?Lease{context,found->second.generation,found->second.chunk}:Lease{};}
std::vector<TraceJournal::Lease> TraceJournal::Sessions() const{std::vector<Lease> scopes;for(const auto& [context,session]:sessions_)scopes.push_back({context,session.generation,session.chunk});return scopes;}
TraceJournal::Lease TraceJournal::Current(int context) const {const auto found=sessions_.find(context);return found!=sessions_.end()&&found->second.active?Lease{context,found->second.generation,found->second.chunk}:Lease{};}
bool TraceJournal::Current(Lease lease) const {const auto actual=Current(lease.context);return lease.context>0&&actual.session==lease.session&&actual.chunk==lease.chunk;}
TraceJournal::Session* TraceJournal::Find(Lease lease){return Current(lease)?&sessions_.at(lease.context):nullptr;}
const TraceJournal::Options* TraceJournal::Configuration(int context) const {const auto found=sessions_.find(context);return found!=sessions_.end()?&found->second.options:nullptr;}
bool TraceJournal::Event(Lease lease,const std::string& json,std::string& error){const auto session=Find(lease);if(!session)return false;if(Bytes()>kGlobalBytes||json.size()+1>kGlobalBytes-Bytes()){error="Native trace global journal capacity exceeded";return false;}return Line(session->trace,json,kJournalBytes,error);}
std::string TraceJournal::StackJSON(const Session& session){std::vector<std::string> files(session.files.size());for(const auto& [file,index]:session.files)files[index]=file;std::string json="{\"files\":[";for(size_t i=0;i<files.size();++i){if(i)json+=',';json+=Quote(files[i]);}json+="],\"stacks\":[";for(size_t i=0;i<session.stacks.size();++i){if(i)json+=',';json+=session.stacks[i];}return json+"]}";}
bool TraceJournal::Stack(Lease lease,uint64_t call,const std::vector<StackFrame>& frames,std::string& error){
  const auto session=Find(lease);if(!session||!call)return false;
  if(frames.size()>32||session->stacks.size()>=kCalls){error="Native trace stack capacity exceeded";return false;}
  // A failed capture must not consume file IDs. Stage only this stack's new
  // files instead of copying the complete chunk dictionary on every action.
  std::map<std::string,size_t> files;
  std::string stack="["+std::to_string(call)+",[";size_t added=0;
  for(size_t i=0;i<frames.size();++i){
    const auto& frame=frames[i];
    if(frame.file.empty()||frame.file.size()>4096||frame.function.size()>512||frame.file.find('\0')!=std::string::npos||frame.line<0||frame.column<0){error="Invalid native trace call stack";return false;}
    auto file=session->files.find(frame.file);size_t index;
    if(file!=session->files.end())index=file->second;
    else{
      file=files.find(frame.file);
      if(file==files.end()){
        if(session->files.size()+files.size()>=4096){error="Native trace stack file capacity exceeded";return false;}
        added+=frame.file.size()+4;file=files.emplace(frame.file,session->files.size()+files.size()).first;
      }
      index=file->second;
    }
    if(i)stack+=',';
    stack+="["+std::to_string(index)+","+std::to_string(frame.line)+","+std::to_string(frame.column)+","+Quote(frame.function)+"]";
  }
  stack+="]]";added+=stack.size()+1;
  if(added>kJournalBytes-session->stack_bytes||Bytes()>kGlobalBytes||added>kGlobalBytes-Bytes()){error="Native trace stack bytes exceeded";return false;}
  session->stacks.push_back(std::move(stack));session->files.merge(files);session->stack_bytes+=added;return true;
}
bool TraceJournal::Before(Lease lease,uint64_t call,const std::string& json,std::string& error){const auto session=Find(lease);if(!session||!call)return false;if(session->calls.size()>=kCalls||session->calls.contains(call)){error="Native trace action identity/capacity exceeded";return false;}if(!Event(lease,json,error))return false;session->calls.insert(call);return true;}
bool TraceJournal::After(Lease lease,uint64_t call,const std::string& json,std::string& error){const auto session=Find(lease);if(!session||!session->calls.contains(call))return false;if(!Event(lease,json,error))return false;session->calls.erase(call);return true;}
bool TraceJournal::Network(int context,const std::string& json,std::string& error){const auto found=sessions_.find(context);if(found==sessions_.end()||!found->second.options.dom)return false;if(Bytes()>kGlobalBytes||json.size()+1>kGlobalBytes-Bytes()){error="Native trace global journal capacity exceeded";return false;}return Line(found->second.network,json,kNetworkBytes,error);}
bool TraceJournal::WebSocket(Lease lease,const std::string& json,std::string& error){
  const auto session=Find(lease);if(!session||!session->options.dom)return false;
  if(json.empty()||json.size()>4*1048576||json.size()+1>kNetworkBytes-session->websocket.size()||Bytes()>kGlobalBytes||json.size()+1>kGlobalBytes-Bytes()){error="Native trace WebSocket journal byte capacity exceeded";return false;}
  session->websocket+=json;session->websocket+='\n';return true;
}
bool TraceJournal::Resource(int context,std::string name,std::vector<uint8_t> bytes,bool inherited,std::string& error){
  const auto found=sessions_.find(context);if(found==sessions_.end()||(!found->second.active&&!inherited))return false;
  if(!TraceArchive::ValidName(name)||name=="trace.trace"||name=="trace.network"||name=="trace.stacks"){error="Invalid native trace resource name";return false;}auto& session=found->second;auto& entries=inherited?session.inherited:session.local;
  if(const auto existing=entries.find(name);existing!=entries.end()){if(existing->second==bytes)return true;error="Native trace resource identity collision";return false;}
  if(session.inherited.contains(name)||session.local.contains(name)){error="Native trace resource scope identity collision";return false;}
  if(session.inherited.size()+session.local.size()>=TraceArchive::kEntries-3||bytes.size()>TraceArchive::kEntryBytes||bytes.size()>TraceArchive::kBytes-session.resource_bytes||Bytes()>kGlobalBytes||bytes.size()>kGlobalBytes-Bytes()){error="Native trace resource capacity exceeded";return false;}
  session.resource_bytes+=bytes.size();entries.emplace(std::move(name),std::move(bytes));return true;
}
bool TraceJournal::Group(Lease lease,uint64_t call,const std::string& before,std::string& error){const auto session=Find(lease);if(!session)return true;if(session->groups.size()>=128){error="Native trace group capacity exceeded";return false;}if(!Before(lease,call,before,error))return false;session->groups.push_back(call);return true;}
bool TraceJournal::GroupEnd(Lease lease,const std::string& after,std::string& error){const auto session=Find(lease);if(!session||session->groups.empty())return true;const auto call=session->groups.back();if(!After(lease,call,after,error))return false;session->groups.pop_back();return true;}
std::vector<uint64_t> TraceJournal::Groups(Lease lease) const {const auto found=sessions_.find(lease.context);return Current(lease)&&found!=sessions_.end()?found->second.groups:std::vector<uint64_t>{};}
size_t TraceJournal::SnapshotBytes(Lease lease) const {const auto found=sessions_.find(lease.context);if(!Current(lease)||found==sessions_.end())return 0;const auto& session=found->second;return session.trace.size()+session.network.size()+session.websocket.size()+session.resource_bytes+StackJSON(session).size();}
bool TraceJournal::Snapshot(Lease lease,TraceArchive& archive,std::string& error) const {const auto found=sessions_.find(lease.context);if(!Current(lease)||found==sessions_.end()){error="Native trace snapshot chunk is unavailable";return false;}const auto& session=found->second;
TraceArchive candidate;if(!session.websocket.empty()&&!candidate.AddText("resources/websocket-"+std::to_string(session.generation)+".jsonl",session.websocket,error))return false;if(!candidate.AddText("trace.trace",session.trace,error)||!candidate.AddText("trace.network",session.network,error)||!candidate.AddText("trace.stacks",StackJSON(session),error))return false;for(const auto& [name,bytes]:session.inherited)if(!candidate.Add(name,bytes,error))return false;for(const auto& [name,bytes]:session.local)if(!candidate.Add(name,bytes,error))return false;archive=std::move(candidate);return true;
}
bool TraceJournal::StopChunk(int context,TraceArchive& archive,bool export_artifact,std::string& error){
  const auto found=sessions_.find(context);if(found==sessions_.end()||!found->second.active){if(export_artifact){error="Must start tracing before stopping";return false;}return true;}auto& session=found->second;
  if(export_artifact&&!Snapshot(Current(context),archive,error))return false;
  session.active=false;std::string{}.swap(session.trace);session.local.clear();session.calls.clear();session.groups.clear();session.files.clear();std::vector<std::string>{}.swap(session.stacks);session.stack_bytes=0;session.resource_bytes=0;for(const auto& [name,bytes]:session.inherited)session.resource_bytes+=bytes.size();return true;
}
bool TraceJournal::Stop(int context,TraceArchive& archive,bool export_artifact,std::string& error){if(!StopChunk(context,archive,export_artifact,error))return false;sessions_.erase(context);return true;}
void TraceJournal::Closed(int context){sessions_.erase(context);}
size_t TraceJournal::Bytes() const {size_t total=0;for(const auto& [context,session]:sessions_)total+=session.trace.size()+session.network.size()+session.websocket.size()+session.resource_bytes+session.stack_bytes;return total;}
} // namespace suitecut
