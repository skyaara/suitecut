// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <atomic>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <unistd.h>
#include "include/cef_api_hash.h"
#include "include/cef_parser.h"
#include "har/har_files.h"
#include "har/har_replay.h"
#include "trace/trace_archive.h"
using Files=suitecut::NativeHARFiles;
using Dict=Files::Dict;
Dict Parse(const std::string& text){const auto value=CefParseJSON(text,JSON_PARSER_RFC);return value?value->GetDictionary():nullptr;}
int main(){
  assert(cef_api_hash(CEF_API_VERSION,0));
  const auto directory=std::filesystem::temp_directory_path()/std::filesystem::path("suitecut-har-files-"+std::to_string(getpid()));std::filesystem::create_directories(directory);
  std::atomic_bool canceled=false;double now=2;Files::Action action{canceled,99,[&]{return now;}};std::string error;Dict loaded;
  auto archive=Parse(R"({"log":{"version":"1.2","entries":[{"request":{"url":"https://native.test/body","method":"POST","headers":[],"postData":{"mimeType":"application/octet-stream","text":"AP8NCg==","_encoding":"base64"}},"response":{"status":201,"statusText":"Created","headers":[{"name":"Content-Type","value":"application/octet-stream"},{"name":"Set-Cookie","value":"a=1"},{"name":"Set-Cookie","value":"b=2"}],"content":{"size":4,"mimeType":"application/octet-stream","text":"AP8NCg==","encoding":"base64"}}}]}})");
  auto actual=Parse(R"({"url":"https://native.test/body","method":"POST","headers":{},"postDataEntries":[{"bytes":"AP8NCg=="}]})");
  for(const bool zip:{false,true})for(const auto content:{Files::Content::Embed,Files::Content::Attach,Files::Content::Omit}){
    const auto path=(directory/(std::string(zip?"archive-":"plain-")+std::to_string(int(content))+(zip?".zip":".har"))).string();error.clear();
    assert(Files::Write(path,archive,content,zip,action,error));assert(error.empty());
    assert(Files::Read(path,action,loaded,error));assert(error.empty());
    suitecut::NativeHARReplay replay;assert(replay.Load(loaded,error));const auto match=replay.Lookup(actual);
    if(content==Files::Content::Omit)assert(!match);else{assert(match&&match->status==201&&match->body==std::vector<uint8_t>({0,255,13,10}));size_t cookies=0;for(const auto&[name,value]:match->headers)if(name=="Set-Cookie")++cookies;assert(cookies==2);}
    assert(archive->GetDictionary("log")->GetList("entries")->GetDictionary(0)->GetDictionary("response")->GetDictionary("content")->HasKey("text"));
  }
  const auto path=(directory/"atomic.har").string();{std::ofstream original(path);original<<"original";}
  canceled=true;assert(!Files::Write(path,archive,Files::Content::Embed,false,action,error));canceled=false;
  {std::ifstream original(path);std::string text;original>>text;assert(text=="original");}
  now=100;assert(!Files::Write(path,archive,Files::Content::Embed,false,action,error));now=2;
  auto traversal=archive->Copy(false);auto body=traversal->GetDictionary("log")->GetList("entries")->GetDictionary(0)->GetDictionary("response")->GetDictionary("content");body->Remove("text");body->Remove("encoding");body->SetString("_file","../escape");auto value=CefValue::Create();value->SetDictionary(traversal);{std::ofstream file(path);file<<CefWriteJSON(value,JSON_WRITER_DEFAULT).ToString();}
  assert(!Files::Read(path,action,loaded,error)&&error.find("path")!=std::string::npos);
  // Only explicit fallback may interpret an actual empty log as zero entries.
  const auto empty=(directory/"empty-log.har").string();{std::ofstream file(empty);file<<R"({"log":{}})";}
  const auto prior=loaded;error.clear();assert(!Files::Read(empty,action,loaded,error)&&loaded==prior);error.clear();assert(Files::Read(empty,action,loaded,error,true)&&error.empty());
  assert(loaded->GetDictionary("log")->GetList("entries")->GetSize()==0);suitecut::NativeHARReplay empty_replay;assert(empty_replay.Load(loaded,error)&&empty_replay.size()==0&&!empty_replay.Lookup(actual));
  for(const std::string invalid:{R"({"log":{"entries":{}}})",R"({"log":null})",R"({})",R"({not-json})"}){std::ofstream file(empty);file<<invalid;file.close();error.clear();assert(!Files::Read(empty,action,loaded,error,true));}
  {std::ofstream file(empty);file<<R"({"log":{}})";}
  canceled=true;error.clear();assert(!Files::Read(empty,action,loaded,error,true));canceled=false;
  now=100;error.clear();assert(!Files::Read(empty,action,loaded,error,true));now=2;
  // No suitecut byte or count cap on HAR files: unreferenced ZIP members, bodies
  // past the old 2 MiB bound and more entries than ZIP32 holds load whole.
  suitecut::TraceArchive zip;assert(zip.AddText("har.har",R"({"log":{"entries":[]}})",error));
  for(size_t i=0;i<4;++i)assert(zip.Add("resources/unused-"+std::to_string(i)+".dat",std::vector<uint8_t>(8*1048576,0),error));
  std::vector<uint8_t> encoded;assert(zip.Encode(encoded,error));const auto large=(directory/"unused-members.zip").string();{std::ofstream file(large,std::ios::binary);file.write(reinterpret_cast<const char*>(encoded.data()),encoded.size());}
  error.clear();assert(Files::Read(large,action,loaded,error)&&error.empty());
  {auto many=CefDictionaryValue::Create();auto log=CefDictionaryValue::Create();auto entries=CefListValue::Create();
   for(size_t i=0;i<66000;++i){auto entry=CefDictionaryValue::Create();auto response=CefDictionaryValue::Create();auto content=CefDictionaryValue::Create();if(!i)content->SetString("text",std::string(3*1048576,'b'));response->SetDictionary("content",content);entry->SetDictionary("response",response);entries->SetDictionary(i,entry);}
   log->SetList("entries",entries);many->SetDictionary("log",log);const auto path=(directory/"many.har").string();error.clear();assert(Files::Write(path,many,Files::Content::Attach,false,action,error));
   Dict reread;assert(Files::Read(path,action,reread,error)&&reread->GetDictionary("log")->GetList("entries")->GetSize()==66000);
   assert(reread->GetDictionary("log")->GetList("entries")->GetDictionary(0)->GetDictionary("response")->GetDictionary("content")->GetString("text").ToString().size()==4*1048576);}
  std::filesystem::remove_all(directory);
}
