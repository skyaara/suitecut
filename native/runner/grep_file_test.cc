// SPDX-License-Identifier: MIT
#include "grep_file.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <vector>
namespace fs=std::filesystem;
using suitecut::runner::GrepFileComponent;
int main(){
  std::vector<char> pattern{'/','t','m','p','/','s','u','i','t','e','c','u','t','-','g','r','e','p','-','f','i','l','e','-','X','X','X','X','X','X','\0'};
  const auto created=::mkdtemp(pattern.data());if(!created)throw std::runtime_error("actual grep path control directory creation failed");
  const fs::path root(created);unsigned checks=0;
  const auto check=[&](bool condition){++checks;if(!condition)throw std::runtime_error("grep file control failed "+std::to_string(checks));};
  const auto reject=[&](const fs::path& file,const std::string& directory){bool failed=false;try{(void)GrepFileComponent(file,directory);}catch(const std::exception&){failed=true;}check(failed);};
  fs::create_directories(root/"inside"/"nested");fs::create_directory(root/"outside");
  {std::ofstream file(root/"inside"/"nested"/"suite.mjs");file<<"export default {tests:[]};\n";}
  {std::ofstream file(root/"outside"/"other.mjs");file<<"export default {tests:[]};\n";}
  const auto file=root/"inside"/"nested"/"suite.mjs";
  check(GrepFileComponent(file,"")=="suite.mjs");check(GrepFileComponent(file,(root/"inside").string())=="nested/suite.mjs");
  check(GrepFileComponent(file,(root/"inside"/"nested").string())=="suite.mjs");
  fs::create_directory_symlink(root/"inside",root/"root-alias");
  check(GrepFileComponent(file,(root/"root-alias").string())=="nested/suite.mjs");
  fs::create_symlink(root/"outside"/"other.mjs",root/"inside"/"escaped.mjs");
  reject(root/"inside"/"escaped.mjs",(root/"inside").string());reject(file,(root/"outside").string());
  reject(file,file.string());reject(file,(root/"missing").string());reject(root/"missing.mjs",root.string());
  fs::remove_all(root);check(!fs::exists(root));
  std::cout<<"Native grep real canonical file "<<checks<<" controls PASS\n";
}
