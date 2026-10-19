// SPDX-License-Identifier: MIT
#include "har_files.h"
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <filesystem>
#include <limits>
#include <map>
#include <set>
#include <vector>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include "include/cef_parser.h"
#include "include/cef_stream.h"
#include "include/cef_zip_reader.h"
#include "openssl/evp.h"
#include "trace_archive.h"
namespace suitecut {
namespace {
using Dict=NativeHARFiles::Dict;
using Action=NativeHARFiles::Action;
using Bytes=std::vector<uint8_t>;
using Resources=std::map<std::string,Bytes>;
struct Descriptor {int value=-1;~Descriptor(){if(value>=0)close(value);}};
bool Current(const Action& action,std::string& error){
  if(action.canceled.load(std::memory_order_acquire)){error="Native HAR file operation canceled";return false;}
  if(!action.now||!std::isfinite(action.deadline)||action.deadline<0){error="Invalid native HAR original file deadline";return false;}
  if(action.deadline&&action.now()>=action.deadline){error="Native HAR original file deadline exceeded";return false;}
  return true;
}
bool Path(const std::string& path,std::string& error){
  if(path.empty()||path.size()>4096||path.find('\0')!=std::string::npos||!std::filesystem::path(path).is_absolute()){error="Native HAR requires an absolute file path";return false;}return true;
}
bool ReadBytes(const std::string& path,size_t limit,const Action& action,Bytes& bytes,std::string& error){
  if(!Current(action,error))return false;
  Descriptor file{open(path.c_str(),O_RDONLY|O_CLOEXEC|O_NONBLOCK)};struct stat info{};
  if(file.value<0||fstat(file.value,&info)||!S_ISREG(info.st_mode)||info.st_size<0){error="Native HAR input is not a readable regular file";return false;}
  if(uint64_t(info.st_size)>limit){error="Native HAR input file exceeds its bounded byte capacity";return false;}
  Bytes result;result.reserve(size_t(info.st_size));uint8_t chunk[65536];
  for(;;){if(!Current(action,error))return false;const auto count=read(file.value,chunk,sizeof(chunk));if(count<0&&errno==EINTR)continue;if(count<0){error="Native HAR file read failed";return false;}if(!count)break;if(size_t(count)>limit-result.size()){error="Native HAR input file grew beyond byte capacity";return false;}result.insert(result.end(),chunk,chunk+count);}
  bytes=std::move(result);return true;
}
bool Commit(const std::string& path,const Bytes& bytes,const Action& action,std::string& error){
  if(!Current(action,error))return false;
  const auto parent=std::filesystem::path(path).parent_path();std::error_code ec;std::filesystem::create_directories(parent,ec);if(ec){error="Cannot create native HAR destination directory";return false;}
  std::string pattern=path+".suitecut-har-XXXXXX";std::vector<char> temporary(pattern.begin(),pattern.end());temporary.push_back('\0');Descriptor output{mkstemp(temporary.data())};
  if(output.value<0){error="Cannot create private native HAR temporary file";return false;}
  const auto cleanup=[&]{unlink(temporary.data());};
  if(fchmod(output.value,0600)){cleanup();error="Cannot secure native HAR output";return false;}
  size_t offset=0;while(offset<bytes.size()){
    if(!Current(action,error)){cleanup();return false;}
    const auto size=std::min(size_t(65536),bytes.size()-offset);const auto count=write(output.value,bytes.data()+offset,size);
    if(count<0&&errno==EINTR)continue;
    if(count<=0){cleanup();error="Native HAR file write failed";return false;}offset+=size_t(count);
  }
  if(fsync(output.value)){cleanup();error="Native HAR file flush failed";return false;}
  if(!Current(action,error)){cleanup();return false;}
  if(rename(temporary.data(),path.c_str())){cleanup();error="Native HAR atomic file replacement failed";return false;}
  return true;
}
bool Body(Dict value,const char* field,Bytes& bytes,std::string& error){
  if(value->GetType("text")!=VTYPE_STRING){error="Native HAR body text must be a string";return false;}
  const auto text=value->GetString("text").ToString(),encoding=value->GetString(field).ToString();
  if(encoding.empty()){if(text.size()>NativeHARFiles::kBodyBytes){error="Native HAR body exceeds 2 MiB";return false;}bytes.assign(text.begin(),text.end());return true;}
  if(encoding!="base64"||text.size()>2796204){error="Invalid native HAR body encoding or capacity";return false;}
  if(text.empty()){bytes.clear();return true;}auto decoded=CefBase64Decode(text);
  if(!decoded||decoded->GetSize()>NativeHARFiles::kBodyBytes){error="Invalid native HAR base64 body";return false;}
  bytes.resize(decoded->GetSize());decoded->GetData(bytes.data(),bytes.size(),0);
  if(CefBase64Encode(bytes.data(),bytes.size())!=text){error="Noncanonical native HAR base64 body";return false;}return true;
}
// ZIP member names use only a bounded, explicit observed MIME projection;
// never a URL suffix or author-provided path. Unknowns keep the .dat suffix.
std::string Extension(Dict content){
  if(!content||content->GetType("mimeType")!=VTYPE_STRING||content->GetString("mimeType").length()>256)return ".dat";
  auto mime=content->GetString("mimeType").ToString();mime.resize(mime.find(';')==std::string::npos?mime.size():mime.find(';'));
  const auto first=mime.find_first_not_of(" \t"),last=mime.find_last_not_of(" \t");if(first==std::string::npos)return ".dat";mime=mime.substr(first,last-first+1);
  for(auto&c:mime)if(c>='A'&&c<='Z')c=char(c-'A'+'a');
  if(mime=="text/html")return ".html";
  if(mime=="text/css")return ".css";
  if(mime=="application/javascript"||mime=="text/javascript")return ".js";
  if(mime=="application/json")return ".json";
  if(mime=="text/plain")return ".txt";
  if(mime=="image/png")return ".png";
  return ".dat";
}
std::string Resource(const Bytes& bytes,Dict content,bool zipped){
  unsigned char hash[EVP_MAX_MD_SIZE];unsigned size=0;if(!EVP_Digest(bytes.data(),bytes.size(),hash,&size,EVP_sha1(),nullptr)||size!=20)return {};
  const char* hex="0123456789abcdef";std::string result=zipped?"":"resources/";for(unsigned i=0;i<size;++i){result+=hex[hash[i]>>4];result+=hex[hash[i]&15];}return result+(zipped?Extension(content):".dat");
}
bool Entries(Dict archive,CefRefPtr<CefListValue>& entries,std::string& error){
  auto log=archive?archive->GetDictionary("log"):nullptr;entries=log?log->GetList("entries"):nullptr;
  if(!entries||entries->GetSize()>NativeHARFiles::kResources){error="Invalid native HAR entries or entry capacity";return false;}return true;
}
bool Attach(Dict archive,NativeHARFiles::Content mode,bool zipped,Resources& resources,const Action& action,std::string& error){
  CefRefPtr<CefListValue> entries;if(!Entries(archive,entries,error))return false;size_t retained=0;
  for(size_t i=0;i<entries->GetSize();++i){
    if(!Current(action,error))return false;
    auto entry=entries->GetDictionary(i);auto response=entry?entry->GetDictionary("response"):nullptr;auto content=response?response->GetDictionary("content"):nullptr;if(!content){error="Native HAR entry has no response content metadata";return false;}
    if(mode==NativeHARFiles::Content::Embed)continue;
    if(mode==NativeHARFiles::Content::Omit){content->Remove("text");content->Remove("encoding");content->Remove("_file");content->Remove("_sha1");continue;}
    if(!content->HasKey("text"))continue;
    Bytes bytes;if(!Body(content,"encoding",bytes,error))return false;const auto name=Resource(bytes,content,zipped);if(name.empty()){error="Native HAR resource digest failed";return false;}
    const auto found=resources.find(name);
    if(found!=resources.end()&&found->second!=bytes){error="Native HAR attachment digest/name conflicts with actual body bytes";return false;}
    if(found==resources.end()){if(resources.size()>=NativeHARFiles::kResources||bytes.size()>NativeHARFiles::kJSONBytes-retained){error="Native HAR attachment capacity exceeded";return false;}retained+=bytes.size();resources.emplace(name,std::move(bytes));}
    content->Remove("text");content->Remove("encoding");content->Remove("_sha1");content->SetString("_file",name);
  }return true;
}
bool Rehydrate(Dict archive,const Resources* embedded,const std::filesystem::path& parent,const Action& action,std::string& error){
  CefRefPtr<CefListValue> entries;if(!Entries(archive,entries,error))return false;size_t retained=0;
  for(size_t i=0;i<entries->GetSize();++i){
    if(!Current(action,error))return false;
    auto entry=entries->GetDictionary(i);auto request=entry?entry->GetDictionary("request"):nullptr;auto response=entry?entry->GetDictionary("response"):nullptr;
    for(const auto& [object,encoding]:std::vector<std::pair<Dict,const char*>>{{request?request->GetDictionary("postData"):nullptr,"_encoding"},{response?response->GetDictionary("content"):nullptr,"encoding"}}){
      if(!object||!object->HasKey("_file"))continue;
      if(object->GetType("_file")!=VTYPE_STRING||object->HasKey("text")){error="Ambiguous native HAR body attachment";return false;}
      const auto name=object->GetString("_file").ToString();if(!TraceArchive::ValidName(name)){error="Native HAR body attachment path is invalid";return false;}
      Bytes bytes;
      if(embedded){const auto file=embedded->find(name);if(file==embedded->end()){error="Native HAR ZIP body attachment is missing";return false;}bytes=file->second;}
      else if(!ReadBytes((parent/name).string(),NativeHARFiles::kBodyBytes,action,bytes,error))return false;
      if(bytes.size()>NativeHARFiles::kBodyBytes||bytes.size()>NativeHARFiles::kJSONBytes-retained){error="Native HAR attachment decoded-byte capacity exceeded";return false;}retained+=bytes.size();
      object->SetString("text",CefBase64Encode(bytes.data(),bytes.size()));object->SetString(encoding,"base64");object->Remove("_file");
    }
  }return true;
}
bool ZIP(const Bytes& bytes,const Action& action,Bytes& json,Resources& resources,std::string& error){
  auto stream=CefStreamReader::CreateForData(const_cast<uint8_t*>(bytes.data()),bytes.size());auto reader=stream?CefZipReader::Create(stream):nullptr;
  if(!reader){error="Native HAR ZIP cannot be opened";return false;}const auto fail=[&](const char* message){reader->Close();if(error.empty())error=message;return false;};
  if(!reader->MoveToFirstFile())return fail("Native HAR ZIP contains no files");
  std::set<std::string> names;size_t retained=0;bool found=false;
  do{
    if(!Current(action,error))return fail("Native HAR ZIP canceled");
    const auto name=reader->GetFileName().ToString();const auto size=reader->GetFileSize();
    if(!names.insert(name).second||names.size()>NativeHARFiles::kResources||size<0||uint64_t(size)>NativeHARFiles::kJSONBytes)return fail("Invalid native HAR ZIP entry name, size, or count");
    if(name.ends_with('/')){if(size!=0||!TraceArchive::ValidName(name.substr(0,name.size()-1)))return fail("Invalid native HAR ZIP directory");continue;}
    if(!TraceArchive::ValidName(name))return fail("Invalid native HAR ZIP entry name");
    const bool document=name.ends_with(".har")&&!name.starts_with("resources/");if(!document&&uint64_t(size)>NativeHARFiles::kBodyBytes)return fail("Native HAR ZIP resource exceeds 2 MiB");
    if(size_t(size)>NativeHARFiles::kJSONBytes-retained)return fail("Native HAR ZIP decoded-byte capacity exceeded");
    retained+=size_t(size);
    if(!reader->OpenFile(""))return fail("Native HAR ZIP entry cannot be opened");
    Bytes value(size_t(size),uint8_t(0));size_t offset=0;
    while(offset<value.size()){if(!Current(action,error))return fail("Native HAR ZIP canceled");const auto count=reader->ReadFile(value.data()+offset,std::min(size_t(65536),value.size()-offset));if(count<=0)return fail("Native HAR ZIP entry is truncated or corrupt");offset+=size_t(count);}
    uint8_t extra=0;if(reader->ReadFile(&extra,1)!=0||!reader->CloseFile())return fail("Native HAR ZIP entry size or CRC failed");
    if(document){if(found)return fail("Native HAR ZIP contains multiple archive documents");found=true;json=std::move(value);}else resources.emplace(name,std::move(value));
  }while(reader->MoveToNextFile());
  if(!reader->Close()||!found){error="Native HAR ZIP document is missing or archive close failed";return false;}return true;
}
}
bool NativeHARFiles::Read(const std::string& path,const Action& action,Dict& archive,std::string& error,bool allow_empty_log){
  if(!Path(path,error))return false;
  Bytes bytes;if(!ReadBytes(path,kFileBytes,action,bytes,error))return false;Bytes json;Resources resources;
  const bool zipped=bytes.size()>=4&&bytes[0]=='P'&&bytes[1]=='K'&&bytes[2]==3&&bytes[3]==4;
  if(zipped){if(!ZIP(bytes,action,json,resources,error))return false;}else{if(bytes.size()>kJSONBytes){error="Native HAR JSON exceeds 32 MiB";return false;}json=std::move(bytes);}
  if(!Current(action,error))return false;
  auto parsed=CefParseJSON(std::string(json.begin(),json.end()),JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;
  if(!result){error="Native HAR archive is not a valid JSON object";return false;}
  // Explicit replay fallback treats an actual empty log as zero matches.
  // Other malformed shapes and all file/resource bounds remain strict.
  const auto log=result->GetDictionary("log");
  if(allow_empty_log&&log&&!log->HasKey("entries"))log->SetList("entries",CefListValue::Create());
  if(!Rehydrate(result,zipped?&resources:nullptr,std::filesystem::path(path).parent_path(),action,error))return false;
  archive=result;return true;
}
bool NativeHARFiles::Write(const std::string& path,Dict archive,Content content,bool zipped,const Action& action,std::string& error){
  if(!archive){error="Native HAR archive is unavailable";return false;}
  if(!Path(path,error)||!Current(action,error))return false;
  auto result=archive->Copy(false);Resources resources;if(!Attach(result,content,zipped,resources,action,error))return false;
  auto wrapped=CefValue::Create();wrapped->SetDictionary(result);const auto json=CefWriteJSON(wrapped,JSON_WRITER_DEFAULT).ToString();if(json.size()>kJSONBytes){error="Native HAR JSON exceeds 32 MiB";return false;}
  Bytes output;
  if(zipped){TraceArchive zip;if(!zip.AddText("har.har",json,error))return false;for(auto&[name,bytes]:resources)if(!zip.Add(name,std::move(bytes),error))return false;if(!zip.Encode(output,error))return false;}
  else{
    const auto parent=std::filesystem::path(path).parent_path();if(!resources.empty()){std::error_code ec;const auto directory=parent/"resources";if(std::filesystem::is_symlink(directory,ec)){error="Native HAR attachment directory cannot be a symlink";return false;}}
    for(const auto&[name,bytes]:resources)if(!Commit((parent/name).string(),bytes,action,error))return false;
    output.assign(json.begin(),json.end());
  }
  return Commit(path,output,action,error);
}
} // namespace suitecut
