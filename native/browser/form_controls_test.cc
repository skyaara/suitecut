// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "form_controls.h"
#include <cassert>
#include <fstream>
#include <iterator>
#include <sys/stat.h>

int main() {
  std::string first,second;
  {
    SuiteCutUploadFiles files;
    first=files.Stage("same.txt",std::string_view("a\0b",3));
    second=files.Stage("same.txt","second");
    assert(first!=second);
    struct stat info{};assert(stat(first.c_str(),&info)==0);assert((info.st_mode&0777)==0600);
    std::ifstream input(first,std::ios::binary);
    const std::string data((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
    assert(data==std::string("a\0b",3));
    files.Append(first,std::string_view("\0more",5));
    assert(std::filesystem::file_size(first)==8);
    bool foreign=false;try{files.Append("/tmp/not-owned","x");}catch(const std::invalid_argument&){foreign=true;}assert(foreign);
    assert(std::filesystem::file_size(files.Stage("empty.txt",""))==0);
    for(const std::string invalid:std::vector<std::string>{"", ".", "..", "../outside", "x/y", "x\\y",std::string("a\0b",3)}){
      bool rejected=false;try{files.Stage(invalid,"bad");}catch(const std::invalid_argument&){rejected=true;}
      assert(rejected);
    }
  }
  assert(!std::filesystem::exists(first));assert(!std::filesystem::exists(second));
  SuiteCutUploadStore store;
  auto retained=std::make_shared<SuiteCutUploadFiles>();
  first=retained->Stage("retained.txt","bytes");
  assert(store.CanReplace("first",128*1048576));
  store.Replace("first","frame-a",128*1048576,retained);retained.reset();
  assert(store.Inputs()==1&&store.Bytes()==128*1048576&&std::filesystem::exists(first));
  assert(!store.CanReplace("second",1));assert(store.CanReplace("first",4));
  store.DropFrame("other");assert(std::filesystem::exists(first));
  store.DropFrame("frame-a");assert(store.Bytes()==0&&store.Inputs()==0&&!std::filesystem::exists(first));
  for(int index=0;index<10000;++index){
    auto file=std::make_shared<SuiteCutUploadFiles>();
    const auto path=file->Stage("churn.bin","abc");
    store.Replace("churn",index%2?"a":"b",3,file);file.reset();
    store.Replace("churn","a",0,nullptr);assert(!std::filesystem::exists(path));
  }
  assert(store.Bytes()==0&&store.Inputs()==0);
}
