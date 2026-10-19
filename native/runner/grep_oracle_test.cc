// SPDX-License-Identifier: MIT
#include "grep_title.h"
#include <fstream>
#include <iostream>
#include <iterator>
namespace md=suitecut::runner::merge_detail;
using namespace suitecut::runner;
int main(int argc,char** argv){
  if(argc!=2)throw std::invalid_argument("native grep oracle control requires one pinned fixture path");
  std::ifstream file(argv[1],std::ios::binary);if(!file)throw std::invalid_argument("native grep oracle fixture missing");
  std::string bytes;char c;while(file.get(c)){if(bytes.size()>=1024*1024)throw std::invalid_argument("grep oracle fixture capacity1MiB exceeded");bytes+=c;}
  const auto fixture=md::JSONParser(bytes).Parse();unsigned cases=0,matches=0,limited=0;
  for(const auto& row:md::Array(md::Get(fixture,"cases"))){
    const auto name=md::Text(md::Get(row,"name"));GrepPolicy policy;bool failed=false;
    const auto& include=md::Get(row,"include");const auto& exclude=md::Get(row,"exclude");
    if(include.kind!=md::JSON::Null)policy.include_argument=md::Text(include);
    if(exclude.kind!=md::JSON::Null)policy.exclude_argument=md::Text(exclude);
    try{policy.Compile();}catch(const std::invalid_argument&){failed=true;}
    const auto failure=md::Get(row,"expectedCompileFailure").raw=="true";
    if(failed!=failure)throw std::runtime_error("grep oracle compilation mismatch: "+name);
    ++cases;if(failed){++limited;continue;}
    std::set<std::string> expected,actual;
    for(const auto& id:md::Array(md::Get(row,"stockSelectedIds")))expected.insert(md::Text(id));
    GrepPattern::Work work;
    for(const auto& title:md::Array(md::Get(fixture,"titles"))){
      if(policy.Matches(GrepUnits(md::Get(title,"title").raw,32768),work))actual.insert(md::Text(md::Get(title,"stockId")));
      ++matches;
    }
    if(actual!=expected)throw std::runtime_error("grep oracle native selection mismatch: "+name);
  }
  std::cout<<"Native grep stock public contract "<<cases<<" cases / "<<matches<<" actual title comparisons / "<<limited<<" explicit syntax/subset failures PASS\n";
}
