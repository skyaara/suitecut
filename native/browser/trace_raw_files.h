// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <functional>
#include <map>
#include <memory>
#include <openssl/sha.h>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <unistd.h>
#include <vector>
#include "trace_archive.h"

namespace suitecut {
// Native writer-thread staging for raw trace journals. Resources get a genuine
// filesystem-allocated namespace per export, so independently recorded chunks
// cannot overwrite each other's non-content-addressed PNG/WebSocket resources.
// Every rename is atomic; a set of multiple files is not a POSIX transaction.
class TraceRawFiles {
 public:
  // One actual native chunk keeps a single filesystem lease and namespace.
  // This owner has no filesystem teardown: staging cleanup stays on its writer.
  struct Live {
    ~Live(){if(!path_key.empty()){std::lock_guard<std::mutex> guard(paths_mutex_);paths_.erase(path_key);}}
    std::filesystem::path directory;std::string name,resource_name,path_key;
    std::map<std::string,std::string> published;
  };
  ~TraceRawFiles(){Cleanup();}
  bool PrepareLive(const TraceArchive& archive,const std::string& requested,const std::string& name,const std::atomic_bool& canceled,std::shared_ptr<Live> live,std::string& error){
    live_=std::move(live);if(!live_){error="Native live trace writer has no chunk owner";return false;}
    return Prepare(archive,requested,name,canceled,error);
  }
  static bool ValidName(const std::string& name){return !name.empty()&&name.size()<=200&&name!="."&&name!=".."&&name.find('/')==std::string::npos&&name.find('\0')==std::string::npos;}
  bool Prepare(const TraceArchive& archive,const std::string& requested,const std::string& name,const std::atomic_bool& canceled,std::string& error){
    if(!ValidName(name)){error="Invalid native raw trace name";return false;}
    std::error_code code;
    if(live_&&!live_->path_key.empty()){
      directory_=live_->directory;if(name!=live_->name){error="Native live trace chunk name changed";return false;}
    }else{
      std::filesystem::create_directories(requested,code);if(code)return Fail("Could not create native raw trace directory",code,error);
      directory_=std::filesystem::canonical(requested,code);if(code)return Fail("Could not resolve native raw trace directory",code,error);
      const auto key=(directory_/name).string();{std::lock_guard<std::mutex> guard(paths_mutex_);if(paths_.contains(key)){error="Native raw trace output is already being written";return false;}paths_.insert(key);}
      if(live_){live_->directory=directory_;live_->name=name;live_->path_key=key;}else path_key_=key;
    }
    auto pattern=(directory_/".suitecut-trace-XXXXXX").string();if(!mkdtemp(pattern.data()))return Fail("Could not stage native raw trace",std::error_code(errno,std::generic_category()),error);temporary_=pattern;
    if(live_&&live_->resource_name.empty())live_->resource_name="trace-resources-"+temporary_.filename().string().substr(16);
    resource_name_=live_?live_->resource_name:"trace-resources-"+temporary_.filename().string().substr(16);name_=name;
    for(const auto& [file,bytes]:archive.Entries()){
      if(canceled){error="Native raw trace operation canceled";return false;}
      const bool journal=file=="trace.trace"||file=="trace.network"||file=="trace.stacks";
      if(!journal&&live_){unsigned char hash[SHA256_DIGEST_LENGTH];SHA256(bytes.data(),bytes.size(),hash);const std::string digest(reinterpret_cast<const char*>(hash),sizeof(hash));if(const auto old=live_->published.find(file);old!=live_->published.end()&&old->second==digest)continue;prepared_[file]=digest;}
      const auto destination=journal?temporary_/(name+file.substr(5)):temporary_/resource_name_/file;
      std::filesystem::create_directories(destination.parent_path(),code);if(code)return Fail("Could not stage native raw trace directory",code,error);
      if(file=="trace.trace"||file=="trace.network"){
        std::string rewritten;if(!Rewrite(archive,bytes,resource_name_,rewritten,error))return false;
        if(!Write(destination,reinterpret_cast<const uint8_t*>(rewritten.data()),rewritten.size(),canceled,error))return false;
      }else if(!Write(destination,bytes.data(),bytes.size(),canceled,error))return false;
    }
    return true;
  }
  bool Commit(std::string& error,const std::atomic_bool* canceled=nullptr,const std::function<bool()>& admissible={}){
    const auto active=[&]{if(admissible&&!admissible()){error="Native raw trace original writer deadline expired";return false;}if(canceled&&*canceled){error="Native raw trace operation canceled";return false;}return true;};if(!active())return false;
    std::error_code code;
    if(live_){
      // Actual resource bytes become visible before a journal can reference them.
      for(const auto& [name,digest]:prepared_){if(!active())return false;const auto destination=directory_/resource_name_/name;std::filesystem::create_directories(destination.parent_path(),code);if(code)return Fail("Could not create native live trace resource directory",code,error);std::filesystem::rename(temporary_/resource_name_/name,destination,code);if(code)return Fail("Could not commit native live trace resource",code,error);live_->published[name]=digest;}
    }else if(std::filesystem::exists(temporary_/resource_name_,code)){
      std::filesystem::rename(temporary_/resource_name_,directory_/resource_name_,code);if(code)return Fail("Could not commit native raw trace resources",code,error);
    }else if(code)return Fail("Could not inspect native raw trace resources",code,error);
    for(const char* extension:{".stacks",".network",".trace"}){
      if(!active())return false;
      std::filesystem::rename(temporary_/(name_+extension),directory_/(name_+extension),code);if(code)return Fail("Could not commit native raw trace journal",code,error);
    }
    return true;
  }
 private:
  struct Span{size_t begin=0,end=0;};
  // Scan JSON syntax without interpreting author strings. In particular isolated
  // UTF-16 escapes and literal private-tag-shaped dictionaries remain verbatim.
  static void Space(std::string_view text,size_t& i){while(i<text.size()&&(text[i]==' '||text[i]=='\t'||text[i]=='\r'||text[i]=='\n'))++i;}
  static bool String(std::string_view text,size_t& i){if(i>=text.size()||text[i++]!='"')return false;while(i<text.size()){const char c=text[i++];if(c=='"')return true;if(c=='\\'){if(i==text.size())return false;++i;}else if(static_cast<unsigned char>(c)<32)return false;}return false;}
  static bool Value(std::string_view text,size_t& i,unsigned depth=0){
    if(depth>128){return false;}Space(text,i);if(i>=text.size())return false;if(text[i]=='"')return String(text,i);
    if(text[i]=='{'||text[i]=='['){const char end=text[i++]=='{'?'}':']';Space(text,i);if(i<text.size()&&text[i]==end){++i;return true;}for(;;){if(end=='}'){Space(text,i);if(!String(text,i))return false;Space(text,i);if(i>=text.size()||text[i++]!=':')return false;}if(!Value(text,i,depth+1))return false;Space(text,i);if(i>=text.size())return false;if(text[i]==end){++i;return true;}if(text[i++]!=',')return false;}}
    const auto start=i;while(i<text.size()&&text[i]!=','&&text[i]!='}'&&text[i]!=']'&&text[i]!=' '&&text[i]!='\t'&&text[i]!='\r'&&text[i]!='\n')++i;return i>start;
  }
  static bool Property(std::string_view text,Span object,std::string_view key,Span& output){
    size_t i=object.begin;Space(text,i);if(i>=object.end||text[i++]!='{')return false;for(;;){Space(text,i);if(i>=object.end||text[i]=='}')return false;const size_t start=i;if(!String(text,i))return false;const auto match=text.substr(start,i-start)==key;Space(text,i);if(i>=object.end||text[i++]!=':')return false;Space(text,i);const size_t begin=i;if(!Value(text,i))return false;if(match){output={begin,i};return true;}Space(text,i);if(i>=object.end||text[i++]!=',')return false;}
  }
  static bool Rewrite(const TraceArchive& archive,const std::vector<uint8_t>& input,const std::string& prefix,std::string& output,std::string& error){
    const std::string_view text(reinterpret_cast<const char*>(input.data()),input.size());output.reserve(text.size()+512);size_t start=0;
    while(start<text.size()){
      const auto newline=text.find('\n',start);const size_t end=newline==std::string_view::npos?text.size():newline;const auto line=text.substr(start,end-start);Span type,file,object{0,line.size()};std::vector<Span> files;
      if(Property(line,object,"\"type\"",type)){
        const auto kind=line.substr(type.begin,type.end-type.begin);
        if(kind=="\"screencast-frame\""||kind=="\"screenshot\""||kind=="\"aria-snapshot\""){if(Property(line,object,"\"file\"",file))files.push_back(file);}
        else if(kind=="\"resource-snapshot\""){
          Span snapshot,response,content,request,post;if(Property(line,object,"\"snapshot\"",snapshot)){
            if(Property(line,snapshot,"\"response\"",response)&&Property(line,response,"\"content\"",content)&&Property(line,content,"\"_file\"",file))files.push_back(file);
            if(Property(line,snapshot,"\"request\"",request)&&Property(line,request,"\"postData\"",post)&&Property(line,post,"\"_file\"",file))files.push_back(file);
          }
        }
      }
      std::sort(files.begin(),files.end(),[](Span a,Span b){return a.begin<b.begin;});size_t copied=0;
      for(const auto& link:files){
        const auto token=line.substr(link.begin,link.end-link.begin);bool found=false;
        // Match only exact native resource tokens with actual archive bytes.
        // Arbitrary author DOM strings and private-tag-shaped data stay verbatim.
        for(const auto& [name,bytes]:archive.Entries())if(name!="trace.trace"&&name!="trace.network"&&name!="trace.stacks"&&token.size()==name.size()+2&&token.front()=='"'&&token.back()=='"'&&token.substr(1,token.size()-2)==name){
          output.append(line.substr(copied,link.begin-copied));output+='"';output+=prefix+'/'+name;output+='"';copied=link.end;found=true;break;
        }
        if(!found){error="Native raw trace resource reference has no actual archive entry";return false;}
      }
      output.append(line.substr(copied));
      if(newline!=std::string_view::npos){output+='\n';}start=end+(newline!=std::string_view::npos);
    }
    return true;
  }
  static bool Write(const std::filesystem::path& path,const uint8_t* data,size_t size,const std::atomic_bool& canceled,std::string& error){
    const int descriptor=open(path.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,0600);if(descriptor<0)return Fail("Could not create native raw trace file",std::error_code(errno,std::generic_category()),error);
    bool ok=true;for(size_t offset=0;offset<size&&!canceled;){const auto count=write(descriptor,data+offset,std::min(size_t(1048576),size-offset));if(count<0&&errno==EINTR)continue;if(count<=0){ok=Fail("Could not write native raw trace file",std::error_code(errno,std::generic_category()),error);break;}offset+=size_t(count);}
    if(canceled){error="Native raw trace operation canceled";ok=false;}
    if(ok&&fsync(descriptor)!=0)ok=Fail("Could not flush native raw trace file",std::error_code(errno,std::generic_category()),error);
    if(close(descriptor)!=0&&ok)ok=Fail("Could not close native raw trace file",std::error_code(errno,std::generic_category()),error);
    return ok;
  }
  static bool Fail(const char* action,const std::error_code& code,std::string& error){
    const char* label=nullptr;for(const auto& [value,name]:std::initializer_list<std::pair<std::errc,const char*>>{{std::errc::no_such_file_or_directory,"ENOENT"},{std::errc::not_a_directory,"ENOTDIR"},{std::errc::permission_denied,"EACCES"},{std::errc::operation_not_permitted,"EPERM"},{std::errc::no_space_on_device,"ENOSPC"},{std::errc::read_only_file_system,"EROFS"},{std::errc::filename_too_long,"ENAMETOOLONG"},{std::errc::is_a_directory,"EISDIR"},{std::errc::file_exists,"EEXIST"}})if(code==std::make_error_condition(value)){label=name;break;}
    error=std::string(action)+": "+(label?label:code.category().name())+" ("+code.message()+", code "+std::to_string(code.value())+")";return false;
  }
  void Cleanup(){if(!temporary_.empty()){std::error_code ignored;std::filesystem::remove_all(temporary_,ignored);}if(!path_key_.empty()){std::lock_guard<std::mutex> guard(paths_mutex_);paths_.erase(path_key_);}}
  inline static std::mutex paths_mutex_;inline static std::set<std::string> paths_;
  std::shared_ptr<Live> live_;std::map<std::string,std::string> prepared_;
  std::filesystem::path temporary_,directory_;std::string name_,resource_name_,path_key_;
};
} // namespace suitecut
