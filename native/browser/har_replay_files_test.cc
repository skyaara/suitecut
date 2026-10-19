// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <filesystem>
#include <fstream>
#include <unistd.h>
#include "include/cef_api_hash.h"
#include "include/cef_parser.h"
#include "har_replay_files.h"
int main(){assert(cef_api_hash(CEF_API_VERSION,0));using Reader=suitecut::NativeHARReplayFiles;std::vector<std::function<void()>>background,owner;double now=1;Reader::Hooks hooks;hooks.now=[&]{return now;};hooks.background=[&](std::function<void()>job){background.push_back(std::move(job));return true;};hooks.owner=[&](std::function<void()>job){owner.push_back(std::move(job));return true;};auto reader=std::make_shared<Reader>(std::move(hooks));auto path=(std::filesystem::temp_directory_path()/std::filesystem::path("suitecut-har-reader-"+std::to_string(getpid())+".zip")).string();auto archive=CefParseJSON(R"({"log":{"entries":[{"request":{"url":"https://native.test/bytes","method":"GET","headers":[]},"response":{"status":200,"statusText":"OK","headers":[],"content":{"text":"AP8NCg==","encoding":"base64"}}}]}})",JSON_PARSER_RFC)->GetDictionary();std::atomic_bool canceled=false;suitecut::NativeHARFiles::Action action{canceled,20,[&]{return now;}};std::string error;assert(suitecut::NativeHARFiles::Write(path,archive,suitecut::NativeHARFiles::Content::Attach,true,action,error));
  Reader::Replay retained;assert(reader->Read(1,path,20,[&](Reader::Replay replay,const std::string&error){assert(error.empty());retained=std::move(replay);}));assert(reader->Physical()==1&&!retained);background[0]();assert(reader->Physical()==1);owner[0]();assert(reader->Physical()==0&&retained&&Reader::RetainedBytes()==retained->bytes());auto request=CefParseJSON(R"({"url":"https://native.test/bytes","method":"GET","headers":{}})",JSON_PARSER_RFC)->GetDictionary();assert(retained->Lookup(request)->body==std::vector<uint8_t>({0,255,13,10}));retained.reset();assert(Reader::RetainedBytes()==0);
  assert(reader->Read(2,path,20,[](Reader::Replay,const std::string&){assert(false);}));reader->Cancel(2);assert(reader->Physical()==1);background[1]();assert(reader->Physical()==1);reader->Tick();assert(reader->Physical()==0&&Reader::RetainedBytes()==0);owner[1]();
  bool expired=false;assert(reader->Read(3,path,5,[&](Reader::Replay replay,const std::string&error){assert(!replay&&error.find("deadline")!=std::string::npos);expired=true;}));now=6;background[2]();owner[2]();assert(expired&&reader->Physical()==0&&Reader::RetainedBytes()==0);
  const auto empty_path=path+".empty.har";
  {std::ofstream file(empty_path);file<<R"({"log":{}})";assert(file.good());}
  bool strict_failed=false;assert(reader->Read(4,empty_path,20,[&](Reader::Replay value,const std::string&reason){assert(!value&&!reason.empty());strict_failed=true;}));background[3]();owner[3]();assert(strict_failed&&reader->Physical()==0);
  bool empty_fallback=false;assert(reader->Read(5,empty_path,20,[&](Reader::Replay value,const std::string&reason){assert(value&&value->size()==0&&value->bytes()==0&&reason.empty());assert(!value->Lookup(request));empty_fallback=true;},true));assert(reader->Physical()==1);background[4]();assert(reader->Physical()==1);owner[4]();assert(empty_fallback&&reader->Physical()==0&&Reader::RetainedBytes()==0);
  assert(reader->Read(6,empty_path,20,[](Reader::Replay,const std::string&){assert(false);},true));reader->Cancel(6);assert(reader->Physical()==1);background[5]();assert(reader->Physical()==1);owner[5]();assert(reader->Physical()==0&&Reader::RetainedBytes()==0);
  reader->Clear();std::filesystem::remove(empty_path);std::filesystem::remove(path);
}
