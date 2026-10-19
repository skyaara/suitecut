// SPDX-License-Identifier: MIT
#pragma once
#include "json_text.h"
#include "report_merge_json.h"
#include <map>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <stdexcept>
namespace suitecut::runner {
// Pure path policy, not an output/file receipt. Facts must come from the actual
// native module, selected project and test declaration at callback admission.
struct NativeSnapshotSettings {
  std::optional<std::string> test_dir,snapshot_dir,path_template,screenshot_template,aria_template;
};
struct NativeSnapshotFacts {
  std::filesystem::path module;
  std::string project_name;
  std::vector<std::string> title_path;
};
struct NativeSnapshotPart {
  enum class Kind {Literal,Argument,Extension,SnapshotSuffix};
  Kind kind=Kind::Literal;std::string value;
  bool operator==(const NativeSnapshotPart&)const=default;
};
struct NativeSnapshotPlan {
  std::string base_directory,snapshot_directory;
  std::vector<NativeSnapshotPart> snapshot,screenshot,aria;
  bool common_kind_plan=false;
  bool mutable_suffix=false;
};
class NativeSnapshotPaths {
 public:
  static constexpr size_t kPathBytes=7900,kTemplateBytes=32768,kConfigBytes=32000,kParts=256;
  static std::string Platform(){
#if defined(_WIN32)
    return "win32";
#elif defined(__APPLE__)
    return "darwin";
#elif defined(__linux__)
    return "linux";
#else
    throw std::invalid_argument("Native snapshot platform is unsupported");
#endif
  }
  static void Validate(const NativeSnapshotSettings&settings){
    for(const auto* path:{&settings.test_dir,&settings.snapshot_dir})if(*path)CheckPath(**path);
    for(const auto* text:{&settings.path_template,&settings.screenshot_template,&settings.aria_template})if(*text)CheckText(**text,kConfigBytes);
  }
  static bool NeedsTitle(const NativeSnapshotSettings&settings){
    for(const auto* text:{&settings.path_template,&settings.screenshot_template,&settings.aria_template})if(*text){
      for(size_t index=0;index<(**text).size();){
        const auto begin=(**text).find('{',index);if(begin==std::string::npos)break;
        const auto end=(**text).find('}',begin+1);if(end==std::string::npos)break;
        const auto name=std::string_view(**text).substr(begin+1,end-begin-1);
        if(name=="testName"||(name.size()==9&&name.substr(1)=="testName"))return true;
        index=end+1;
      }
    }
    return false;
  }
  static std::string FilesystemText(const std::u16string&units){
    std::string raw="\"";constexpr char hex[]="0123456789abcdef";
    for(const auto unit:units){raw+="\\u";for(int shift=12;shift>=0;shift-=4)raw+=hex[(unit>>shift)&15];}raw+='"';
    auto value=merge_detail::JSONParser(raw).Parse();return merge_detail::Text(value);
  }
  static NativeSnapshotSettings Merge(NativeSnapshotSettings root,const NativeSnapshotSettings&project){
    if(project.test_dir)root.test_dir=project.test_dir;
    if(project.snapshot_dir)root.snapshot_dir=project.snapshot_dir;
    if(project.path_template)root.path_template=project.path_template;
    if(project.screenshot_template)root.screenshot_template=project.screenshot_template;
    if(project.aria_template)root.aria_template=project.aria_template;
    return root;
  }
  static NativeSnapshotPlan Directory(const NativeSnapshotFacts&facts,const NativeSnapshotSettings&settings={}){
    Validate(settings);
    if(!facts.module.is_absolute()||facts.module.filename().empty())
      throw std::invalid_argument("Native snapshot path lacks the actual module/test declaration");
    const auto module=facts.module.lexically_normal();CheckPath(module.string());
    const auto config=module.parent_path(),test=Root(config,settings.test_dir),snapshot=Root(config,settings.snapshot_dir.value_or(test.string()));
    const auto relative=module.lexically_relative(test);
    if(relative.empty()||relative.is_absolute()||*relative.begin()=="..")
      throw std::invalid_argument("Actual native test module is outside its declared testDir");
    NativeSnapshotPlan plan;plan.base_directory=config.string();
    plan.snapshot_directory=(snapshot/(relative.string()+"-snapshots")).lexically_normal().string();CheckPath(plan.snapshot_directory);
    return plan;
  }
  static NativeSnapshotPlan Bind(const NativeSnapshotFacts&facts,const NativeSnapshotSettings&settings={},bool mutable_suffix=false){
    auto plan=Directory(facts,settings);
    plan.mutable_suffix=mutable_suffix;
    if(facts.title_path.empty()||facts.title_path.size()>128)throw std::invalid_argument("Native snapshot path lacks actual test declaration");
    const auto module=facts.module.lexically_normal(),config=module.parent_path();
    const auto test=Root(config,settings.test_dir),snapshot=Root(config,settings.snapshot_dir.value_or(test.string()));
    const auto relative=module.lexically_relative(test);
    std::string title;
    if(NeedsTitle(settings))for(const auto&part:facts.title_path){CheckText(part,kTemplateBytes);if(title.size()+part.size()+1>kTemplateBytes)throw std::length_error("Native snapshot title metadata exceeds bound");if(!title.empty())title+='-';title+=part;}
    const Values values{config.string(),test.string(),snapshot.string(),relative.parent_path().string(),relative.string(),module.filename().string(),module.stem().string(),Segment(facts.project_name),title.empty()?std::string{}:Segment(title),Platform()};
    const std::string_view common=settings.path_template?std::string_view(*settings.path_template):std::string_view("{snapshotDir}/{testFilePath}-snapshots/{arg}{-projectName}{-snapshotSuffix}{ext}");
    const std::string_view screenshot=settings.screenshot_template?std::string_view(*settings.screenshot_template):common;
    const std::string_view aria=settings.aria_template?std::string_view(*settings.aria_template):settings.path_template?common:std::string_view("{snapshotDir}/{testFilePath}-snapshots/{arg}{ext}");
    plan.snapshot=Compile(common,values,mutable_suffix);
    if(screenshot==common&&aria==common)plan.common_kind_plan=true;
    else{
      plan.screenshot=Compile(screenshot,values,mutable_suffix);plan.aria=Compile(aria,values,mutable_suffix);
      if(plan.snapshot==plan.screenshot&&plan.snapshot==plan.aria){
        plan.common_kind_plan=true;
        std::vector<NativeSnapshotPart>().swap(plan.screenshot);std::vector<NativeSnapshotPart>().swap(plan.aria);
      }
    }
    return plan;
  }
  static std::string Unsupported(const std::string&reason,const std::optional<std::string>&directory,bool mutable_suffix=false){
    const auto paths="{\"version\":1,\"unsupported\":"+Quote(reason)+(directory?",\"snapshotDir\":"+Quote(*directory):"")+"}";
    return mutable_suffix?SuffixContext(paths):paths;
  }
  static std::string Serialize(const NativeSnapshotPlan&plan){
    const auto parts=[](const std::vector<NativeSnapshotPart>&values){
      std::string result="[";
      for(const auto&part:values){
        if(result.size()>1)result+=',';
        const auto kind=part.kind==NativeSnapshotPart::Kind::Literal?"literal":part.kind==NativeSnapshotPart::Kind::Argument?"arg":part.kind==NativeSnapshotPart::Kind::Extension?"ext":"suffix";
        result+="{\"kind\":"+Quote(kind)+",\"value\":"+Quote(part.value)+"}";
      }
      return result+"]";
    };
    if(plan.common_kind_plan){
      if(plan.snapshot.empty()||!plan.screenshot.empty()||!plan.aria.empty())throw std::invalid_argument("Native common snapshot plan is inconsistent");
      const auto paths="{\"version\":2,\"baseDirectory\":"+Quote(plan.base_directory)+",\"snapshotDir\":"+Quote(plan.snapshot_directory)+",\"plans\":[{\"id\":1,\"parts\":"+parts(plan.snapshot)+"}],\"kinds\":{\"snapshot\":1,\"screenshot\":1,\"aria\":1}}";
      return plan.mutable_suffix?SuffixContext(paths):paths;
    }
    const auto paths="{\"version\":1,\"baseDirectory\":"+Quote(plan.base_directory)+",\"snapshotDir\":"+Quote(plan.snapshot_directory)+",\"snapshot\":"+parts(plan.snapshot)+",\"screenshot\":"+parts(plan.screenshot)+",\"aria\":"+parts(plan.aria)+"}";
    return plan.mutable_suffix?SuffixContext(paths):paths;
  }
 private:
  static std::string SuffixContext(const std::string&paths){return "{\"version\":3,\"suffix\":{\"initial\":"+Quote(Platform())+",\"maxBytes\":1024,\"policy\":\"filenameComponent\"},\"paths\":"+paths+"}";}
  struct Values {std::string config,test,snapshot,file_dir,file_path,file_name,file_base,project,title,platform;};
  static std::string Quote(const std::string&value){
    static constexpr char digits[]="0123456789abcdef";
    std::string json="\"";
    for(const unsigned char ch:value){
      if(ch=='"'||ch=='\\'){json+='\\';json+=char(ch);}
      else if(ch<32){json+="\\u00";json+=digits[ch>>4];json+=digits[ch&15];}
      else json+=char(ch);
    }
    json+='"';
    if(!JsonString(json,kTemplateBytes))throw std::invalid_argument("Native snapshot path policy is not bounded filesystem UTF8");
    return json;
  }
  static void CheckText(const std::string&value,size_t bound){
    if(value.size()>bound||value.find('\0')!=std::string::npos)throw std::invalid_argument("Native snapshot text exceeds its filesystem bound");
    (void)Quote(value);
  }
  static void CheckPath(const std::string&value){CheckText(value,kPathBytes);}
  static std::filesystem::path Root(const std::filesystem::path&config,const std::optional<std::string>&raw){
    if(!raw)return config;
    CheckPath(*raw);auto path=std::filesystem::path(*raw);path=(path.is_absolute()?path:config/path).lexically_normal();CheckPath(path.string());return path;
  }
  static std::string Segment(const std::string&raw){
    CheckText(raw,kTemplateBytes);std::string result;result.reserve(raw.size());bool separator=false;
    for(const unsigned char ch:raw){
      const bool safe=(ch>='a'&&ch<='z')||(ch>='A'&&ch<='Z')||(ch>='0'&&ch<='9')||ch=='_'||ch=='-'||ch>=128;
      if(safe){if(separator&&!result.empty()&&result.back()!='-')result+='-';separator=false;result+=char(ch);}else separator=true;
    }
    return result;
  }
  static std::optional<std::string> Value(std::string_view name,const Values&v){
    if(name=="configDir")return v.config;
    if(name=="testDir")return v.test;
    if(name=="snapshotDir")return v.snapshot;
    if(name=="testFileDir")return v.file_dir;
    if(name=="testFilePath")return v.file_path;
    if(name=="testFileName")return v.file_name;
    if(name=="testFileBaseName")return v.file_base;
    if(name=="projectName")return v.project;
    if(name=="testName")return v.title;
    if(name=="platform"||name=="snapshotSuffix")return v.platform;
    return {};
  }
  static std::vector<NativeSnapshotPart> Compile(std::string_view text,const Values&values,bool mutable_suffix){
    if(text.size()>kTemplateBytes)throw std::length_error("Native snapshot template exceeds bound");
    std::vector<NativeSnapshotPart>result;std::string literal;
    const auto add=[&](NativeSnapshotPart::Kind kind,std::string value){
      if(result.size()>=kParts)throw std::length_error("Native snapshot token plan exceeds bound");
      result.push_back({kind,std::move(value)});
    };
    const auto flush=[&]{if(!literal.empty()){add(NativeSnapshotPart::Kind::Literal,std::move(literal));literal.clear();}};
    size_t retained=0;
    for(size_t index=0;index<text.size();){
      if(text[index]!='{'){literal+=text[index++];continue;}
      const auto end=text.find('}',index+1);if(end==std::string::npos){literal+=text.substr(index);break;}
      auto name=std::string_view(text).substr(index+1,end-index-1);std::string prefix;auto value=mutable_suffix&&name=="snapshotSuffix"?std::nullopt:Value(name,values);
      if(!value&&name!="arg"&&name!="ext"&&!(mutable_suffix&&name=="snapshotSuffix")&&name.size()>1){prefix=name.substr(0,1);name.remove_prefix(1);value=mutable_suffix&&name=="snapshotSuffix"?std::nullopt:Value(name,values);}
      if(name=="arg"||name=="ext"||(mutable_suffix&&name=="snapshotSuffix")){
        flush();add(name=="arg"?NativeSnapshotPart::Kind::Argument:name=="ext"?NativeSnapshotPart::Kind::Extension:NativeSnapshotPart::Kind::SnapshotSuffix,std::move(prefix));
      }else if(value){if(!value->empty())literal+=prefix+*value;}
      else literal+=text.substr(index,end-index+1);
      index=end+1;
      retained=literal.size();for(const auto&part:result)retained+=part.value.size();
      if(retained>kTemplateBytes)throw std::length_error("Native snapshot expanded token plan exceeds bound");
    }
    retained=literal.size();for(const auto&part:result)retained+=part.value.size();
    if(retained>kTemplateBytes)throw std::length_error("Native snapshot expanded token plan exceeds bound");
    flush();if(result.empty())add(NativeSnapshotPart::Kind::Literal,"");return result;
  }
};
class NativeSnapshotDeclarations {
 public:
  void Declare(size_t owner,size_t projects,std::string_view field,std::string value){
    if(owner>projects||projects>128)throw std::invalid_argument("Native snapshot config has no actual project");
    const auto previous=owner?projects_.find(owner-1):projects_.end();
    auto candidate=previous==projects_.end()?(owner?NativeSnapshotSettings{}:root_):previous->second;
    auto* slot=Field(candidate,field);
    if(*slot)throw std::invalid_argument("Duplicate native snapshot config field");
    *slot=std::move(value);NativeSnapshotPaths::Validate(candidate);
    if(owner)projects_[owner-1]=std::move(candidate);else root_=std::move(candidate);
  }
  NativeSnapshotSettings Select(std::optional<size_t> project,size_t projects)const{
    if(projects>128||(project&&*project>=projects)||(!project&&projects))throw std::invalid_argument("Native snapshot actual project selection missing");
    const auto found=project?projects_.find(*project):projects_.end();
    return found==projects_.end()?root_:NativeSnapshotPaths::Merge(root_,found->second);
  }
 private:
  NativeSnapshotSettings root_;std::map<size_t,NativeSnapshotSettings>projects_;
  static std::optional<std::string>* Field(NativeSnapshotSettings&settings,std::string_view field){
    if(field=="testDir")return &settings.test_dir;
    if(field=="snapshotDir")return &settings.snapshot_dir;
    if(field=="snapshotPathTemplate")return &settings.path_template;
    if(field=="screenshotPathTemplate")return &settings.screenshot_template;
    if(field=="ariaPathTemplate")return &settings.aria_template;
    throw std::invalid_argument("Unknown native snapshot config field");
  }
};
} // namespace suitecut::runner
