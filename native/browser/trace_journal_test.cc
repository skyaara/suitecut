// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <fstream>
#include "trace_journal.h"
int main(int argc,char** argv){
  using Journal=suitecut::TraceJournal;Journal journal;std::string error;suitecut::TraceArchive archive;
  assert(journal.Group({},1,"ignored",error));assert(journal.GroupEnd({},"ignored",error));
  assert(!journal.Stop(1,archive,true,error));assert(journal.Stop(1,archive,false,error));
  Journal::Options options;options.dom=true;assert(journal.Start(1,options,"{\"type\":\"context-options\"}",error));assert(!journal.Start(1,options,"{}",error));
  const auto first=journal.Current(1);assert(first.context==1&&journal.Current(first));
  assert(journal.Before(first,9,"{\"type\":\"before\",\"callId\":9}",error));assert(!journal.After(first,10,"dangling",error));
  assert(journal.Stack(first,9,{{"/actual/file\"name.cpp","actual function",12,4}},error));
  assert(journal.Network(1,"{\"type\":\"resource-snapshot\"}",error));assert(journal.Resource(1,"resources/style.css",{1,2,3},true,error));assert(journal.Resource(1,"resources/style.css",{1,2,3},true,error));assert(!journal.Resource(1,"resources/style.css",{9},true,error));
  assert(journal.Resource(1,"resources/chunk-1.jpg",{4,5},false,error));assert(journal.Resource(1,"src/actual.cpp",{6,7},false,error));assert(journal.StopChunk(1,archive,true,error));assert(!journal.Current(first));assert(archive.Count()==6);
  if(argc==2){std::vector<uint8_t> first_bytes;assert(archive.Encode(first_bytes,error));std::ofstream first_file(std::string(argv[1])+".first.zip",std::ios::binary);first_file.write(reinterpret_cast<const char*>(first_bytes.data()),first_bytes.size());assert(first_file.good());}
  assert(!journal.Event(first,"late canceled capture",error));assert(journal.StartChunk(1,"{\"type\":\"context-options\"}",error));const auto second=journal.Current(1);assert(second.chunk!=first.chunk&&second.session==first.session);
  assert(!journal.After(first,9,"late former chunk completion",error));assert(!journal.After(second,9,"after without its before",error));
  assert(journal.Group(second,11,"{\"type\":\"before\",\"method\":\"tracingGroup\",\"callId\":11}",error));assert(journal.Groups(second)==std::vector<uint64_t>{11});assert(journal.GroupEnd(second,"{\"type\":\"after\",\"callId\":11}",error));assert(journal.Groups(second).empty());
  assert(journal.Stop(1,archive,true,error));assert(archive.Count()==4&&!journal.Contexts()&&!journal.Bytes());
  std::vector<uint8_t> bytes;assert(archive.Encode(bytes,error));if(argc==2){std::ofstream file(argv[1],std::ios::binary);file.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());assert(file.good());}
  assert(journal.Start(1,{},"{}",error));assert(journal.Current(1).session!=first.session);assert(!journal.Event(first,"old reused context",error));assert(!journal.Network(1,"unexpected default snapshot",error));
  {Journal sockets;assert(sockets.Start(9,options,"{}",error));const auto held=sockets.Current(9);assert(sockets.WebSocket(held,"{\"event\":\"created\"}",error));assert(sockets.WebSocket(held,"{\"event\":\"framereceived\",\"payload\":\"AAE=\"}",error));suitecut::TraceArchive one,two;assert(sockets.StopChunk(9,one,true,error));assert(!sockets.WebSocket(held,"{\"late\":true}",error));assert(sockets.StartChunk(9,"{}",error));assert(!sockets.WebSocket(held,"{\"oldChunk\":true}",error));assert(sockets.Stop(9,two,true,error));std::vector<uint8_t>a,b;assert(one.Encode(a,error)&&two.Encode(b,error));assert(one.Count()==4&&two.Count()==4);assert(std::string(a.begin(),a.end()).find("framereceived")!=std::string::npos);assert(sockets.Start(9,options,"{}",error));assert(!sockets.WebSocket(held,"{\"oldSession\":true}",error));assert(sockets.Stop(9,two,true,error)&&two.Count()==3);}
  assert(journal.Start(2,{},"{}",error));const auto independent=journal.Current(2);journal.Closed(1);assert(journal.Current(independent));journal.Closed(2);assert(!journal.Contexts()&&!journal.Bytes());
  {
    Journal stacks;assert(stacks.Start(1,{},"{}",error));const auto lease=stacks.Current(1);
    // Reject after staging a new filename without consuming its file index.
    const auto before=stacks.Bytes();
    assert(!stacks.Stack(lease,1,{{"/rejected","first",0,0},{"","invalid",0,0}},error));
    assert(stacks.Bytes()==before);
    assert(stacks.Stack(lease,2,{{"/z","z",1,2},{"/a","a",3,4},{"/z","again",5,6}},error));
    assert(stacks.Stack(lease,3,{{"/a","existing",7,8},{"/new","new",9,10}},error));
    suitecut::TraceArchive snapshot;assert(stacks.Snapshot(lease,snapshot,error));std::vector<uint8_t> zip;
    assert(snapshot.Encode(zip,error));const std::string text(zip.begin(),zip.end());
    assert(text.find("{\"files\":[\"/z\",\"/a\",\"/new\"],\"stacks\":[[2,[[0,1,2,\"z\"],[1,3,4,\"a\"],[0,5,6,\"again\"]]],[3,[[1,7,8,\"existing\"],[2,9,10,\"new\"]]]]}")!=std::string::npos);
    assert(text.find("/rejected")==std::string::npos);
    std::vector<Journal::StackFrame> frames;
    for(size_t first=3;first<4095;first+=32){frames.clear();for(size_t i=first;i<4095&&i<first+32;++i)frames.push_back({"/file-"+std::to_string(i),"",0,0});assert(stacks.Stack(lease,first+10,frames,error));}
    const auto at_limit=stacks.Bytes();
    assert(!stacks.Stack(lease,5000,{{"/last","",0,0},{"/too-many","",0,0}},error));
    assert(stacks.Bytes()==at_limit);
    assert(stacks.Stack(lease,5001,{{"/last","",0,0},{"/last","",0,0}},error));
    assert(!stacks.Stack(lease,5002,{{"/too-many","",0,0}},error));
    assert(stacks.Stack(lease,5003,{{"/a","still valid",0,0}},error));
  }
}
