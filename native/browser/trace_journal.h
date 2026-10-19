// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "trace_archive.h"

namespace suitecut {
// Context and chunk ownership is native. Captures are supplied only by actual
// Source/HTTP/renderer callbacks; the journal never invents a completed action.
class TraceJournal {
 public:
  struct Options {bool screenshots=false,dom=false,action_png=false,aria=false,sources=false;std::string name;};
  struct StackFrame {std::string file,function;int line=0,column=0;};
  struct Lease {int context=0;uint64_t session=0,chunk=0;};
  static constexpr size_t kContexts=128,kJournalBytes=16*1024*1024,kNetworkBytes=16*1024*1024,kCalls=4096,kGlobalBytes=256*1024*1024;
  bool Start(int context,Options options,const std::string& context_event,std::string& error);
  bool StartChunk(int context,const std::string& context_event,std::string& error);
  bool Snapshot(Lease lease,TraceArchive& archive,std::string& error) const;
  size_t SnapshotBytes(Lease lease) const;
  bool StopChunk(int context,TraceArchive& archive,bool export_artifact,std::string& error);
  bool Stop(int context,TraceArchive& archive,bool export_artifact,std::string& error);
  void Closed(int context);
  void Clear(){sessions_.clear();}
  Lease Current(int context) const;
  Lease SessionLease(int context) const;
  std::vector<Lease> Sessions() const;
  bool Current(Lease lease) const;
  const Options* Configuration(int context) const;
  bool Event(Lease lease,const std::string& json,std::string& error);
  bool Stack(Lease lease,uint64_t call,const std::vector<StackFrame>& frames,std::string& error);
  bool Before(Lease lease,uint64_t call,const std::string& json,std::string& error);
  bool After(Lease lease,uint64_t call,const std::string& json,std::string& error);
  bool Network(int context,const std::string& json,std::string& error);
  bool WebSocket(Lease lease,const std::string& json,std::string& error);
  bool Resource(int context,std::string name,std::vector<uint8_t> bytes,bool inherited,std::string& error);
  bool Group(Lease lease,uint64_t call,const std::string& before,std::string& error);
  bool GroupEnd(Lease lease,const std::string& after,std::string& error);
  std::vector<uint64_t> Groups(Lease lease) const;
  size_t Contexts() const{return sessions_.size();}
  size_t Bytes() const;
 private:
  struct Session {Options options;uint64_t generation=0,chunk=0;bool active=false;std::string trace,network,websocket;std::map<std::string,size_t> files;std::vector<std::string> stacks;size_t stack_bytes=0;std::set<uint64_t> calls;std::vector<uint64_t> groups;std::map<std::string,std::vector<uint8_t>> inherited,local;size_t resource_bytes=0;};
  Session* Find(Lease lease);
  static std::string StackJSON(const Session& session);
  std::map<int,Session> sessions_;uint64_t generation_=0;
};
} // namespace suitecut
