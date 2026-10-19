// SPDX-License-Identifier: MIT
#include "screenshot_inputs.h"
#include "operation_registry.h"
#include <cassert>
#include <cmath>
#include <limits>
namespace {
using Inputs=suitecut::ScreenshotInputs;
using Purpose=Inputs::Purpose;
struct Fixture {
 double now=1;size_t settled=0,wakes=0;unsigned nonce=0;
 std::shared_ptr<suitecut::ScreenshotByteBudget>budget=std::make_shared<suitecut::ScreenshotByteBudget>();
 std::shared_ptr<Inputs>inputs;Inputs::Owner owner{3,7,11};
 Fixture(){Inputs::Hooks hooks;hooks.now=[this]{return now;};hooks.settled=[this](int context){assert(context==owner.context);++settled;};hooks.wake=[this]{++wakes;};hooks.background=[](auto){assert(false&&"Configuration-only input may not create a fake file job");return false;};inputs=std::make_shared<Inputs>(budget,std::move(hooks));}
 std::string Prepare(Purpose purpose=Purpose::Screenshot,size_t size=2,double deadline=50){std::string error;const auto result=inputs->Prepare(owner,"configuration-"+std::to_string(++nonce),false,0,size,deadline,error,purpose);assert(error.empty()&&!result.token.empty());return result.token;}
 void Upload(const std::string&token,const std::string&text){std::string error;for(size_t offset=0;offset<text.size();offset+=24576){const auto end=std::min(text.size(),offset+24576);assert(inputs->Append(owner,token,true,offset,{text.begin()+offset,text.begin()+end},error));}assert(error.empty());}
 std::shared_ptr<Inputs::Data>Claim(const std::string&token,Purpose purpose=Purpose::Screenshot){std::shared_ptr<Inputs::Data>data;std::string error;assert(inputs->Claim(owner,token,"",81,[&](auto value,double deadline,const std::string&reason){assert(reason.empty()&&deadline==50);data=std::move(value);},error,purpose));assert(error.empty()&&data);return data;}
 void Empty(){assert(inputs->AdmissionCount()==0&&inputs->PhysicalCount()==0&&inputs->ContextReady(3)&&budget->Used()==0);}
};
}
int main(){
 {
  Fixture fixture;const std::string body="{\"style\":\""+std::string(140000,'x')+"\"}";const auto token=fixture.Prepare(Purpose::Screenshot,body.size());fixture.Upload(token,body);
  std::string error,json;assert(!fixture.inputs->Inspect(fixture.owner,token,json,error,Purpose::PDF));error.clear();assert(fixture.inputs->Inspect(fixture.owner,token,json,error,Purpose::Screenshot)&&json==body);
  auto data=fixture.Claim(token);assert(data->png.empty()&&data->config==body&&data->admission&&fixture.inputs->AdmissionCount()==1&&!fixture.inputs->ContextReady(3));assert(fixture.budget->Used()==4*body.size());
  fixture.inputs->PageClosed(7);fixture.inputs->DiscardClient(7,"configuration-1");assert(fixture.inputs->AdmissionCount()==1);data.reset();fixture.Empty();
 }
 {
  Fixture fixture;const auto token=fixture.Prepare(Purpose::PDF);fixture.Upload(token,"{}");std::string error;bool called=false;
  assert(!fixture.inputs->Claim(fixture.owner,token,"",81,[&](auto,double,const auto&){called=true;},error,Purpose::Screenshot));assert(!called&&error.find("purpose mismatch")!=std::string::npos);
  auto data=fixture.Claim(token,Purpose::PDF);data.reset();fixture.Empty();
 }
 {
  Fixture fixture;std::string error;assert(fixture.inputs->Prepare(fixture.owner,"invalid",false,0,2,50,error).token.empty());error.clear();assert(fixture.inputs->Prepare(fixture.owner,"invalid",true,0,2,50,error,Purpose::PDF).token.empty());error.clear();assert(fixture.inputs->Prepare(fixture.owner,"invalid",false,33,2,50,error,Purpose::Screenshot).token.empty());error.clear();assert(fixture.inputs->Prepare(fixture.owner,"invalid",false,0,16*1048576+1,50,error,Purpose::PDF).token.empty());fixture.Empty();
 }
 {
  Fixture fixture;const auto token=fixture.Prepare();std::string error;assert(!fixture.inputs->Append(fixture.owner,token,false,0,{1},error));fixture.inputs->PageClosed(7);fixture.Empty();
 }
 {
  Fixture fixture;const auto token=fixture.Prepare();fixture.Upload(token,"{}");auto retained=fixture.Claim(token);for(unsigned i=0;i<15;++i)fixture.Prepare();std::string error;assert(fixture.inputs->Prepare(fixture.owner,"overflow",false,0,2,50,error,Purpose::Screenshot).token.empty());assert(fixture.inputs->AdmissionCount()==16);retained.reset();assert(fixture.inputs->AdmissionCount()==15);fixture.Prepare();fixture.inputs->Clear();fixture.Empty();
 }
 {
  Fixture fixture;const auto token=fixture.Prepare(Purpose::PDF,9*1048576);std::string error;assert(fixture.inputs->Prepare(fixture.owner,"overflow",false,0,9*1048576,50,error,Purpose::PDF).token.empty());assert(error.find("64MiB")!=std::string::npos);fixture.inputs->PageClosed(7);fixture.Empty();
 }
 {
  Fixture fixture;const auto token=fixture.Prepare();fixture.Upload(token,"{}");fixture.now=50;std::string error,json;assert(!fixture.inputs->Inspect(fixture.owner,token,json,error,Purpose::Screenshot));assert(error.find("original deadline")!=std::string::npos);fixture.Empty();
 }
 {
  Fixture fixture;std::string structure="{\"action\":\"pdf\",\"many\":[";for(size_t i=0;i<250001;++i){if(i)structure+=',';structure+="null";}structure+="]}";const auto token=fixture.Prepare(Purpose::PDF,structure.size());fixture.Upload(token,structure);std::string error,json;assert(!fixture.inputs->Inspect(fixture.owner,token,json,error,Purpose::PDF));assert(error.find("work budget")!=std::string::npos);fixture.inputs->Clear();fixture.Empty();
 }
 {
  SuiteCutOperationRegistry registry;const auto commit=registry.Begin(1,20,0);assert(commit);assert(registry.BindOriginalDeadline(1,commit->generation,50));assert(!registry.IsExpired(1,commit->generation,49));assert(registry.IsExpired(1,commit->generation,50));
  assert(!registry.BindOriginalDeadline(1,commit->generation,0));assert(!registry.BindOriginalDeadline(1,commit->generation,51));assert(!registry.BindOriginalDeadline(1,commit->generation+1,40));assert(!registry.BindOriginalDeadline(1,commit->generation,std::numeric_limits<double>::quiet_NaN()));assert(registry.BindOriginalDeadline(1,commit->generation,40));
  const auto disabled=registry.Begin(2,20,0);assert(disabled&&registry.BindOriginalDeadline(2,disabled->generation,0));assert(!registry.IsExpired(2,disabled->generation,1e100));
 }
 {
  Fixture fixture;const auto token=fixture.Prepare();fixture.Upload(token,"{}");std::string json,error;std::shared_ptr<Inputs::Data>inspection;
  assert(fixture.inputs->Inspect(fixture.owner,token,json,error,Purpose::Screenshot,&inspection));fixture.inputs->Cancel(fixture.owner,token);assert(fixture.inputs->LogicalCount()==0&&fixture.budget->Used()==8&&inspection->config=="{}");inspection.reset();fixture.Empty();
 }

 {
  Fixture fixture;std::string text="{\"action\":\"pdf\",\"extra\":[";for(size_t i=0;i<100001;++i){if(i)text+=',';text+='0';}text+="]}";const auto token=fixture.Prepare(Purpose::PDF,text.size());fixture.Upload(token,text);std::string json,error;assert(!fixture.inputs->Inspect(fixture.owner,token,json,error,Purpose::PDF));assert(error.find("node/entry")!=std::string::npos);fixture.inputs->Clear();fixture.Empty();
 }
 {
  Fixture fixture;const auto text=std::string(257,'[')+"0"+std::string(257,']');const auto token=fixture.Prepare(Purpose::PDF,text.size());fixture.Upload(token,text);std::string json,error;assert(!fixture.inputs->Inspect(fixture.owner,token,json,error,Purpose::PDF));assert(error.find("nesting levels")!=std::string::npos);fixture.inputs->Clear();fixture.Empty();
 }
 {
  Fixture fixture;std::string text="{\"action\":\"pdf\",\"headerTemplate\":\"<div title=\\\"real\\\">\\n";for(size_t i=0;i<850000;++i)text+="\\u4e2d";text+="</div>\"}";assert(text.size()>5*1048576-200000&&850025<1048576);const auto token=fixture.Prepare(Purpose::PDF,text.size());fixture.Upload(token,text);std::string json,error;assert(fixture.inputs->Inspect(fixture.owner,token,json,error,Purpose::PDF)&&json==text);fixture.inputs->Clear();fixture.Empty();
 }
 {
  using Inputs=suitecut::ScreenshotInputs;double now=10;size_t clock_reads=0;auto budget=std::make_shared<suitecut::ScreenshotByteBudget>();Inputs::Hooks hooks;hooks.now=[&]{return ++clock_reads>=4?50:now;};auto inputs=std::make_shared<Inputs>(budget,std::move(hooks));std::string error,json;const Inputs::Owner owner{3,7,9};std::string text="{\"action\":\"pdf\",\"headerTemplate\":\""+std::string(100000,'x')+"\"}";const auto ticket=inputs->Prepare(owner,"actual-clock",false,0,text.size(),50,error,Purpose::PDF);assert(!ticket.token.empty());
  for(size_t offset=0;offset<text.size();offset+=24576){clock_reads=0;const auto end=std::min(text.size(),offset+24576);assert(inputs->Append(owner,ticket.token,true,offset,{text.begin()+offset,text.begin()+end},error));}
  clock_reads=0;assert(!inputs->Inspect(owner,ticket.token,json,error,Purpose::PDF));assert(error.find("original deadline")!=std::string::npos);assert(clock_reads>=4&&inputs->AdmissionCount()==0&&budget->Used()==0);
 }

}
