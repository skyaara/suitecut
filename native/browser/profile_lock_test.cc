// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "profile_lock.h"
#include <cassert>
#include <filesystem>
#include <fstream>
int main(){
  char pattern[]="/tmp/suitecut-profile-lock-XXXXXX";
  const char* root=mkdtemp(pattern);assert(root);
  const auto path=std::filesystem::path(root);
  std::ofstream(path/"retained-data")<<"original";
  std::string error;
  {
    SuiteCutProfileLock first,second;
    assert(first.Acquire(root,error));
    assert(!second.Acquire(root,error)&&error=="Native persistent profile is already in use");
    assert(!first.Acquire(root,error));
    std::filesystem::create_directory_symlink(path,path/"alias");
    SuiteCutProfileLock alias;assert(!alias.Acquire((path/"alias").string(),error));
    assert(std::filesystem::exists(path/"retained-data"));
  }
  {SuiteCutProfileLock next;assert(next.Acquire(root,error));}
  assert(std::filesystem::exists(path/"retained-data"));
  std::filesystem::remove(path/".suitecut-profile.lock");
  std::filesystem::create_symlink(path/"retained-data",path/".suitecut-profile.lock");
  {SuiteCutProfileLock symlink;assert(!symlink.Acquire(root,error));}
  std::ifstream original(path/"retained-data");std::string text;original>>text;assert(text=="original");
  original.close();std::filesystem::remove_all(path);
}
