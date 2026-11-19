// SPDX-License-Identifier: MIT
#pragma once
#include "grep_expression.h"
#include <map>
#include <memory>
#include <set>
namespace suitecut::runner {
  struct GrepRegExp {
    std::u16string source;
    std::string flags;
  }
  ;
  struct GrepConfigLayer {
    // Absent inherits; an explicitly empty array overrides the parent field.
    std::optional<std::vector<GrepRegExp>> include,exclude;
  }
  ;
  struct GrepConfigDeclarations {
    GrepConfigLayer root;
    std::map<size_t,GrepConfigLayer> projects;
    bool Active()const{
      if(root.include||root.exclude)return true;
      for(const auto& [project,layer]:projects){
        (void)project;
        if(layer.include||layer.exclude)return true;
      }
      return false;
    }
  }
  ;
  inline GrepExpression CompileConfigGrep(const GrepRegExp& declaration){
    return GrepExpression(declaration.source,declaration.flags);
  }
  class GrepConfiguration {
    using Patterns=std::vector<std::shared_ptr<const GrepExpression>>;
    struct Layer {
      std::optional<Patterns> include,exclude;
    }
    ;
    public: explicit GrepConfiguration(const GrepConfigDeclarations& declarations){
      root_=Compile(declarations.root);
      for(const auto& [project,layer]:declarations.projects)projects_.emplace(project,Compile(layer));
    }
    bool Matches(std::u16string_view title,std::optional<size_t> project,GrepPattern::Work& work)const{
      const Layer* layer=nullptr;
      if(project){
        const auto found=projects_.find(*project);
        if(found!=projects_.end())layer=&found->second;
      }
      const auto& include=layer&&layer->include?layer->include:root_.include;
      const auto& exclude=layer&&layer->exclude?layer->exclude:root_.exclude;
      return(!include||Any(*include,title,work))&&(!exclude||!Any(*exclude,title,work));
    }
    private: static bool Any(const Patterns& patterns,std::u16string_view title,GrepPattern::Work& work){
      for(const auto& pattern:patterns)if(pattern->Matches(title,work))return true;
      return false;
    }
    std::optional<Patterns> Compile(const std::optional<std::vector<GrepRegExp>>& declarations){
      if(!declarations)return std::nullopt;
      Patterns result;
      for(const auto& declaration:*declarations){
        result.push_back(std::make_shared<const GrepExpression>(CompileConfigGrep(declaration)));
      }
      return result;
    }
    Layer Compile(const GrepConfigLayer& declarations){
      return{
        Compile(declarations.include),Compile(declarations.exclude)
      }
      ;
    }
    Layer root_;
    std::map<size_t,Layer> projects_;
  }
  ;
}
