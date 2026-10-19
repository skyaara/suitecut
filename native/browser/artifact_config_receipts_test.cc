// SPDX-License-Identifier: MIT
#include "artifact_config_receipts.h"
#include <cassert>
int main(){
 for(const bool physical:{false,true}){
  bool current=true,expired=false;int actions=0,deadlines=0;
  auto lease=std::make_shared<int>(11);std::weak_ptr<int>weak=lease;
  auto receipt=suitecut::RetainArtifactConfiguration(lease,[&]{return current;},[&]{return expired;},[&]{++deadlines;current=false;},[&](bool ok,const std::string&json){assert(!weak.expired()&&ok&&json=="actual");++actions;},physical);
  lease.reset();assert(!weak.expired());receipt(true,"actual");assert(actions==1&&deadlines==0);receipt={};assert(weak.expired());
 }
 for(const bool physical:{false,true}){
  bool current=false;int actions=0;auto lease=std::make_shared<int>(11);std::weak_ptr<int>weak=lease;
  auto receipt=suitecut::RetainArtifactConfiguration(lease,[&]{return current;},[]{assert(false);return false;},[]{assert(false);},[&](bool,const std::string&){assert(!weak.expired());++actions;},physical);
  lease.reset();assert(!weak.expired());receipt(false,"actual late receipt");assert(actions==(physical?1:0));receipt={};assert(weak.expired());
 }
 for(const bool physical:{false,true}){
  bool current=true;int actions=0,deadlines=0;auto lease=std::make_shared<int>(11);std::weak_ptr<int>weak=lease;
  auto receipt=suitecut::RetainArtifactConfiguration(lease,[&]{return current;},[]{return true;},[&]{++deadlines;current=false;},[&](bool,const std::string&){assert(!current&&!weak.expired());++actions;},physical);
  lease.reset();receipt(true,"ACK arrived after original deadline before Tick");assert(deadlines==1&&actions==(physical?1:0));receipt={};assert(weak.expired());
 }
 {
  auto lease=std::make_shared<int>(11);std::weak_ptr<int>weak=lease;
  auto receipt=suitecut::RetainArtifactConfiguration(lease,[]{assert(false);return false;},[]{assert(false);return false;},[]{assert(false);},[](bool,const std::string&){assert(false);},false);
  lease.reset();assert(!weak.expired());receipt={};assert(weak.expired()); // Actual channel close discards its real callbacks.
 }
}
