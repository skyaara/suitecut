// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <atomic>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>
#include <unistd.h>
#include "include/cef_api_hash.h"
#include "include/cef_parser.h"
#include "include/cef_stream.h"
#include "include/cef_zip_reader.h"
#include "har_files.h"
#include "har_replay.h"
#include "trace_archive.h"
using Files=suitecut::NativeHARFiles;using Dict=Files::Dict;using Bytes=std::vector<uint8_t>;
Dict MemberObject(const std::string&text){return CefParseJSON(text,JSON_PARSER_RFC)->GetDictionary();}
std::string MemberJSON(Dict value){auto root=CefValue::Create();root->SetDictionary(value->Copy(false));return CefWriteJSON(root,JSON_WRITER_DEFAULT).ToString();}
std::map<std::string,Bytes> Members(const std::string&path){
 auto stream=CefStreamReader::CreateForFile(path);auto reader=CefZipReader::Create(stream);assert(reader&&reader->MoveToFirstFile());std::map<std::string,Bytes> result;
 do{auto name=reader->GetFileName().ToString();const auto size=reader->GetFileSize();assert(size>=0&&uint64_t(size)<=Files::kBodyBytes&&suitecut::TraceArchive::ValidName(name));Bytes bytes(static_cast<size_t>(size));assert(reader->OpenFile(""));size_t offset=0;while(offset<bytes.size()){const int count=reader->ReadFile(bytes.data()+offset,bytes.size()-offset);assert(count>0);offset+=static_cast<size_t>(count);}uint8_t extra=0;assert(reader->ReadFile(&extra,1)==0&&reader->CloseFile());assert(result.emplace(name,std::move(bytes)).second);}while(reader->MoveToNextFile());assert(reader->Close());return result;
}
int main(){
 assert(cef_api_hash(CEF_API_VERSION,0));const auto directory=std::filesystem::temp_directory_path()/std::filesystem::path("suitecut-har-members-"+std::to_string(getpid()));std::filesystem::create_directories(directory);
 const std::string payload="Actual attached bytes",hash="3b127f0fe0ef142f25f917c1dc4c3c571868e1e5";const Bytes actual(payload.begin(),payload.end());
 struct Case{std::string mime,suffix;bool present=true,boolean=false;};
 const std::vector<Case>cases={{" Text/Html ; charset=UTF-8",".html"},{"text/css;charset=utf-8",".css"},{"image/png",".png"},{"application/javascript",".js"},{"text/javascript",".js"},{"application/json",".json"},{"text/plain",".txt"},{"application/x-custom",".dat"},{"",".dat",false},{"",".dat",true,true},{std::string(257,'x'),".dat"},{"../../evil.har",".dat"},{std::string("image/png\0../evil.har",21),".dat"},{"text/html; charset=x/../../escape.har",".html"}};
 auto archive=MemberObject(R"({"log":{"version":"1.2","entries":[]}})");auto entries=archive->GetDictionary("log")->GetList("entries");
 for(size_t i=0;i<cases.size();++i){auto entry=MemberObject(R"({"request":{"method":"GET","headers":[]},"response":{"status":200,"statusText":"OK","headers":[],"content":{}}})");entry->GetDictionary("request")->SetString("url","http://native.test/entry-"+std::to_string(i));auto content=entry->GetDictionary("response")->GetDictionary("content");content->SetString("text",payload);content->SetInt("size",static_cast<int>(payload.size()));if(cases[i].present){if(cases[i].boolean)content->SetBool("mimeType",true);else content->SetString("mimeType",cases[i].mime);}entries->SetDictionary(i,entry->Copy(false));}
 const auto original=MemberJSON(archive);std::atomic_bool canceled=false;double now=2;Files::Action action{canceled,99,[&]{return now;}};std::string error;const auto path=(directory/"actual.zip").string();assert(Files::Write(path,archive,Files::Content::Attach,true,action,error)&&error.empty());assert(MemberJSON(archive)==original);
 auto members=Members(path);assert(members.size()==8&&members.contains("har.har"));for(const auto&[name,bytes]:members)if(name!="har.har"){assert(name.find('/')==std::string::npos&&bytes==actual);}
 auto raw=MemberObject(std::string(members.at("har.har").begin(),members.at("har.har").end()));auto written=raw->GetDictionary("log")->GetList("entries");assert(written->GetSize()==cases.size());for(size_t i=0;i<cases.size();++i){assert(written->GetDictionary(i)->GetDictionary("request")->GetString("url")=="http://native.test/entry-"+std::to_string(i));auto content=written->GetDictionary(i)->GetDictionary("response")->GetDictionary("content");const auto name=hash+cases[i].suffix;assert(content->GetString("_file")==name&&members.at(name)==actual&&!content->HasKey("text")&&!content->HasKey("encoding"));}
 Dict loaded;assert(Files::Read(path,action,loaded,error));suitecut::NativeHARReplay replay;assert(replay.Load(loaded,error));for(size_t i=0;i<cases.size();++i){auto request=MemberObject(R"({"method":"GET","headers":{}})");request->SetString("url","http://native.test/entry-"+std::to_string(i));const auto match=replay.Lookup(request);assert(match&&match->body==actual);}
 // Non-ZIP attachments retain their exact old namespace and member format.
 const auto plain=(directory/"actual.har").string();assert(Files::Write(plain,archive,Files::Content::Attach,false,action,error));std::ifstream file(plain);const std::string text((std::istreambuf_iterator<char>(file)),{});auto document=MemberObject(text);assert(document->GetDictionary("log")->GetList("entries")->GetDictionary(0)->GetDictionary("response")->GetDictionary("content")->GetString("_file")=="resources/"+hash+".dat");assert(std::filesystem::exists(directory/"resources"/(hash+".dat")));assert(Files::Read(plain,action,loaded,error));
 // Read the actual prior-format ZIP as well, using unmodified safe path logic.
 auto legacy=raw->Copy(false);auto legacy_entries=legacy->GetDictionary("log")->GetList("entries");for(size_t i=0;i<legacy_entries->GetSize();++i)legacy_entries->GetDictionary(i)->GetDictionary("response")->GetDictionary("content")->SetString("_file","resources/"+hash+".dat");suitecut::TraceArchive old_zip;assert(old_zip.AddText("har.har",MemberJSON(legacy),error)&&old_zip.Add("resources/"+hash+".dat",actual,error));Bytes encoded;assert(old_zip.Encode(encoded,error));const auto old=(directory/"legacy.zip").string();{std::ofstream output(old,std::ios::binary);output.write(reinterpret_cast<const char*>(encoded.data()),encoded.size());}assert(Files::Read(old,action,loaded,error));assert(replay.Load(loaded,error));
 // Preserve old destination on cancellation/deadline, including cancellation
 // after the real private temporary file is created but before its first write.
 const auto canceled_path=(directory/"preserved.zip").string();{std::ofstream out(canceled_path);out<<"old";}canceled=true;assert(!Files::Write(canceled_path,archive,Files::Content::Attach,true,action,error));canceled=false;now=100;assert(!Files::Write(canceled_path,archive,Files::Content::Attach,true,action,error));now=2;
 size_t calls=0;Files::Action mid{canceled,99,[&]{if(++calls==cases.size()+2)canceled=true;return 2.;}};assert(!Files::Write(canceled_path,archive,Files::Content::Attach,true,mid,error));assert(calls>=cases.size()+2);canceled=false;{std::ifstream preserved(canceled_path);std::string value;preserved>>value;assert(value=="old");}for(const auto&entry:std::filesystem::directory_iterator(directory))assert(entry.path().filename().string().find(".suitecut-har-")==std::string::npos);
 std::filesystem::remove_all(directory);
}
