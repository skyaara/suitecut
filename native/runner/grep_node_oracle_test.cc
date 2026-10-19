// SPDX-License-Identifier: MIT
#include "grep_expression.h"
#include "grep_title.h"
#include <fstream>
#include <iostream>
namespace md=suitecut::runner::merge_detail;
namespace sr=suitecut::runner;
int main(int argc,char** argv){
  if(argc!=2)throw std::invalid_argument("native RegExp oracle requires one pinned genuine Node fixture");
  std::ifstream input(argv[1],std::ios::binary);
  if(!input)throw std::invalid_argument("native RegExp oracle fixture missing");
  std::string bytes;
  char c;
  while(input.get(c)){
    if(bytes.size()>=8*1024*1024)throw std::invalid_argument("native RegExp oracle capacity8MiB exceeded");
    bytes+=c;
  }
  const auto fixture=md::JSONParser(bytes).Parse();
  std::vector<std::u16string> titles;
  for(const auto& title:md::Array(md::Get(fixture,"titles")))titles.push_back(sr::GrepUnits(title.raw,32768));
  size_t patterns=0,comparisons=0,negative=0,mismatches=0;
  for(const auto& row:md::Array(md::Get(fixture,"patterns"))){
    const auto source=sr::GrepUnits(md::Get(row,"source").raw);
    const auto flags=md::Text(md::Get(row,"flags"));
    std::optional<sr::GrepExpression> expression;
    std::string error;
    try{
      expression.emplace(source,flags);
    }
    catch(const std::invalid_argument& failure){
      error=failure.what();
    }
    const bool invalid=md::Get(row,"compileError").kind!=md::JSON::Null;
    if(invalid==expression.has_value()){if(++mismatches<=100)std::cerr<<"native/Node compilation mismatch source="<<sr::GrepJSON(source)<<" flags="<<flags<<" native="<<error<<"\n";continue;}
    ++patterns;
    if(invalid){
      ++negative;
      continue;
    }
    const auto* compile_only=md::Find(row,"compileOnly");
    if(compile_only&&compile_only->raw=="true")continue;
    std::set<size_t> selected;
    for(const auto& index:md::Array(md::Get(row,"selected")))selected.insert(md::Integer(index));
    for(size_t index=0;
    index<titles.size();
    ++index){
      sr::GrepWork work;
      const bool actual=expression->Matches(titles[index],work);
      if(actual!=selected.contains(index)){if(++mismatches<=100)std::cerr<<"native/Node boolean mismatch source="<<sr::GrepJSON(source)<<" flags="<<flags<<" title="<<sr::GrepJSON(titles[index])<<" actual="<<actual<<" expected="<<selected.contains(index)<<"\n";}
      ++comparisons;
    }
  }
  if(mismatches){std::cerr<<mismatches<<" native/Node mismatches\n";return 1;}
  std::cout<<"Native genuine Node RegExp parity "<<patterns<<" patterns / "<<comparisons<<" comparisons / "<<negative<<" actual syntax errors PASS\n";
}
