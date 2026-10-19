// SPDX-License-Identifier: MIT
#pragma once
#include "grep_pattern.h"
#include "grep_vm.h"
namespace suitecut::runner {
  class GrepExpression {
    public: GrepExpression(std::u16string source,std::string flags={
    }
    ){
      GrepVM::Flags options;
      std::set<char> seen;
      for(const auto flag:flags){
        if(!seen.insert(flag).second)throw std::invalid_argument("Native grep duplicate RegExp flag");
        if(flag=='i')options.insensitive=true;
        else if(flag=='m')options.multiline=true;
        else if(flag=='s')options.dot_all=true;
        else if(flag=='y')options.sticky=true;
        else if(flag=='u')options.unicode=true;
        else if(flag=='v')options.unicode_sets=true;
        else if(flag!='g'&&flag!='d')throw std::invalid_argument("Native grep invalid RegExp flag");
      }
      if(options.unicode&&options.unicode_sets)throw std::invalid_argument("Native grep u/v flags are mutually exclusive");
      vm_.emplace(source,options);
      if(vm_->CanUseNFA()){
        nfa_.emplace(std::move(source),GrepPattern::Options{
          options.insensitive,options.multiline,options.dot_all,options.sticky
        }
        );
        vm_.reset();
      }
    }
    bool Matches(std::u16string_view title,GrepWork& work)const{
      return nfa_?nfa_->Matches(title,work):vm_->Matches(title,work);
    }
    size_t StateCount()const{
      return nfa_?nfa_->StateCount():vm_->StateCount();
    }
    size_t RangeCount()const{
      return nfa_?nfa_->RangeCount():vm_->RangeCount();
    }
    bool UsesVM()const{
      return vm_.has_value();
    }
    private:std::optional<GrepPattern> nfa_;
    std::optional<GrepVM> vm_;
  }
  ;
  inline GrepExpression CompileCLIGrep(std::u16string argument){
    if(argument.size()>4096)throw std::invalid_argument("Native grep CLI argument capacity4096 exceeded");
    std::string flags="gi";
    if(!argument.empty()&&argument.front()=='/'){
      const auto slash=argument.rfind('/');
      if(slash>0&&slash!=std::u16string::npos){
        const auto suffix=argument.substr(slash+1);
        bool recognized=true;
        for(const auto flag:suffix)if(flag!='g'&&flag!='i')recognized=false;
        if(recognized){
          flags.assign(suffix.begin(),suffix.end());
          argument=argument.substr(1,slash-1);
        }
      }
    }
    return GrepExpression(std::move(argument),std::move(flags));
  }
}
