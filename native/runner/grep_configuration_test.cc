// SPDX-License-Identifier: MIT
#include "grep_configuration.h"
#include <iostream>
namespace sr=suitecut::runner;
namespace {
  unsigned checks=0;
  void Check(bool value){
    ++checks;
    if(!value)throw std::runtime_error("native grep config control failed "+std::to_string(checks));
  }
  using Rows=std::vector<sr::GrepRegExp>;
  bool Matches(const sr::GrepConfiguration& config,std::u16string_view title,std::optional<size_t> project={
  }
  ){
    sr::GrepWork work;
    return config.Matches(title,project,work);
  }
}
int main(){
  sr::GrepConfigDeclarations declarations;
  Check(!declarations.Active());
  declarations.root.include=Rows{
    {
      u"AA$",""
    }
    ,{
      u"BB$",""
    }
  }
  ;
  declarations.root.exclude=Rows{
    {
      u"^skip",""
    }
  }
  ;
  Check(declarations.Active());
  sr::GrepConfiguration root(declarations);
  Check(Matches(root,u"AA"));
  Check(Matches(root,u"BB"));
  Check(!Matches(root,u"CC"));
  Check(!Matches(root,u"skip AA"));
  declarations.projects[0].include=Rows{
    {
      u"CC$",""
    }
  }
  ;
  sr::GrepConfiguration replaced(declarations);
  Check(Matches(replaced,u"CC",0));
  Check(!Matches(replaced,u"AA",0));
  Check(!Matches(replaced,u"skip CC",0));
  Check(Matches(replaced,u"AA",1));
  declarations.projects[0].exclude=Rows{
  }
  ;
  sr::GrepConfiguration clear(declarations);
  Check(Matches(clear,u"skip CC",0));
  Check(!Matches(clear,u"skip AA",1));
  declarations.projects[0].include=Rows{
  }
  ;
  sr::GrepConfiguration empty(declarations);
  Check(!Matches(empty,u"CC",0));
  Check(Matches(empty,u"AA",1));
  declarations.root.include.reset();
  declarations.root.exclude=Rows{
  }
  ;
  declarations.projects.clear();
  sr::GrepConfiguration any(declarations);
  Check(Matches(any,u"anything"));
  declarations.root.include=Rows{
    {
      u"π$","i"
    }
  }
  ;
  sr::GrepConfiguration greek(declarations);
  Check(Matches(greek,u"Π"));
  Check(!Matches(greek,u"P"));
  declarations.root.include=Rows{
    {
      u"AA$","g"
    }
  }
  ;
  sr::GrepConfiguration global(declarations);
  Check(Matches(global,u"AA"));
  Check(Matches(global,u"AA"));
  declarations.root.include=Rows{
    {
      u"p1","y"
    }
  }
  ;
  sr::GrepConfiguration sticky(declarations);
  Check(Matches(sticky,u"p1 file test"));
  Check(!Matches(sticky,u"p2 p1 test"));
  declarations.root.include=Rows{
    {
      u"^(?<x>a+)b\\k<x>$",""
    }
  }
  ;
  sr::GrepConfiguration capture(declarations);
  Check(Matches(capture,u"aabaa"));
  Check(!Matches(capture,u"aaba"));
  std::cout<<"Native config grep inheritance/empty/OR/RegExp "<<checks<<" controls PASS\n";
}
