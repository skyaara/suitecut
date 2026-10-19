// SPDX-License-Identifier: MIT
#pragma once
#include "suite_file_owner.h"
#include <map>
#include <memory>
#include <optional>
#include <sstream>

namespace suitecut::runner {
struct FixtureDeclarationLocation {
  uint64_t declaration=0;
  std::string file;
  uint32_t line=0,column=0;
};
// Actual native reads of genuine host-captured declaration callsites. This
// registry never guesses a line, claims a missing file exists, or reads JS code
// to infer fixture behavior. Source excerpts are optional diagnostic evidence.
class FixtureDeclarationSources {
 public:
  static constexpr size_t kFiles=32,kDeclarations=256,kFileBytes=1024*1024,kBytes=8*1024*1024;
  void Capture(const FixtureDeclarationLocation& location){
    if(!location.declaration||location.declaration>kDeclarations||!location.line||!location.column||
        location.line>2147483647||location.column>2147483647||location.file.empty()||location.file[0]!='/'||
        location.file.size()>16384||location.file.find('\0')!=std::string::npos||locations_.contains(location.declaration))
      throw std::invalid_argument("Native fixture declaration callsite rejected");
    locations_.emplace(location.declaration,location);
    if(files_.contains(location.file)||unavailable_.contains(location.file))return;
    if(files_.size()>=kFiles)throw std::length_error("Native fixture source file capacity exceeded");
    std::shared_ptr<SuiteFileOwner> owner;std::string bytes;
    try{owner=std::make_shared<SuiteFileOwner>(files_.size()+1,location.file);bytes=owner->ReadBytes(kFileBytes);}
    catch(const std::system_error&){unavailable_.emplace(location.file,true);return;}
    catch(const std::invalid_argument&){unavailable_.emplace(location.file,true);return;}
    catch(const std::length_error&){unavailable_.emplace(location.file,true);return;}
    catch(const std::runtime_error&){unavailable_.emplace(location.file,true);return;}
    if(bytes.size()>kBytes-bytes_)throw std::length_error("Native fixture source byte capacity exceeded");
    const auto size=bytes.size();files_.emplace(location.file,File{std::move(owner),std::move(bytes)});bytes_+=size;
  }
  std::optional<std::string> Excerpt(uint64_t declaration)const{
    const auto location=locations_.find(declaration);if(location==locations_.end())return std::nullopt;
    const auto file=files_.find(location->second.file);if(file==files_.end())return std::nullopt;
    try{
      file->second.owner->Validate();
      if(file->second.owner->ReadBytes(kFileBytes)!=file->second.bytes)return std::nullopt;
    }catch(const std::system_error&){return std::nullopt;}
    catch(const std::invalid_argument&){return std::nullopt;}
    catch(const std::length_error&){return std::nullopt;}
    catch(const std::runtime_error&){return std::nullopt;}
    const auto& bytes=file->second.bytes;size_t begin=0;uint32_t line=1;
    while(line<location->second.line){const auto end=bytes.find('\n',begin);if(end==std::string::npos)return std::nullopt;begin=end+1;++line;}
    const auto end=bytes.find('\n',begin);const auto length=(end==std::string::npos?bytes.size():end)-begin;
    if(length>2048)return std::nullopt; // Never truncate a UTF8 scalar or invent a preview.
    auto text=bytes.substr(begin,length);if(!text.empty()&&text.back()=='\r')text.pop_back();
    std::ostringstream excerpt;excerpt<<"> "<<line<<" | "<<text;
    return excerpt.str();
  }
  const FixtureDeclarationLocation* Location(uint64_t declaration)const{
    const auto found=locations_.find(declaration);return found==locations_.end()?nullptr:&found->second;
  }
 private:
  struct File{std::shared_ptr<SuiteFileOwner> owner;std::string bytes;};
  std::map<uint64_t,FixtureDeclarationLocation> locations_;
  std::map<std::string,File> files_;
  std::map<std::string,bool> unavailable_;
  size_t bytes_=0;
};
} // namespace suitecut::runner
