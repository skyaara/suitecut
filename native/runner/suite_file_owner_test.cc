// SPDX-License-Identifier: MIT
#include "suite_file_owner.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <type_traits>
using namespace suitecut::runner;
template<class F>void Reject(F fn){bool rejected=false;try{fn();}catch(const std::exception&){rejected=true;}assert(rejected);}
static void Write(const std::string& path,const std::string& bytes){std::ofstream out(path,std::ios::binary|std::ios::trunc);out<<bytes;out.close();assert(out);}
int main(){
 static_assert(!std::is_copy_constructible_v<SuiteFileOwner>);
 char pattern[]="/tmp/suitecut-file-owner-XXXXXX";const auto raw=::mkdtemp(pattern);assert(raw);const std::string root=raw;
 unsigned controls=0;
 try{
  const auto path=root+"/suite.mjs";Write(path,"export default {};");
  {SuiteFileOwner owner(1,path);struct stat real{};assert(::stat(path.c_str(),&real)==0);assert(owner.Token()==SuiteFileToken({1,static_cast<uint64_t>(real.st_dev),static_cast<uint64_t>(real.st_ino)}));assert(owner.Path()==path&&owner.Opened()<=std::chrono::steady_clock::now());owner.Validate();++controls;}
  {const auto link=root+"/link.mjs";assert(::symlink(path.c_str(),link.c_str())==0);SuiteFileOwner owner(2,link);assert(owner.Path()==path);assert(::unlink(link.c_str())==0);owner.Validate();++controls;}
  {SuiteFileOwner owner(3,path);SuiteFileOwner moved(std::move(owner));Reject([&]{owner.Validate();});moved.Validate();assert(moved.Token().id==3);++controls;}
  {SuiteFileOwner owner(4,path);const auto replacement=root+"/replacement";Write(replacement,"export default {};");assert(::rename(replacement.c_str(),path.c_str())==0);Reject([&]{owner.Validate();});++controls;}
  {SuiteFileOwner owner(5,path);std::ofstream out(path,std::ios::app);out<<"// changed size";out.close();assert(out);Reject([&]{owner.Validate();});++controls;}
  {SuiteFileOwner owner(6,path);assert(::rename(path.c_str(),(root+"/renamed").c_str())==0);Reject([&]{owner.Validate();});assert(::rename((root+"/renamed").c_str(),path.c_str())==0);++controls;}
  {SuiteFileOwner owner(7,path);assert(::unlink(path.c_str())==0);Reject([&]{owner.Validate();});Write(path,"new actual inode");Reject([&]{owner.Validate();});++controls;}
  Reject([&]{SuiteFileOwner owner(0,path);});++controls;
  Reject([&]{SuiteFileOwner owner(8,root);});++controls;
  Reject([&]{SuiteFileOwner owner(9,root+"/missing");});++controls;
  Reject([&]{SuiteFileOwner owner(10,std::string(path)+std::string(1,'\0')+"suffix");});++controls;
  {const auto fifo=root+"/fifo";assert(::mkfifo(fifo.c_str(),0600)==0);Reject([&]{SuiteFileOwner owner(11,fifo);});++controls;}
  {const auto invalid=root+"/invalid-"+std::string(1,static_cast<char>(0xff));Write(invalid,"real file");Reject([&]{SuiteFileOwner owner(12,invalid);});++controls;}
  {SuiteFileOwner owner(13,path);assert(owner.Retire()>=owner.Opened());Reject([&]{owner.Validate();});Reject([&]{owner.Retire();});++controls;}
  assert(controls==14);std::filesystem::remove_all(root);std::cout<<"Native actual suite file identity: "<<controls<<" controls passed\n";return 0;
 }catch(...){std::error_code cleanup;std::filesystem::remove_all(root,cleanup);if(cleanup)std::cerr<<cleanup.message()<<'\n';throw;}
}
