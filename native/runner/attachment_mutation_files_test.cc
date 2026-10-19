// SPDX-License-Identifier: MIT
#include "runner_attachments.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <set>
using namespace suitecut::runner;
// This executable uses the real native writer/rollback entry points. The
// callback owner is a native controller fixture, not an authored Node host.
int main(int argc,char** argv){
  if(argc==2&&std::string_view(argv[1])=="--native-attachment-worker")return attachment_files::Worker();
  if(argc==2&&std::string_view(argv[1])=="--native-attachment-cleanup")return attachment_files::Cleanup();
  assert(argc==3);const std::filesystem::path parent=argv[1];std::filesystem::create_directories(parent);
  auto pattern=(parent/"mutable-attachments-XXXXXX").string();std::vector<char> path(pattern.begin(),pattern.end());path.push_back(0);
  const auto made=mkdtemp(path.data());assert(made);const std::filesystem::path out=made;
  std::cout<<"ACTUAL native file evidence "<<out.string()<<std::endl;
  const auto fault=std::filesystem::absolute(argv[2]).string();assert(std::filesystem::is_regular_file(fault));
  assert(!setenv("LD_PRELOAD",fault.c_str(),1));
  const auto now=[](){return DedicatedRealMilliseconds();};
  const auto marker=out/"writer-fsync.marker",release=out/"writer-fsync.release";
  assert(!setenv("SUITECUT_ATTACHMENT_TEST_HOLD","file",1));
  assert(!setenv("SUITECUT_ATTACHMENT_TEST_MARKER",marker.c_str(),1));
  assert(!setenv("SUITECUT_ATTACHMENT_TEST_RELEASE",release.c_str(),1));
  std::vector<std::vector<std::string>> packets;std::vector<std::string> failures;
  RunnerAttachments::Budget budget;
  RunnerAttachments actor(out,{[&](const auto& line){packets.push_back(attachment_files::Fields(line));},[&](const auto& error){failures.push_back(error);},{},[&](const auto&,const auto& error){failures.push_back(error);}},&budget);
  const StepOwner body{1,1,0,1,1,1,getpid(),"body"};actor.WorkerStarted();actor.BeginCallback(body,now()+15000,now());
  const auto send=[&](std::vector<std::string> fields){assert(actor.Packet(fields,now(),false));};
  send({"ATTACH_MUTATE","1","0","1","1","0","0","S","0","-"});
  const auto name=StepWire("\"held real writer\""),type=StepWire("\"text/plain\"");
  const std::string bytes="real bytes preserved after view withdrawal";
  send({"ATTACH_APPEND_BEGIN","2","0","1","1",name,"B",std::to_string(bytes.size()),type,"-"});
  size_t cursor=0;std::map<uint64_t,std::string> data{{2,bytes}};std::set<uint64_t> chunked,finished;
  const auto pump=[&](){
    actor.Tick(now());
    while(cursor<packets.size()){
      const auto packet=packets[cursor++];if(packet.empty())continue;
      if(packet[0]=="ATTACH_ADMITTED"){
        const auto request=attachment_files::UInt(packet[1]);assert(packet.size()==8&&packet[6]=="1");
        if(chunked.insert(request).second){const auto& value=data.at(request);send({"ATTACH_CHUNK",packet[5],packet[2],packet[3],packet[4],"1",attachment_files::Hex(value)});}
      }else if(packet[0]=="ATTACH_CHUNK_OK"){
        const auto request=attachment_files::UInt(packet[1]);if(finished.insert(request).second)send({"ATTACH_FINISH",packet[5],packet[2],packet[3],packet[4]});
      }
    }
  };
  const auto until=[&](auto ready){const auto deadline=now()+10000;while(!ready()){assert(now()<deadline);pump();poll(nullptr,0,1);}};
  const auto read_marker=[&](const auto& path){std::ifstream input(path);std::string line;if(!std::getline(input,line)||input.eof())return std::vector<std::string>{};auto fields=attachment_files::Fields(line);return fields.size()==5?fields:std::vector<std::string>{};};
  until([&]{return !read_marker(marker).empty();});const auto held=read_marker(marker);
  const auto writer=attachment_files::UInt(held[0]);assert(writer!=static_cast<uint64_t>(getpid())&&!kill(static_cast<pid_t>(writer),0)&&held[3]=="0");
  assert(actor.Physical()==1&&budget.physical==1&&actor.Records().empty());
  send({"ATTACH_MUTATE","3","0","1","1","1","0","S","0","d2"});
  send({"ATTACH_APPEND_BEGIN","4","0","1","1",StepWire("\"reference only\""),"R","0",type,StepWire("\"missing\\ud800\"")});
  send({"ATTACH_MUTATE","5","0","1","1","2","0","S","0","d4,h,d2"});
  send({"ATTACH_MUTATE","6","0","1","1","3","0","S","0","d4"});
  bool forwarded=false;std::optional<std::string> original;
  actor.FenceWithAppendFailure(0,1,1,[&](const auto& error){forwarded=true;original=error;},now());
  assert(!forwarded&&actor.Physical()==1&&budget.physical==1&&actor.Declarations()[0].receipt==AttachmentDeclarations::Receipt::Pending);
  {std::ofstream gate(release);gate<<"release actual fsync\n";}
  until([&]{return actor.IdleReady();});assert(forwarded&&!original&&actor.Physical()==0&&budget.physical==0&&failures.empty()&&actor.Records().size()==1);
  const auto committed=actor.Records()[0];assert(committed.writer_pid==static_cast<int64_t>(writer)&&committed.writer_exit==0&&!committed.writer_signaled&&committed.bytes==bytes.size());
  struct stat actual{};assert(!stat(committed.path.c_str(),&actual)&&static_cast<uint64_t>(actual.st_ino)==committed.inode&&static_cast<uint64_t>(actual.st_dev)==committed.device);
  std::ifstream content(committed.path,std::ios::binary);const std::string read((std::istreambuf_iterator<char>(content)),{});assert(read==bytes);
  size_t journals=0;actor.ForEachAttachmentProjection([&](const auto& journal){++journals;assert(journal.Projection().size()==1&&journal.Projection()[0]->id==actor.Declarations()[1].id&&journal.Changes().size()==4&&journal.References().size()==2);});assert(journals==1&&actor.Declarations()[0].receipt==AttachmentDeclarations::Receipt::Committed);
  // A real ordinary attach now commits and retires its real writer before its
  // exact native publication-view receipt may advance the callback.
  assert(!unsetenv("SUITECUT_ATTACHMENT_TEST_HOLD"));auto hook=body;hook.callback_kind="afterEach";hook.callback_token=9;hook.scope=2;
  actor.BeginCallback(hook,now()+10000,now());data.emplace(7,"ordinary actual bytes");
  send({"ATTACH_BEGIN","7","9","2","1",StepWire("\"ordinary\""),"B",std::to_string(data.at(7).size()),type,"-"});
  until([&]{return actor.Records().size()==2&&actor.Physical()==0;});
  assert(budget.physical==0&&actor.Pending(9,2,1)&&!actor.IdleReady());
  std::vector<std::string> publication;for(const auto& packet:packets)if(packet[0]=="ATTACH_PUBLISHED"&&packet[1]=="7")publication=packet;
  assert(publication.size()==11&&attachment_files::UInt(publication[10])>0);
  bool published_forward=false;actor.FenceWithAppendFailure(9,2,1,[&](const auto& error){assert(!error);published_forward=true;},now());assert(!published_forward);
  send({"ATTACH_MUTATE","8","9","2","1","4","0","P",publication[10],publication[5]});
  assert(published_forward&&actor.IdleReady()&&actor.PublicationViewReceipts().size()==1&&actor.PublicationViewReceipts()[0].accepted&&actor.Physical()==0);
  // A removed body that really fails EFBIG retains its rollback job and actual
  // errno history until real fsync/drain/waitpid receipts, independently of view.
  const auto cleanup_marker=out/"cleanup-fsync.marker",cleanup_release=out/"cleanup-fsync.release";
  assert(!setenv("SUITECUT_ATTACHMENT_TEST_HOLD","cleanup",1));assert(!setenv("SUITECUT_ATTACHMENT_TEST_EFBIG","1",1));
  assert(!setenv("SUITECUT_ATTACHMENT_TEST_MARKER",cleanup_marker.c_str(),1));assert(!setenv("SUITECUT_ATTACHMENT_TEST_RELEASE",cleanup_release.c_str(),1));
  auto failing=hook;failing.callback_token=10;failing.scope=3;actor.BeginCallback(failing,now()+10000,now());
  data.emplace(9,"real oversized write");send({"ATTACH_APPEND_BEGIN","9","10","3","1",name,"B",std::to_string(data.at(9).size()),type,"-"});
  send({"ATTACH_MUTATE","10","10","3","1","5","0","S","0","d9"});
  send({"ATTACH_MUTATE","11","10","3","1","6","0","S","0","-"});
  bool failed_forward=false;std::optional<std::string> failed;
  actor.FenceWithAppendFailure(10,3,1,[&](const auto& error){failed_forward=true;failed=error;},now());
  until([&]{return !read_marker(cleanup_marker).empty();});const auto cleanup=read_marker(cleanup_marker);
  assert(cleanup[3]=="1"&&!failed_forward&&actor.Physical()==1&&budget.physical==1&&actor.Records().size()==2);
  {std::ofstream gate(cleanup_release);gate<<"release actual cleanup fsync\n";}
  until([&]{return actor.IdleReady();});assert(failed_forward&&failed&&failed->find("errno=27")!=std::string::npos&&actor.Physical()==0&&budget.physical==0&&actor.Declarations().back().receipt==AttachmentDeclarations::Receipt::Failed);
  actor.ForEachAttachmentProjection([&](const auto& journal){assert(journal.Projection().empty()&&journal.Revision()==7&&journal.References().size()==4);});
  assert(std::filesystem::is_regular_file(committed.path));
  std::cout<<"PASS 3 actual native filewriter scopes: held fsync withdraw/replace/order, committed-file publication ACK fence, removed EFBIG held rollback; native callback fixtures, no Node/CEF host claim\n";
}
