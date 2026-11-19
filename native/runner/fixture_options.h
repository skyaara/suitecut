// SPDX-License-Identifier: MIT
#pragma once
#include "fixture_catalog.h"
#include "json_text.h"
#include <charconv>
#include <cmath>
namespace suitecut::runner {
// Serialized declaration data, never an executable callback or a file receipt.
struct FixtureLiteral {
  enum class Kind { Undefined, JSON };
  Kind kind=Kind::Undefined;std::string json;
  bool operator==(const FixtureLiteral&)const=default;
  bool Valid()const{
    if(kind==Kind::Undefined)return json.empty();
    if(kind!=Kind::JSON||json.empty())return false;
    struct Parser {
      std::string_view text;size_t at=0;
      void Space(){while(at<text.size()&&(text[at]==' '||text[at]=='\t'||text[at]=='\r'||text[at]=='\n'))++at;}
      bool String(){const auto begin=at;if(at>=text.size()||text[at++]!='"')return false;
        while(at<text.size()){const auto c=text[at++];if(c=='"')return JsonString(text.substr(begin,at-begin));if(c=='\\'){if(at==text.size())return false;++at;}}return false;}
      bool Literal(std::string_view value){if(text.substr(at,value.size())!=value)return false;at+=value.size();return true;}
      bool Number(){const auto begin=at;if(at<text.size()&&text[at]=='-')++at;if(at==text.size())return false;
        if(text[at]=='0')++at;else{if(text[at]<'1'||text[at]>'9')return false;while(at<text.size()&&text[at]>='0'&&text[at]<='9')++at;}
        if(at<text.size()&&text[at]=='.'){++at;const auto digits=at;while(at<text.size()&&text[at]>='0'&&text[at]<='9')++at;if(at==digits)return false;}
        if(at<text.size()&&(text[at]=='e'||text[at]=='E')){++at;if(at<text.size()&&(text[at]=='+'||text[at]=='-'))++at;const auto digits=at;while(at<text.size()&&text[at]>='0'&&text[at]<='9')++at;if(at==digits)return false;}
        double actual=0;const auto parsed=std::from_chars(text.data()+begin,text.data()+at,actual,std::chars_format::general);return parsed.ec==std::errc{}&&parsed.ptr==text.data()+at&&std::isfinite(actual);
      }
      // Iterative: nesting depth and node count are bounded only by memory.
      bool Member(){if(!String())return false;Space();return at<text.size()&&text[at++]==':';}
      bool Value(){std::vector<char> open;
        for(;;){Space();if(at>=text.size())return false;const auto first=text[at];
          if(first=='{'||first=='['){const bool object=first=='{';const auto close=object?'}':']';++at;Space();
            if(at<text.size()&&text[at]==close)++at;
            else{open.push_back(close);if(object&&!Member())return false;continue;}
          }
          else if(first=='"'){if(!String())return false;}
          else if(first=='t'){if(!Literal("true"))return false;}
          else if(first=='f'){if(!Literal("false"))return false;}
          else if(first=='n'){if(!Literal("null"))return false;}
          else if(!Number())return false;
          for(;;){if(open.empty())return true;Space();if(at>=text.size())return false;const auto next=text[at++];
            if(next==open.back()){open.pop_back();continue;}
            if(next!=',')return false;Space();if(open.back()=='}'&&!Member())return false;break;}
        }
      }
    } parser{json};
    if(!parser.Value())return false;
    parser.Space();return parser.at==json.size();
  }
};
struct FixtureValueDeclaration {uint64_t declaration=0;std::optional<bool> option;std::optional<FixtureLiteral> literal;};
struct FixtureOptionOverride {std::string name;FixtureLiteral value;};
// Pure native catalog selection. FixtureOwners still requires actual host
// SETUP/use/READY/TEARDOWN callbacks for constants; no receipts are synthesized.
class FixtureOptionCatalog {
 public:
  enum class Origin { Callback, DeclaredLiteral, ConfiguredLiteral };
  struct Source {uint64_t declaration=0;bool option=false;Origin origin=Origin::Callback;std::optional<FixtureLiteral> constant;std::vector<std::string> original_dependencies;};
  FixtureOptionCatalog(std::vector<FixtureCatalog::Layer> layers,std::vector<FixtureValueDeclaration> values={},std::vector<FixtureOptionOverride> options={}):raw_(std::move(layers)){
    if(raw_.empty())throw std::invalid_argument("Native fixture value catalog requires a layer");
    std::map<uint64_t,FixtureValueDeclaration> declared;
    for(auto& value:values){if(!value.declaration||!declared.emplace(value.declaration,value).second)throw std::invalid_argument("Invalid native fixture value declaration identity");if(value.literal)Validate(*value.literal);}
    std::map<std::string,uint64_t> effective;uint64_t expected=0;
    for(const auto& layer:raw_){const auto previous=effective;std::set<std::string> names;
      for(const auto& fixture:layer){if(fixture.id!=++expected||!FixtureCatalog::Name(fixture.name)||!names.insert(fixture.name).second)throw std::invalid_argument("Invalid native fixture option declaration identity/name");
        Source source;source.declaration=fixture.id;source.original_dependencies=fixture.dependencies;
        if(const auto old=previous.find(fixture.name);old!=previous.end())source.option=Get(old->second).option;
        if(const auto record=declared.find(fixture.id);record!=declared.end()){
          if(record->second.option){if(source.option&&!*record->second.option)throw std::invalid_argument("Native fixture option:true cannot be overridden as false: "+fixture.name);source.option=*record->second.option;}
          if(record->second.literal){if(!fixture.dependencies.empty())throw std::invalid_argument("Native literal fixture cannot declare callback dependencies: "+fixture.name);source.origin=Origin::DeclaredLiteral;source.constant=record->second.literal;}
        }
        sources_.push_back(std::move(source));effective[fixture.name]=fixture.id;
      }
    }
    for(const auto&[id,value]:declared){(void)value;if(id>sources_.size())throw std::invalid_argument("Native fixture value has no actual declaration");}
    auto cooked=raw_;std::map<uint64_t,FixtureDeclaration*> by_id;for(auto& layer:cooked)for(auto& fixture:layer)by_id.emplace(fixture.id,&fixture);
    std::set<std::string> configured;
    for(const auto& option:options){if(!FixtureCatalog::Name(option.name)||!configured.insert(option.name).second)throw std::invalid_argument("Invalid or repeated native fixture option override");const auto target=effective.find(option.name);if(target==effective.end())throw std::invalid_argument("Unknown native fixture option: "+option.name);
      auto& source=sources_.at(target->second-1);if(!source.option)throw std::invalid_argument("Native configuration can override only option fixtures: "+option.name);
      Validate(option.value);source.origin=Origin::ConfiguredLiteral;source.constant=option.value;by_id.at(target->second)->dependencies.clear();
    }
    graph_.emplace(std::move(cooked));
  }
  const FixtureCatalog& Graph()const{return graph_.value();}
  const std::vector<FixtureCatalog::Layer>& RawLayers()const{return raw_;}
  const Source& Get(uint64_t declaration)const{if(!declaration||declaration>sources_.size())throw std::invalid_argument("Unknown native fixture value declaration");return sources_[declaration-1];}
 private:
  static void Validate(const FixtureLiteral& value){if(!value.Valid())throw std::invalid_argument("Invalid native fixture literal value");}
  std::vector<FixtureCatalog::Layer> raw_;std::vector<Source> sources_;std::optional<FixtureCatalog> graph_;
};
}
