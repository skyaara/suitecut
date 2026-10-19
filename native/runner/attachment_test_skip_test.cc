// SPDX-License-Identifier: MIT
#include "runner_attachments.h"
#include <cassert>
#include <filesystem>
#include <iostream>
#include <limits>
namespace sr=suitecut::runner;namespace af=sr::attachment_files;namespace fs=std::filesystem;
struct Fixture {
 fs::path root;sr::StepOwner owner;std::vector<std::vector<std::string>> packets;std::vector<std::string> failures;sr::RunnerAttachments actor;
 Fixture(fs::path directory,sr::StepOwner actual):root(std::move(directory)),owner(std::move(actual)),actor(root,{[this](const auto& line){packets.push_back(af::Fields(line));},[this](const auto& error){failures.push_back(error);}}){actor.WorkerStarted();actor.BeginCallback(owner,af::Now()+5000,af::Now());}
 void Begin(const std::string& kind="B",const std::string& source="") {assert(actor.Packet({"ATTACH_BEGIN","1",std::to_string(owner.callback_token),std::to_string(owner.scope),std::to_string(owner.scope_generation),"6100",kind,"0","~",af::Hex(source)},af::Now(),false));}
 bool Can(uint64_t request=7)const{return actor.CanAcceptTestSkip(owner.callback_token,owner.scope,owner.scope_generation,request,af::Now());}
 void Accept(uint64_t request=7){actor.AcceptTestSkip(owner.callback_token,owner.scope,owner.scope_generation,request,"\"actual cause\\ud800\"",af::Now());}
 void Drain(){const auto cap=af::Now()+7000;while(actor.Physical()){assert(af::Now()<cap);actor.Tick(af::Now());poll(nullptr,0,1);}assert(failures.empty());}
 void Empty()const{if(fs::exists(root))for(const auto& p:fs::recursive_directory_iterator(root))assert(!p.is_regular_file());assert(actor.Records().empty());}
 bool Typed(uint64_t request)const{for(const auto& p:packets)if(p[0]=="ATTACH_TEST_SKIP_RESULT"){assert(p.size()==8&&p[6]==std::to_string(request)&&p[7]=="-");return true;}return false;}
};
int main(int argc,char** argv){
 if(argc==2&&std::string_view(argv[1])=="--native-attachment-worker")return af::Worker();
 if(argc==2&&std::string_view(argv[1])=="--native-attachment-cleanup")return af::Cleanup();
 char name[]="/tmp/suitecut-attachment-test-skip-XXXXXX";assert(mkdtemp(name));const fs::path root=name;unsigned groups=0;
 {Fixture f(root/"body",{1,1,0,1,1,1,getpid(),"body"});f.Begin();assert(f.Can());assert(!f.actor.CanAcceptTestSkip(0,2,1,7,af::Now()));assert(!f.actor.CanAcceptTestSkip(0,1,1,0,af::Now()));assert(!f.actor.CanAcceptTestSkip(0,1,1,7,std::numeric_limits<double>::quiet_NaN()));f.Accept();assert(!f.Can());bool rejected=false;try{f.Accept();}catch(const std::runtime_error&){rejected=true;}assert(rejected);bool forwarded=false;f.actor.Fence(0,1,1,[&]{forwarded=true;},af::Now());assert(!forwarded&&f.actor.Physical()==1);f.Drain();assert(forwarded&&f.Typed(7));f.Empty();++groups;}
 {Fixture f(root/"beforeEach",{2,1,19,2,1,1,getpid(),"beforeEach"});f.Begin();assert(f.Can());f.Accept(8);f.actor.Fence(19,2,1,[]{},af::Now());f.Drain();assert(f.Typed(8));f.Empty();++groups;}
 {Fixture f(root/"beforeAll-unsupported",{3,1,31,3,1,1,getpid(),"beforeAll"});f.Begin();assert(!f.Can());bool rejected=false;try{f.Accept();}catch(const std::runtime_error&){rejected=true;}assert(rejected);f.actor.Cancel(31,3,1,"ordinary native cancellation",af::Now());f.actor.Fence(31,3,1,[]{},af::Now());f.Drain();assert(!f.Typed(7));f.Empty();++groups;}
 {Fixture f(root/"ordinary-observed-error",{4,1,0,4,1,1,getpid(),"body"});f.Begin("P",(root/"genuine-missing-file").string());const auto cap=af::Now()+5000;while(f.Can()){assert(af::Now()<cap);f.actor.Tick(af::Now());poll(nullptr,0,1);}assert(f.actor.Physical());bool rejected=false;try{f.Accept();}catch(const std::runtime_error&){rejected=true;}assert(rejected);f.actor.Fence(0,4,1,[]{},af::Now());f.Drain();assert(!f.Typed(7));f.Empty();++groups;}
 {Fixture f(root/"closed-raw-callback",{5,1,0,5,1,1,getpid(),"body"});f.Begin();f.actor.CloseAdmission(0,5,1);assert(!f.Can());f.actor.Cancel(0,5,1,"closed callback",af::Now());f.actor.Fence(0,5,1,[]{},af::Now());f.Drain();assert(!f.Typed(7));f.Empty();++groups;}
 fs::remove_all(root);std::cout<<"PASS"<<groups<<" actual native attachment test-skip cause/owner/phase/error precedence and physical file rollback groups\n";
}
