// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <fstream>
#include <iostream>
#include "trace_raw_files.h"
namespace {
std::string Read(const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);assert(file);return {std::istreambuf_iterator<char>(file),{}};}
}
int main(){
  using suitecut::TraceRawFiles;using suitecut::TraceArchive;std::string error;std::atomic_bool canceled=false;
  const auto directory=std::filesystem::temp_directory_path()/("suitecut-trace-raw-test-"+std::to_string(getpid()));std::filesystem::remove_all(directory);
  assert(!TraceRawFiles::ValidName("../escape")&&!TraceRawFiles::ValidName("/")&&!TraceRawFiles::ValidName("..")&&!TraceRawFiles::ValidName(std::string(201,'x')));
  TraceArchive archive;
  const std::string dom="{\"type\":\"frame-snapshot\",\"snapshot\":{\"html\":[\"HTML\",{\"\\ud800\":\"\\udfff\",\"__suitecutNativeUTF16Scalar\":\"literal\"},\"\\ud800\"],\"file\":\"resources/actual.png\"}}\n";
  assert(archive.AddText("trace.trace","{\"type\":\"context-options\"}\n"+dom+"{\"type\":\"screenshot\",\"file\":\"screenshots/call@1-before.png\"}\n{\"file\":\"aria/call@1-before.json\",\"type\":\"aria-snapshot\"}\n{\"type\":\"screencast-frame\",\"file\":\"resources/actual.jpeg\"}\n",error));
  assert(archive.AddText("trace.network","{\"type\":\"resource-snapshot\",\"snapshot\":{\"request\":{\"postData\":{\"_file\":\"resources/actual-post.dat\",\"text\":\"actual\"}},\"response\":{\"content\":{\"_file\":\"resources/actual.css\",\"_sha1\":\"actual.css\"}}}}\n",error));
  assert(archive.AddText("trace.stacks","{\"files\":[\"/actual/caller.ts\"],\"stacks\":[]}",error));
  for(const char* name:{"screenshots/call@1-before.png","aria/call@1-before.json","resources/actual.jpeg","resources/actual.css","resources/actual-post.dat","resources/websocket-1.jsonl","src/actual.ts"})assert(archive.Add(name,{0,1,0xff,0},error));
  std::vector<uint8_t> original,after;assert(archive.Encode(original,error));
  {TraceRawFiles writer;assert(writer.Prepare(archive,directory.string(),"first",canceled,error));assert(!std::filesystem::exists(directory/"first.trace"));{TraceRawFiles concurrent;assert(!concurrent.Prepare(archive,directory.string(),"first",canceled,error));assert(error.find("already being written")!=std::string::npos);}assert(writer.Commit(error));}
  assert(archive.Encode(after,error)&&after==original);
  const auto trace=Read(directory/"first.trace"),network=Read(directory/"first.network");assert(trace.find(dom)!=std::string::npos);assert(trace.find("__suitecutNativeUTF16Scalar")!=std::string::npos);assert(network.find("\"_sha1\":\"actual.css\"")!=std::string::npos);assert(Read(directory/"first.stacks")=="{\"files\":[\"/actual/caller.ts\"],\"stacks\":[]}");
  std::string prefix;for(const auto& entry:std::filesystem::directory_iterator(directory))if(entry.is_directory())prefix=entry.path().filename();assert(prefix.starts_with("trace-resources-"));
  assert(trace.find("\"file\":\""+prefix+"/screenshots/call@1-before.png\"")!=std::string::npos);assert(trace.find("\"file\":\""+prefix+"/aria/call@1-before.json\"")!=std::string::npos);assert(network.find("\"_file\":\""+prefix+"/resources/actual.css\"")!=std::string::npos);
  assert(Read(directory/prefix/"resources/actual.css")==std::string("\0\1\xff\0",4));assert(network.find("\"_file\":\""+prefix+"/resources/actual-post.dat\"")!=std::string::npos);assert(Read(directory/prefix/"resources/actual-post.dat")==std::string("\0\1\xff\0",4));
  // A second export keeps first-chunk resources immutable even if native action
  // IDs or WebSocket journal names are deliberately reused in another session.
  {TraceRawFiles writer;assert(writer.Prepare(archive,directory.string(),"second",canceled,error));assert(writer.Commit(error));}
  assert(Read(directory/"first.trace")==trace&&Read(directory/"second.trace")!=trace);assert(Read(directory/prefix/"resources/websocket-1.jsonl")==std::string("\0\1\xff\0",4));
  canceled=true;{TraceRawFiles writer;assert(!writer.Prepare(archive,directory.string(),"canceled",canceled,error));}assert(!std::filesystem::exists(directory/"canceled.trace"));
  for(const auto& entry:std::filesystem::directory_iterator(directory)){assert(!entry.path().filename().string().starts_with(".suitecut-trace-"));}canceled=false;
  {std::ofstream file(directory/"blocker");file<<"actual file";}
  {TraceRawFiles writer;assert(!writer.Prepare(archive,(directory/"blocker"/"nested").string(),"blocked",canceled,error));assert(error.find("ENOTDIR")!=std::string::npos);}
  // A linked native resource must have actual captured bytes.
  TraceArchive bad;assert(bad.AddText("trace.trace","{\"type\":\"screenshot\",\"file\":\"screenshots/missing.png\"}\n",error));assert(bad.AddText("trace.network","",error));assert(bad.AddText("trace.stacks","{}",error));{TraceRawFiles writer;assert(!writer.Prepare(bad,directory.string(),"missing",canceled,error));assert(error.find("no actual archive entry")!=std::string::npos);}
  std::filesystem::remove_all(directory);std::cout<<"Native raw trace bytes, resource namespaces, UTF16, cancellation and actual errno passed\n";
}
