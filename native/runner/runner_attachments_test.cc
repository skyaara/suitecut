// SPDX-License-Identifier: MIT
#include "runner_attachments.h"
#include <cassert>
#include <fstream>
#include <iostream>
namespace sr=suitecut::runner;namespace af=sr::attachment_files;namespace fs=std::filesystem;
std::string Read(const fs::path& path){std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
void Write(const fs::path& path,std::string_view bytes){std::ofstream out(path,std::ios::binary);out.write(bytes.data(),bytes.size());assert(out.good());}
std::string Wire(std::string_view ascii){std::string out;for(char c:ascii){out+=af::Hex(std::string_view(&c,1));out+="00";}return out.empty()?"-":out;}
struct Fixture{
 fs::path root;std::vector<std::vector<std::string>> replies;std::vector<std::string> failures;sr::StepOwner owner{1,1,0,1,1,1,getpid(),"body"};sr::RunnerAttachments actor;
 explicit Fixture(fs::path directory):root(std::move(directory)),actor(root/"artifacts",{[this](const auto& line){replies.push_back(af::Fields(line));},[this](const auto& error){failures.push_back(error);}}){actor.WorkerStarted();actor.BeginCallback(owner,af::Now()+5000,af::Now());}
 void Begin(uint64_t n,const std::string& kind,size_t bytes,const std::string& source="",const std::string& name="native name"){assert(actor.Packet({"ATTACH_BEGIN",std::to_string(n),"0","1","1",Wire(name),kind,std::to_string(bytes),"~",af::Hex(source)},af::Now(),false));}
 void Until(const std::function<bool()>& done){const auto end=af::Now()+7000;while(!done()){assert(af::Now()<end);actor.Tick(af::Now());poll(nullptr,0,1);}}
 bool Replied(const std::string& command,uint64_t id)const{for(const auto& r:replies)if(r[0]==command&&af::UInt(r[1])==id)return true;return false;}
 void Body(uint64_t id,const std::string& data){Until([&]{return Replied("ATTACH_ADMITTED",id);});if(!data.empty()){assert(actor.Packet({"ATTACH_CHUNK",std::to_string(id),"0","1","1","1",af::Hex(data)},af::Now(),false));Until([&]{return Replied("ATTACH_CHUNK_OK",id);});}assert(actor.Packet({"ATTACH_FINISH",std::to_string(id),"0","1","1"},af::Now(),false));Until([&]{return Replied("ATTACH_RESULT",id);});}
 void Drain(){Until([&]{return !actor.Physical();});assert(failures.empty());}
};
int main(int argc,char** argv){if(argc==2&&std::string_view(argv[1])=="--native-attachment-worker")return af::Worker();if(argc==2&&std::string_view(argv[1])=="--native-attachment-cleanup")return af::Cleanup();char pattern[]="/tmp/suitecut-native-attachment-actor-XXXXXX";assert(mkdtemp(pattern));const fs::path root=pattern;size_t count=0;
 {Fixture f(root);const std::string bytes("real\0\xff\x01",7);f.Begin(1,"B",bytes.size());f.Body(1,bytes);assert(f.actor.Records().size()==1);const auto& r=f.actor.Records()[0];assert(r.owner==f.owner&&r.bytes==bytes.size()&&Read(r.path)==bytes&&r.writer_pid>0&&r.writer_pid!=getpid()&&!r.writer_signaled&&r.writer_exit==0&&r.started<=r.committed&&r.committed<=r.received);bool fence=false;f.actor.CloseAdmission(0,1,1);f.actor.Fence(0,1,1,[&]{fence=true;},af::Now());assert(fence);++count;}
 {Fixture f(root);Write(root/"actual.txt","source bytes");f.Begin(1,"P",0,(root/"actual.txt").string());f.Drain();assert(f.actor.Records().size()==1&&f.actor.Records()[0].content_type_json=="\"text/plain\""&&Read(f.actor.Records()[0].path)=="source bytes");++count;}
 {Fixture f(root);for(uint64_t i=1;i<=5;++i)f.Begin(i,"B",0);assert(f.actor.Physical()==4&&f.Replied("ATTACH_ADMITTED",5));f.actor.Cancel(0,1,1,"actual native cancel",af::Now());bool fence=false;f.actor.Fence(0,1,1,[&]{fence=true;},af::Now());assert(!fence&&f.actor.Physical()==4);f.Drain();assert(fence&&f.actor.ReservedBytes()==0&&f.actor.Records().empty()&&(f.actor.Directory().empty()||fs::is_empty(f.actor.Directory())));++count;}
 {Fixture f(root);f.actor.Amend(0,1,1,af::Now()+100,af::Now());f.Begin(1,"B",0);f.Drain();assert(f.actor.Records().empty()&&(f.actor.Directory().empty()||fs::is_empty(f.actor.Directory())));++count;}
 {Fixture f(root);f.actor.Amend(0,1,1,af::Now()+200,af::Now());f.Begin(1,"B",0);f.Until([&]{return f.Replied("ATTACH_ADMITTED",1);});f.actor.Amend(0,1,1,af::Now()+500,af::Now());const auto until=af::Now()+250;f.Until([&]{return af::Now()>=until;});f.Body(1,"");assert(f.actor.Records().size()==1);++count;}
 {Fixture f(root);f.Begin(1,"P",0,"");f.Drain();assert(f.actor.Records().empty()&&f.Replied("ATTACH_RESULT",1));f.Begin(2,"S",0);f.Body(2,"");assert(f.actor.Records().size()==1&&f.actor.Records()[0].bytes==0);++count;}
 {Fixture f(root);f.Begin(1,"B",0);f.actor.CloseAdmission(0,1,1);f.Begin(2,"B",0);assert(f.Replied("ATTACH_ADMITTED",2));bool fence=false;f.actor.Fence(0,1,1,[&]{fence=true;},af::Now());assert(!fence);f.Body(1,"");assert(fence&&f.actor.Records().size()==1);++count;}
 {Fixture f(root);f.Begin(1,"B",0,"","foreign");f.Until([&]{return f.Replied("ATTACH_ADMITTED",1);});Write(f.actor.Directory()/"foreign-1.dat","not owned");f.Body(1,"");assert(f.actor.Records().empty()&&Read(f.actor.Directory()/"foreign-1.dat")=="not owned");++count;}
 {Fixture f(root);f.Begin(1,"B",0);f.actor.Cancel(0,1,1,"cancel",af::Now());f.Drain();assert(f.actor.Packet({"ATTACH_FINISH","1","0","1","1"},af::Now(),false));assert(f.actor.Records().empty());++count;}
 fs::remove_all(root);std::cout<<"PASS"<<count<<" native attachment actor groups with actual file child/waitpid, callback fences, bounded admission, late deadline amendment, original timeout/cancel and foreign inode preservation\n";
}
