// SPDX-License-Identifier: MIT
#pragma once
#include "grep_title.h"
#include "grep_expression.h"
#include "suite_file_owner.h"
#include <dirent.h>
#include <functional>
#include <memory>

namespace suitecut::runner {
// Match configuration is data. Only native enumeration creates file owners;
// the host never sends a selected pathname or performs the directory walk.
struct SuitePathPattern {
  enum class Kind {Glob,Regex};
  Kind kind=Kind::Glob;
  std::u16string source;
  bool insensitive=false;std::string flags={};
};
class SuitePathMatcher {
 public:
  explicit SuitePathMatcher(const SuitePathPattern& pattern)
      :compiled_(pattern.kind==SuitePathPattern::Kind::Regex?pattern.source:Glob(pattern.source),pattern.flags.empty()&&pattern.insensitive?"i":pattern.flags){}
  bool Matches(std::u16string_view path,GrepPattern::Work& work)const{return compiled_.Matches(path,work);}
 private:
  static std::u16string Glob(std::u16string_view source){
    if(source.empty()||source.size()>2048)throw std::invalid_argument("Native discovery glob requires1..2048 UTF16 units");
    std::u16string result=u"^";
    // Relative globs match the absolute path below any directory, as documented
    // for testMatch/testIgnore. A leading slash is already absolute.
    if(source.front()!=u'/')result+=u"([^]*/)?";
    unsigned braces=0;
    for(size_t at=0;at<source.size();++at){const auto c=source[at];
      if(c==u'\\'){
        if(++at==source.size())throw std::invalid_argument("Native discovery glob has a trailing escape");
        Literal(result,source[at]);
      }else if((c==u'@'||c==u'+'||c==u'!'||c==u'?'||c==u'*')&&at+1<source.size()&&source[at+1]==u'(')
        throw std::invalid_argument("Native discovery extended glob syntax is not yet supported; use braces or a regex");
      else if(c==u'*'){
        if(at+1<source.size()&&source[at+1]==u'*'){
          ++at;
          if((at==1||source[at-2]==u'/')&&(at+1==source.size()||source[at+1]==u'/')){
            if(at+1<source.size()){++at;result+=u"([^]*/)?";}else result+=u"[^]*";
          }else result+=u"[^/]*";
        }else result+=u"[^/]*";
      }else if(c==u'?')result+=u"[^/]";
      else if(c==u'{'){if(++braces>8)throw std::invalid_argument("Native discovery glob nesting capacity8 exceeded");result+=u'(';}
      else if(c==u'}'){if(!braces--)throw std::invalid_argument("Native discovery glob unmatched brace");result+=u')';}
      else if(c==u','&&braces)result+=u'|';
      else if(c==u'['){
        size_t end=at+1;for(;end<source.size()&&source[end]!=u']';++end){}
        if(end==source.size()||end==at+1)throw std::invalid_argument("Native discovery glob invalid character class");
        result+=u'[';size_t start=at+1;if(source[start]==u'!'){result+=u'^';++start;}
        for(size_t i=start;i<end;++i){if(source[i]==u'/'||source[i]==u'\\')throw std::invalid_argument("Native discovery glob class cannot contain slash/escape");result+=source[i];}
        result+=u']';at=end;
      }else Literal(result,c);
      if(result.size()>GrepPattern::kPatternUnits-1)throw std::invalid_argument("Native discovery compiled glob capacity exceeded");
    }
    if(braces)throw std::invalid_argument("Native discovery glob unmatched brace");
    result+=u'$';return result;
  }
  static void Literal(std::u16string& output,char16_t c){
    if(std::u16string_view(u".^$|()[]{}*+?\\").find(c)!=std::u16string_view::npos)output+=u'\\';
    output+=c;
  }
  GrepExpression compiled_;
};
struct SuiteDiscoveryOptions {
  std::string directory;
  // nullopt selects the documented .spec/.test extension family; an explicitly
  // empty list matches no files and is distinct from omitted configuration.
  std::optional<std::vector<SuitePathPattern>> match;
  std::vector<SuitePathPattern> ignore;
  std::vector<std::u16string> file_filters;
};
struct DiscoveredSuite {
  std::string relative_path;
  std::unique_ptr<SuiteFileOwner> owner;
};
class SuiteDiscovery {
 public:
  static constexpr size_t kFiles=128,kEntries=32768,kDepth=64,kPathBytes=16384,kTotalPathBytes=2*1024*1024,kPatterns=128;
  // check is the outer coordinator's original deadline/interruption check. It
  // must not create or renew a budget on individual paths or directories.
  static std::vector<DiscoveredSuite> Collect(const SuiteDiscoveryOptions& options,const std::function<void()>& check){
    SuiteDiscovery discovery(options,check);discovery.Walk(discovery.root_fd_.value,"",0);return std::move(discovery.files_);
  }
  static bool DefaultMatch(std::string_view name){
    const auto dot=name.rfind('.');if(dot==name.npos)return false;
    const auto extension=name.substr(dot+1);
    if(extension!="js"&&extension!="jsx"&&extension!="ts"&&extension!="tsx"&&extension!="mjs"&&extension!="mjsx"&&extension!="mts"&&extension!="mtsx"&&extension!="cjs"&&extension!="cjsx"&&extension!="cts"&&extension!="ctsx")return false;
    const auto stem=name.substr(0,dot);return stem.ends_with(".spec")||stem.ends_with(".test");
  }
 private:
  struct FD {int value=-1;explicit FD(int fd):value(fd){}~FD(){if(value>=0)::close(value);}FD(const FD&)=delete;};
  struct Directory {DIR* value=nullptr;explicit Directory(int fd):value(::fdopendir(fd)){if(!value){const auto error=errno;::close(fd);throw std::system_error(error,std::generic_category(),"Native discovery fdopendir failed");}}~Directory(){::closedir(value);}};
  SuiteDiscovery(const SuiteDiscoveryOptions& options,const std::function<void()>& check)
      :options_(options),check_(check),root_(Root(options.directory)),root_fd_(::open(root_.c_str(),O_RDONLY|O_CLOEXEC|O_DIRECTORY|O_NOFOLLOW)){
    if(root_fd_.value<0)throw std::system_error(errno,std::generic_category(),"Native discovery root open failed");
    if(options.ignore.size()>kPatterns||(options.match&&options.match->size()>kPatterns)||options.file_filters.size()>kPatterns)
      throw std::invalid_argument("Native discovery pattern capacity128 exceeded");
    work_.check_stopped=check_;
    if(options.match)for(const auto& pattern:*options.match){Check();match_.emplace_back(pattern);}
    for(const auto& pattern:options.ignore){Check();ignore_.emplace_back(pattern);}
    for(const auto& pattern:options.file_filters){Check();filters_.push_back(CompileCLIGrep(pattern));}
  }
  static std::string Root(const std::string& path){
    if(path.empty()||path.front()!='/'||path.size()>kPathBytes||path.find('\0')!=path.npos)throw std::invalid_argument("Native discovery requires a bounded absolute testDir");
    auto canonical=std::filesystem::canonical(path).native();
    if(canonical.size()>kPathBytes)throw std::invalid_argument("Native discovery canonical testDir exceeds capacity");
    (void)GrepUTF8(canonical);return canonical;
  }
  void Check(){if(check_)check_();}
  void Walk(int parent,const std::string& relative,size_t depth){
    Check();if(depth>kDepth)throw std::invalid_argument("Native discovery directory depth64 exceeded");
    const int copy=::fcntl(parent,F_DUPFD_CLOEXEC,16);if(copy<0)throw std::system_error(errno,std::generic_category(),"Native discovery directory copy failed");
    Directory directory(copy);std::vector<std::string> names;
    for(;;){Check();errno=0;const auto* entry=::readdir(directory.value);if(!entry){if(errno)throw std::system_error(errno,std::generic_category(),"Native discovery readdir failed");break;}
      const std::string name(entry->d_name);if(name=="."||name=="..")continue;
      if(++entries_>kEntries)throw std::invalid_argument("Native discovery entry capacity32768 exceeded");
      if(name.size()>kTotalPathBytes-name_bytes_)throw std::invalid_argument("Native discovery directory-name capacity exceeded");
      name_bytes_+=name.size();names.push_back(name);
    }
    std::sort(names.begin(),names.end());
    for(const auto& name:names){Check();const auto child=relative.empty()?name:relative+"/"+name;const auto absolute=(root_=="/"?root_:root_+"/")+child;
      if(absolute.size()>kPathBytes)throw std::invalid_argument("Native discovery path capacity16384 exceeded");
      const auto units=GrepUTF8(absolute);struct stat info{};
      if(::fstatat(parent,name.c_str(),&info,AT_SYMLINK_NOFOLLOW)<0)throw std::system_error(errno,std::generic_category(),"Native discovery entry stat failed");
      if(S_ISDIR(info.st_mode)){
        if(name=="node_modules")continue;
        FD opened(::openat(parent,name.c_str(),O_RDONLY|O_CLOEXEC|O_DIRECTORY|O_NOFOLLOW));
        if(opened.value<0)throw std::system_error(errno,std::generic_category(),"Native discovery child directory open failed");
        struct stat held{};if(::fstat(opened.value,&held)<0||held.st_dev!=info.st_dev||held.st_ino!=info.st_ino)throw std::runtime_error("Native discovery directory identity changed");
        Walk(opened.value,child,depth+1);
        if(::fstatat(parent,name.c_str(),&held,AT_SYMLINK_NOFOLLOW)<0||!S_ISDIR(held.st_mode)||held.st_dev!=info.st_dev||held.st_ino!=info.st_ino)throw std::runtime_error("Native discovery traversed directory was replaced");
      }else if(S_ISREG(info.st_mode)&&Matches(units,name)){
        if(files_.size()>=kFiles)throw std::invalid_argument("Native discovery selected file capacity128 exceeded");
        if(absolute.size()>kTotalPathBytes-path_bytes_)throw std::invalid_argument("Native discovery retained path capacity2MiB exceeded");
        auto owner=std::make_unique<SuiteFileOwner>(files_.size()+1,absolute);
        if(owner->Token().device!=static_cast<uint64_t>(info.st_dev)||owner->Token().inode!=static_cast<uint64_t>(info.st_ino)||owner->Path()!=absolute)
          throw std::runtime_error("Native discovery selected file identity changed");
        path_bytes_+=absolute.size();files_.push_back({child,std::move(owner)});
      }
      // Symlinks/special entries never become suite imports in this bounded
      // no-follow slice. There is no claim of symlink traversal parity.
    }
  }
  bool Matches(std::u16string_view path,std::string_view name){
    if(options_.match){if(std::none_of(match_.begin(),match_.end(),[&](const auto& pattern){return pattern.Matches(path,work_);}))return false;}
    else if(!DefaultMatch(name))return false;
    if(std::any_of(ignore_.begin(),ignore_.end(),[&](const auto& pattern){return pattern.Matches(path,work_);}))return false;
    return filters_.empty()||std::any_of(filters_.begin(),filters_.end(),[&](const auto& pattern){return pattern.Matches(path,work_);});
  }
  const SuiteDiscoveryOptions& options_;const std::function<void()>& check_;std::string root_;FD root_fd_;
  std::vector<SuitePathMatcher> match_,ignore_;std::vector<GrepExpression> filters_;GrepPattern::Work work_;
  std::vector<DiscoveredSuite> files_;size_t entries_=0,name_bytes_=0,path_bytes_=0;
};
}
