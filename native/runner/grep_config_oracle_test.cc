// SPDX-License-Identifier: MIT
#include "grep_configuration.h"
#include "grep_title.h"
#include <fstream>
#include <iostream>
namespace md=suitecut::runner::merge_detail;
namespace sr=suitecut::runner;
std::optional<std::vector<sr::GrepRegExp>> Patterns(const md::JSON& value){
  if(value.kind==md::JSON::Null)return{
  }
  ;
  std::vector<sr::GrepRegExp> result;
  const auto add=[&](const md::JSON& declaration){
    result.push_back({
      sr::GrepUnits(md::Get(declaration,"source").raw),md::Text(md::Get(declaration,"flags"))
    }
    );
  }
  ;
  if(value.kind==md::JSON::Array)for(const auto& declaration:md::Array(value))add(declaration);
  else add(value);
  return result;
}
int main(int argc,char** argv){
  if(argc!=2)throw std::invalid_argument("native config grep oracle requires one pinned fixture path");
  std::ifstream file(argv[1],std::ios::binary);
  if(!file)throw std::invalid_argument("native config grep oracle missing");
  std::string bytes;
  char c;
  while(file.get(c)){
    if(bytes.size()>=1024*1024)throw std::invalid_argument("config grep oracle capacity1MiB exceeded");
    bytes+=c;
  }
  const auto fixture=md::JSONParser(bytes).Parse();
  unsigned cases=0,comparisons=0;
  for(const auto& row:md::Array(md::Get(fixture,"cases"))){
    sr::GrepConfigDeclarations declarations;
    std::vector<std::string> names;
    for(const auto& project:md::Array(md::Get(row,"effectiveProjects"))){
      names.push_back(md::Text(md::Get(project,"name")));
      declarations.projects[names.size()-1]={
        Patterns(md::Get(project,"grep")),Patterns(md::Get(project,"grepInvert"))
      }
      ;
    }
    sr::GrepConfiguration config(declarations);
    sr::GrepPolicy cli;
    const auto& include=md::Get(row,"includeCLI");
    const auto& exclude=md::Get(row,"excludeCLI");
    if(include.kind!=md::JSON::Null)cli.include_argument=md::Text(include);
    if(exclude.kind!=md::JSON::Null)cli.exclude_argument=md::Text(exclude);
    cli.Compile();
    std::set<std::pair<std::string,std::string>> expected,actual;
    for(const auto& item:md::Array(md::Get(row,"expectedSelected"))){
      const auto& pair=md::Array(item);
      expected.emplace(md::Text(pair.at(0)),pair.at(1).raw);
    }
    sr::GrepWork work;
    for(size_t project=0;
    project<names.size();
    ++project)for(const auto& title:md::Array(md::Get(fixture,"titles"))){
      const auto composed=sr::GrepUTF8(names[project])+u" "+sr::GrepUTF8(md::Text(md::Get(fixture,"file")))+u" "+sr::GrepUnits(title.raw);
      if(config.Matches(composed,project,work)&&(!cli.Active()||cli.Matches(composed,work)))actual.emplace(names[project],title.raw);
      ++comparisons;
    }
    if(actual!=expected)throw std::runtime_error("native config grep stock mismatch: "+md::Text(md::Get(row,"name")));
    ++cases;
  }
  std::cout<<"Native config/grammar grep stock public contract "<<cases<<" cases / "<<comparisons<<" comparisons PASS\n";
}
