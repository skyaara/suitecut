// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <string>
#include <vector>
#include "include/cef_api_hash.h"
#include "include/cef_values.h"
using Dict=CefRefPtr<CefDictionaryValue>;
struct ArchiveFixture {
 std::vector<bool>coverage;int bytes=0;
 void PrimaryObservation(int context,const std::string&owner,bool ready){assert(context==7&&owner=="page:42");coverage.push_back(ready);}
 void ResponseBytes(int context,const std::string&owner,const std::string&,Dict,const std::string&session){assert(context==7&&owner=="page:42"&&session.empty());++bytes;}
};
struct EngineFixture{ArchiveFixture archive;auto&network_archive(){return archive;}};
struct SourceFixture {
 EngineFixture engine_;int network_context_id_=7,page_id_=42,next=100;bool closing_page_=false,accept=true,inline_ack=false;int submissions=0;Dict sent;
 int AllocateDevToolsId(){return ++next;}
 bool SubmitDevTools(int id,const std::string&method,Dict params,const std::string&session){assert(method=="Network.enable"&&session.empty()&&native_har_primary_enable_==id);++submissions;sent=params->Copy(false);if(inline_ack)NativeHARPrimaryReceipt(id,true,session);return accept;}
 #include "controllers/network/har_byte_source.inc"
};
int main(){
 assert(cef_api_hash(CEF_API_VERSION,0));auto params=CefDictionaryValue::Create();params->SetInt("maxResourceBufferSize",128*1048576);
 {SourceFixture f;f.NativeHARPrimaryEnable(params);assert(f.submissions==1&&f.engine_.archive.coverage==std::vector<bool>{false});const auto id=f.native_har_primary_enable_;f.NativeHARPrimaryReceipt(id+1,true,"");f.NativeHARPrimaryReceipt(id,true,"private-mirror");assert(f.native_har_primary_enable_==id&&f.engine_.archive.coverage.size()==1);f.NativeHARPrimaryReceipt(id,true,"");assert(f.native_har_primary_enable_==0&&(f.engine_.archive.coverage==std::vector<bool>{false,true}));f.NativeHARPrimaryReceipt(id,true,"");assert(f.engine_.archive.coverage.size()==2);}
 {SourceFixture f;f.accept=false;f.NativeHARPrimaryEnable(params);assert(f.native_har_primary_enable_==0&&f.engine_.archive.coverage==std::vector<bool>{false});}
 {SourceFixture f;f.NativeHARPrimaryEnable(params);f.NativeHARPrimaryReceipt(f.native_har_primary_enable_,false,"");assert((f.engine_.archive.coverage==std::vector<bool>{false,false}));}
 {SourceFixture f;f.NativeHARPrimaryEnable(params);const auto old=f.native_har_primary_enable_;f.NativeHARPrimaryReset();f.NativeHARPrimaryReceipt(old,true,"");assert(f.native_har_primary_enable_==0&&(f.engine_.archive.coverage==std::vector<bool>{false,false}));}
 {SourceFixture f;f.inline_ack=true;f.NativeHARPrimaryEnable(params);assert(f.native_har_primary_enable_==0&&(f.engine_.archive.coverage==std::vector<bool>{false,true}));assert(f.sent->GetInt("maxResourceBufferSize")==128*1048576);}
 {SourceFixture f;f.NativeHARPrimaryEnable(params);f.closing_page_=true;f.NativeHARPrimaryReceipt(f.native_har_primary_enable_,true,"");assert((f.engine_.archive.coverage==std::vector<bool>{false,false}));}
 {SourceFixture f;f.NativeHARResponseBytes("Network.dataReceived",params,"");f.NativeHARResponseBytes("Network.responseReceived",params,"");f.NativeHARResponseBytes("Network.loadingFinished",params,"");f.NativeHARResponseBytes("Network.loadingFailed",params,"");f.NativeHARResponseBytes("Network.requestServedFromCache",params,"");assert(f.engine_.archive.bytes==5);f.NativeHARResponseBytes("Network.dataReceived",params,"private-mirror");f.NativeHARResponseBytes("Network.requestWillBeSent",params,"");f.closing_page_=true;f.NativeHARResponseBytes("Network.dataReceived",params,"");assert(f.engine_.archive.bytes==5);}
}
