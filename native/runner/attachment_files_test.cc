// SPDX-License-Identifier: MIT
#include "attachment_files.h"
#include "host_process.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <sys/resource.h>
namespace af=suitecut::runner::attachment_files;namespace sr=suitecut::runner;namespace fs=std::filesystem;
std::string Read(const fs::path& p){std::ifstream in(p,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
void Write(const fs::path& p,const std::string& value){std::ofstream out(p,std::ios::binary);out.write(value.data(),static_cast<std::streamsize>(value.size()));assert(out.good());}
struct Job{
 fs::path directory;std::unique_ptr<sr::DedicatedHostProcess> process;std::vector<std::vector<std::string>> rows;bool opened=false,ready=false,committed=false;uint64_t count=0;double deadline;
 Job(fs::path dir,const std::string& kind,const std::string& source,size_t expected,double timeout=5000):directory(std::move(dir)),deadline(af::Now()+timeout){struct stat info{};assert(stat(directory.c_str(),&info)==0);process=std::make_unique<sr::DedicatedHostProcess>("/proc/self/exe",std::vector<std::string>{"--attachment-worker"});assert(process->Send("INIT 1 "+kind+" "+std::to_string(expected)+" "+af::Hex(directory.string())+" "+af::Hex("actual.bin")+" "+af::Hex(source)+" "+std::to_string(deadline)+" "+std::to_string(static_cast<uint64_t>(info.st_dev))+" "+std::to_string(static_cast<uint64_t>(info.st_ino))));}
 void Poll(){for(const auto& line:process->Poll(5)){auto row=af::Fields(line);assert(!row.empty());if(row[0]=="OPEN")opened=true;else if(row[0]=="READY"){ready=true;count=af::UInt(row[2]);}else if(row[0]=="COMMITTED")committed=true;rows.push_back(std::move(row));}}
 void Until(const std::function<bool()>& done){const auto end=af::Now()+10000;while(!done()){assert(af::Now()<end);Poll();if(process->Exited())break;}}
 void Body(const std::string& bytes){Until([&]{return opened;});assert(opened);if(!bytes.empty()){assert(process->Send("CHUNK 1 1 "+af::Hex(bytes)));Until([&]{return !rows.empty()&&rows.back()[0]=="CHUNK_OK";});}assert(process->Send("FINISH 1"));Until([&]{return ready;});}
 void Exit(){Until([&]{return process->Exited().has_value();});assert(process->Exited());Poll();assert(process->Stdout().empty()&&process->Stderr().empty());}
 bool Error(const std::string& stage,int error){for(const auto& r:rows)if(r[0]=="ERROR"&&af::UInt(r[2])==static_cast<uint64_t>(error)&&af::Unhex(r[3],100)==stage)return true;return false;}
};
int main(int argc,char** argv){if(argc==2&&std::string_view(argv[1])=="--attachment-worker")return af::Worker();std::string pattern="/tmp/suitecut-native-attachments-XXXXXX";std::vector<char> chars(pattern.begin(),pattern.end());chars.push_back(0);const auto made=mkdtemp(chars.data());assert(made);const fs::path root=made;const auto out=root/"owned";fs::create_directory(out);const std::string bytes("real\0\xff\x01",7);
 {Job j(out,"B","",bytes.size());j.Body(bytes);assert(j.ready&&j.count==bytes.size()&&fs::is_empty(out)&&!j.process->Exited());assert(j.process->Send("COMMIT 1"));j.Exit();assert(j.committed&&!j.process->Exited()->signaled&&j.process->Exited()->code==0&&Read(out/"actual.bin")==bytes);fs::remove(out/"actual.bin");}
 // Deterministically let the real child exit before draining its final IPC.
 {Job j(out,"B","",bytes.size());j.Body(bytes);assert(j.process->Send("COMMIT 1"));const auto end=af::Now()+5000;while(!j.process->Exited()){assert(af::Now()<end);poll(nullptr,0,1);}assert(!j.committed&&j.process->Exited()->code==0&&Read(out/"actual.bin")==bytes);j.Exit();assert(j.committed&&!j.process->Exited()->signaled&&j.process->Exited()->code==0&&Read(out/"actual.bin")==bytes);fs::remove(out/"actual.bin");}
 {Write(root/"source",bytes);Job j(out,"P",(root/"source").string(),0);j.Until([&]{return j.ready;});assert(j.ready&&fs::is_empty(out));assert(j.process->Send("COMMIT 1"));j.Exit();assert(j.committed&&Read(out/"actual.bin")==bytes);fs::remove(out/"actual.bin");}
 {Job j(out,"B","",0);j.Body("");assert(j.ready&&fs::is_empty(out));j.process->Kill();j.Exit();assert(j.process->Exited()->signaled&&j.process->Exited()->code==SIGKILL&&!j.committed&&fs::is_empty(out));}
 {Job j(out,"B","",0,200);j.Body("");assert(j.ready);j.Exit();assert(j.Error("deadline",0)&&!j.committed&&!j.process->Exited()->signaled&&j.process->Exited()->code==1&&fs::is_empty(out));}
 {Write(out/"actual.bin","foreign");Job j(out,"B","",bytes.size());j.Body(bytes);assert(j.process->Send("COMMIT 1"));j.Exit();assert(j.Error("publish-link",EEXIST)&&!j.committed&&Read(out/"actual.bin")=="foreign");fs::remove(out/"actual.bin");}
 {Job j(out,"P","",0);j.Exit();assert(j.Error("source-open",ENOENT)&&!j.committed&&fs::is_empty(out));}
 {rlimit previous{};assert(getrlimit(RLIMIT_FSIZE,&previous)==0);auto limited=previous;limited.rlim_cur=4;assert(setrlimit(RLIMIT_FSIZE,&limited)==0);Job j(out,"B","",bytes.size());assert(setrlimit(RLIMIT_FSIZE,&previous)==0);j.Until([&]{return j.opened;});assert(j.opened&&j.process->Send("CHUNK 1 1 "+af::Hex(bytes)));j.Exit();assert(j.Error("write",EFBIG)&&!j.committed&&fs::is_empty(out));}
 fs::remove_all(root);std::cout<<"PASS8 actual native anonymous artifact body/path bytes, two-phase publication, deadline/cancel, no-replace, ENOENT and real EFBIG groups\n";
}
