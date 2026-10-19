// SPDX-License-Identifier: MIT
#include "screenshot_inputs.h"
#include <cassert>
#include <deque>
#include <fstream>
#include <thread>
#include <filesystem>
#include <sys/stat.h>
#include <unistd.h>
namespace {
using Inputs=suitecut::ScreenshotInputs;
struct Fixture{
 double now=1;int nonce=0;bool reject_background=false,reject_post=false,physical_available=true;std::deque<std::function<void()>> cpu,ui;
 std::shared_ptr<suitecut::ScreenshotByteBudget>budget=std::make_shared<suitecut::ScreenshotByteBudget>();std::shared_ptr<Inputs>inputs;
 Inputs::Owner owner{2,3,7};std::vector<std::string>errors,watches;std::vector<std::shared_ptr<Inputs::Data>>results;std::vector<double>deadlines;
 Fixture(){Inputs::Hooks hooks;hooks.now=[this]{return now;};hooks.physical_available=[this]{return physical_available;};hooks.background=[this](auto task){if(reject_background)return false;cpu.push_back(std::move(task));return true;};hooks.post=[this](auto task){if(reject_post)return false;ui.push_back(std::move(task));return true;};inputs=std::make_shared<Inputs>(budget,std::move(hooks));}
 std::string Prepare(bool file=false,size_t bytes=131077,double deadline=50){std::string error;const auto ticket=inputs->Prepare(owner,"fixture-client-"+std::to_string(++nonce),file,file?0:bytes,2,deadline,error);assert(error.empty()&&!ticket.token.empty()&&ticket.deadline==deadline);return ticket.token;}
 void Config(const std::string&token){std::string error;assert(inputs->Append(owner,token,true,0,{'{','}'},error)&&error.empty());}
 void Upload(const std::string&token,const std::vector<uint8_t>&bytes){std::string error;for(size_t offset=0;offset<bytes.size();offset+=24576){const auto last=std::min(bytes.size(),offset+24576);assert(inputs->Append(owner,token,false,offset,{bytes.begin()+offset,bytes.begin()+last},error));}assert(error.empty());}
 void Watch(const std::string&token){std::string error;assert(inputs->Watch(owner,token,90,[this](bool ok,const std::string&value){watches.push_back(ok?"committed":value);},error));}
 void Claim(const std::string&token,const std::string&path=""){std::string error;assert(inputs->Claim(owner,token,path,8,[this](auto data,double deadline,const std::string&error){if(data)results.push_back(data);errors.push_back(error);deadlines.push_back(deadline);},error));assert(error.empty());}
 void CPU(){assert(!cpu.empty());auto work=std::move(cpu.front());cpu.pop_front();std::thread thread([&]{work();});thread.join();work={};}
 void UI(){assert(!ui.empty());auto work=std::move(ui.front());ui.pop_front();work();work={};}
 void Empty(){results.clear();assert(!inputs->LogicalCount()&&!inputs->PhysicalCount()&&!budget->Used()&&inputs->ContextReady(2));assert(cpu.empty()&&ui.empty());}
};
}
int main(){
 const auto root=std::filesystem::temp_directory_path()/("suitecut-input-test-"+std::to_string(getpid()));std::filesystem::create_directories(root);const auto path=root/"actual-readonly.png";std::vector<uint8_t>bytes(131077);for(size_t i=0;i<bytes.size();++i)bytes[i]=uint8_t((i*73+19)%251);{std::ofstream stream(path,std::ios::binary);stream.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());}chmod(path.c_str(),0444);
 {
  Fixture test;auto token=test.Prepare();test.Config(token);test.Upload(token,bytes);test.Watch(token);std::string error,json;assert(test.inputs->Inspect(test.owner,token,json,error)&&json=="{}");test.now=17;test.Claim(token);assert(test.results.size()==1&&test.results[0]->png==bytes&&test.deadlines[0]==50&&test.watches==std::vector<std::string>{"committed"});test.Empty();
 }
 {
  Fixture test;const auto token=test.Prepare();test.Config(token);std::string error;assert(!test.inputs->Append(test.owner,token,true,0,{'x'},error));assert(error.find("duplicate")!=std::string::npos);error.clear();auto other=test.owner;++other.source;assert(!test.inputs->Append(other,token,false,0,{1},error));assert(error.find("another source")!=std::string::npos);test.inputs->Discard(2,999,token);assert(test.inputs->LogicalCount()==1);test.inputs->Discard(2,3,token);test.Empty();
 }
 {
  Fixture test;const auto token=test.Prepare();test.Config(token);test.Watch(token);test.now=50;test.inputs->Tick();assert(test.watches.size()==1&&test.watches[0].find("original deadline")!=std::string::npos);std::string error;assert(!test.inputs->Append(test.owner,token,false,0,{1},error));test.Empty();
 }
 {
  Fixture test;const auto token=test.Prepare(true);test.Config(token);test.Watch(token);test.Claim(token,path);assert(test.inputs->PhysicalCount()==1&&!test.inputs->ContextReady(2));test.CPU();assert(test.inputs->PhysicalCount()==1&&test.errors.empty());test.UI();assert(test.results.size()==1&&test.results[0]->png==bytes&&test.watches==std::vector<std::string>{"committed"});test.Empty();
 }
 {
  Fixture test;const auto token=test.Prepare(true);test.Config(token);test.Watch(token);test.Claim(token,path);test.inputs->CancelCommand(3,8);assert(test.inputs->LogicalCount()==0&&test.inputs->PhysicalCount()==1&&test.budget->Used()>0);test.CPU();assert(test.inputs->PhysicalCount()==1&&test.budget->Used()>0);test.UI();assert(test.errors.empty()&&test.watches.size()==1&&test.watches[0].find("canceled")!=std::string::npos);test.Empty();
 }
 {
  Fixture test;const auto token=test.Prepare(true);test.Config(token);test.Watch(token);test.Claim(token,path);test.CPU();test.now=51;test.UI();assert(test.results.empty()&&test.errors.size()==1&&test.errors[0].find("original deadline")!=std::string::npos);test.Empty();
 }
 {
  Fixture test;const auto token=test.Prepare(true);test.Config(token);test.Watch(token);test.Claim(token,path);test.now=51;test.inputs->Tick();assert(test.errors.size()==1&&test.inputs->PhysicalCount()==1&&!test.inputs->ContextReady(2));test.CPU();test.UI();assert(test.errors.size()==1);test.Empty();
 }
 {
  Fixture test;test.reject_post=true;const auto token=test.Prepare(true);test.Config(token);test.Claim(token,path);test.CPU();assert(test.inputs->PhysicalCount()==1);test.inputs->Tick();assert(test.results.size()==1&&test.results[0]->png==bytes);test.Empty();
 }
 {
  Fixture test;test.reject_background=true;const auto token=test.Prepare(true);test.Config(token);test.Claim(token,path);assert(test.errors.size()==1&&test.errors[0].find("background admission")!=std::string::npos);test.Empty();
 }
 {
  Fixture test;const auto token=test.Prepare(true);test.Config(token);test.Claim(token,path);test.CPU();test.inputs->PageClosed(3);assert(test.inputs->PhysicalCount()==1&&test.errors.empty());test.UI();assert(test.errors.empty());test.Empty();
 }
 {
  const auto fifo=root/"actual-fifo";assert(mkfifo(fifo.c_str(),0600)==0);Fixture test;const auto token=test.Prepare(true);test.Config(token);test.Claim(token,fifo);test.CPU();test.UI();assert(test.errors.size()==1&&test.errors[0].find("regular PNG")!=std::string::npos);test.Empty();
 }
 {
  Fixture test;const auto token=test.Prepare(true,0,0);test.Config(token);test.Claim(token,path);test.now=1e12;test.inputs->Tick();assert(test.errors.empty()&&test.inputs->PhysicalCount()==1);test.CPU();test.UI();assert(test.results.size()==1&&test.deadlines[0]==0);test.Empty();
 }
 {
  Fixture test;std::string error;assert(test.inputs->Prepare(test.owner,"invalid",false,2097153,2,50,error).token.empty());assert(test.budget->Used()==0);test.Empty();
 }
 {
  Fixture test;test.physical_available=false;const auto token=test.Prepare(true);test.Config(token);test.Claim(token,path);assert(test.cpu.empty()&&!test.inputs->PhysicalCount()&&test.inputs->LogicalCount()==1);test.now=49;test.inputs->Tick();assert(test.cpu.empty());test.physical_available=true;test.inputs->Tick();assert(test.inputs->PhysicalCount()==1);test.CPU();test.UI();assert(test.results.size()==1&&test.deadlines[0]==50);test.Empty();
 }
 {
  Fixture test;test.physical_available=false;const auto token=test.Prepare(true);test.Config(token);test.Watch(token);test.Claim(token,path);test.now=50;test.inputs->Tick();assert(test.cpu.empty()&&test.errors.size()==1&&test.errors[0].find("original deadline")!=std::string::npos);test.Empty();
 }
 {
  Fixture test;std::string error;auto token=test.inputs->Prepare(test.owner,"lost-ack-client",false,100,2,0,error);assert(!token.token.empty());test.inputs->DiscardClient(999,"lost-ack-client");assert(test.inputs->LogicalCount()==1);test.inputs->DiscardClient(3,"lost-ack-client");test.Empty();
 }
 struct stat final_stat{};assert(stat(path.c_str(),&final_stat)==0&&(final_stat.st_mode&0222)==0&&size_t(final_stat.st_size)==bytes.size());std::filesystem::permissions(path,std::filesystem::perms::owner_all);std::filesystem::remove_all(root);
}
