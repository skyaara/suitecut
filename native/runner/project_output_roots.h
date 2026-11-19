// SPDX-License-Identifier: MIT
#pragma once
#include "output_directory_options.h"
#include <map>
#include <optional>
namespace suitecut::runner {
// Active project-root declaration policy. Project IDs are the
// native zero-based catalog identities, not user names or current JS counters.
// Directory creation/assignment and retirement remain existing native actors.
class NativeProjectOutputRoots {
 public:
  void Declare(size_t project,size_t actually_declared,const std::string& path,const std::filesystem::path& actual_module){
   if(frozen_||!actually_declared||project>=actually_declared||roots_.contains(project))throw std::invalid_argument("Native project output declaration is stale, duplicate or unknown");
   if(path.find('\0')!=std::string::npos||!actual_module.is_absolute()||actual_module.filename().empty())throw std::invalid_argument("Native project output root lacks actual module identity");
   try{(void)AttachmentPathJSON(path);}catch(const std::exception&){throw std::invalid_argument("Native project outputDir must be scalar filesystem UTF8");}
   const auto declared=std::filesystem::path(path);
   auto normalized=NativeOutputDirectoryRoot((declared.is_absolute()?declared:actual_module.parent_path()/declared).string());
   roots_.emplace(project,std::move(normalized));
  }
  void Freeze(size_t actually_declared){
   if(frozen_||(!roots_.empty()&&roots_.rbegin()->first>=actually_declared))throw std::invalid_argument("Native project output catalog cannot freeze unknown declarations");
   projects_=actually_declared;frozen_=true;
  }
  const std::string* Override(size_t actual_project)const{
   if(!frozen_||actual_project>=projects_)throw std::invalid_argument("Native project output selection lacks the frozen actual catalog");
   const auto found=roots_.find(actual_project);return found==roots_.end()?nullptr:&found->second;
  }
  std::filesystem::path Select(std::optional<size_t> actual_project,const std::filesystem::path& native_fallback)const{
   if(!frozen_||(!actual_project&&projects_))throw std::invalid_argument("Native project output selection lacks actual attempt membership");
   if(actual_project)if(const auto* root=Override(*actual_project))return *root;
   // The fallback is the existing native global/root-derived value, validated
   // independently before IO; this class never manufactures directory receipts.
   return NativeOutputDirectoryRoot(native_fallback.string());
  }
  const std::map<size_t,std::string>& Declarations()const{if(!frozen_)throw std::invalid_argument("Native project output declarations are not frozen");return roots_;}
 private:std::map<size_t,std::string> roots_;size_t projects_=0;bool frozen_=false;
};
} // namespace suitecut::runner
