// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "init_snapshot_publication.h"
#include <cassert>
#include <cstdio>
#include <thread>
using Publications=suitecut::InitSnapshotPublications;
int main(){int groups=0;
 {Publications p;int calls=0;auto accept=[&](auto){++calls;return true;};assert(!p.Commit(0,"{}",accept)&&!p.Commit(1,"",accept)&&calls==0&&p.Contexts()==0);++groups;}
 {Publications p;assert(!p.Commit(1,"old",[](auto){return false;})&&p.Current(1).version==0&&p.Contexts()==0&&p.Bytes()==0);
  assert(p.Commit(1,"old",[](auto){return true;}));auto old=p.Current(1);assert(old.version==1&&*old.bytes=="old");
  assert(!p.Commit(1,"new",[](auto){return false;}));assert(p.Current(1).version==1&&p.Current(1).bytes==old.bytes&&p.Bytes()==3);++groups;}
 {Publications p;int accepts=0;auto accept=[&](auto){++accepts;return true;};assert(p.Commit(1,"same",accept)&&p.Commit(1,"same",accept));assert(accepts==1&&p.Current(1).version==1&&p.Bodies()==1);++groups;}
 {Publications p;std::shared_ptr<const std::string>store;auto accept=[&](auto text){store=std::move(text);return true;};assert(p.Commit(1,"old",accept));auto old=store;
  assert(p.Commit(1,"replacement",accept)&&p.Current(1).version==2&&p.Bodies()==2&&p.Bytes()==14);p.Drop(1);assert(p.Contexts()==0&&p.Bytes()==14);
  store.reset();assert(p.Bytes()==3);old.reset();assert(p.Bytes()==0&&p.Bodies()==0);++groups;}
 {Publications p;for(int i=1;i<=32;++i)assert(p.Commit(i,"x",[](auto){return true;}));assert(!p.Commit(33,"x",[](auto){return true;})&&p.Contexts()==32);p.Drop(1);assert(p.Commit(33,"x",[](auto){return true;}));++groups;}
 {Publications p;std::vector<std::shared_ptr<const std::string>>readers;for(size_t i=0;i<64;++i){assert(p.Commit(1,std::to_string(i),[](auto){return true;}));readers.push_back(p.Current(1).bytes);}
  assert(p.Bodies()==64&&!p.Commit(1,"capacity",[](auto){return true;}));readers.front().reset();assert(p.Commit(1,"readmitted",[](auto){return true;}));++groups;}
 {Publications p;const std::string body(suitecut::InitSnapshotWire::kPayload,'x');for(int i=1;i<=8;++i)assert(p.Commit(i,body,[](auto){return true;}));
  assert(p.Bytes()==Publications::kBytes&&!p.Commit(9,"x",[](auto){return true;}));p.Drop(1);assert(p.Commit(9,"x",[](auto){return true;}));++groups;}
 {Publications p;auto accept=[](auto){return true;};assert(p.Commit(1,"scripts1/binding9/credentials4",accept));assert(p.Commit(1,"scripts2/binding9/credentials4",accept));assert(p.Commit(1,"scripts2/binding10/credentials3",accept));
  assert(p.Current(1).version==3&&*p.Current(1).bytes=="scripts2/binding10/credentials3");++groups;}
 {Publications p;assert(p.Commit(1,"actual IO lease",[](auto){return true;}));auto lease=p.Current(1).bytes;p.Drop(1);assert(p.Bytes()==15);
  std::thread io([lease=std::move(lease)]()mutable{lease.reset();});io.join();assert(p.Bytes()==0&&p.Bodies()==0);++groups;}
 {Publications p;std::shared_ptr<const std::string>accepted;const std::string raw="{\"source\":\"\\ud800\\udfff\"}";
  assert(p.Commit(1,raw,[&](auto text){accepted=text;return true;}));assert(*accepted==raw&&p.Current(1).bytes==accepted);++groups;}
 {Publications p;int failure=9;bool caught=false;
  try{p.Commit(1,"not accepted",[&](auto)->bool{throw &failure;});}catch(int*error){caught=error==&failure;}
  assert(caught&&p.Contexts()==0&&p.Current(1).version==0&&p.Bytes()==0&&p.Bodies()==0);++groups;}
 {Publications p;assert(p.Commit(1,"accepted",[](auto){return true;}));const auto before=p.Current(1);int failure=10;bool caught=false;
  try{p.Commit(1,"not accepted replacement",[&](auto)->bool{throw &failure;});}catch(int*error){caught=error==&failure;}
  assert(caught&&p.Contexts()==1&&p.Current(1).version==before.version&&p.Current(1).bytes==before.bytes&&p.Bytes()==8&&p.Bodies()==1);++groups;}
 std::printf("Init snapshot authoritative publication: %d controlled groups PASS\n",groups);
}
