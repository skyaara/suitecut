// SPDX-License-Identifier: MIT
#include "worker_process.h"
#include "retry_plan.h"
#include <cassert>
#include <iostream>
namespace suitecut::runner {struct WorkerProcessTestAccess {static bool Wait(WorkerProcess& process){return process.WaitBlocking();}static bool Known(const WorkerProcess& process){return process.exit_.has_value();}static int Error(const WorkerProcess& process){return process.wait_error_.value_or(0);}};}
using namespace suitecut::runner;
int main(int argc,char** argv){
 assert(argc==2);
 WorkerProcess worker(argv[1],{"-e","process.stdout.write('HELLO '+process.pid+'\\n');process.stdin.once('data',d=>{process.stderr.write('genuine stderr');process.stdout.write('ACTUAL '+d.toString().trim()+'\\n');process.exitCode=3;process.stdin.destroy();});"});
 std::vector<std::string> lines;auto until=RealMilliseconds()+10000;
 while(lines.empty()&&RealMilliseconds()<until){auto read=worker.Poll();lines.insert(lines.end(),read.begin(),read.end());}
 assert(lines.size()==1&&lines[0]=="HELLO "+std::to_string(worker.Pid()));assert(worker.Send("payload"));
 while(!worker.Exited()&&RealMilliseconds()<until){auto read=worker.Poll();lines.insert(lines.end(),read.begin(),read.end());}
 auto read=worker.Poll(0);lines.insert(lines.end(),read.begin(),read.end());assert(worker.Exited()&&!worker.Exited()->signaled&&worker.Exited()->code==3);assert(lines.back()=="ACTUAL payload"&&worker.Stderr()=="genuine stderr");
 WorkerProcess held(argv[1],{"-e","process.on('SIGTERM',()=>{});console.log('READY');setInterval(()=>{},1000);"});lines.clear();until=RealMilliseconds()+10000;while(lines.empty()&&RealMilliseconds()<until)lines=held.Poll();assert(!lines.empty());
 RetryPlan retry({{1,"real process fence",{},1,false}});auto attempt=retry.Next(static_cast<uint64_t>(held.Pid()));assert(attempt);retry.Finish(static_cast<uint64_t>(held.Pid()),attempt->generation,Outcome::TimedOut,"actual native deadline");held.Terminate(RealMilliseconds(),30);
 assert(!retry.Next(99999));while(!held.Exited()&&RealMilliseconds()<until){held.Poll();held.Tick(RealMilliseconds());}assert(held.Exited()&&held.Exited()->signaled&&held.Exited()->code==SIGKILL);assert(retry.WorkerExited(static_cast<uint64_t>(held.Pid())));assert(retry.Next(99999));
 {
 WorkerProcess externally_reaped(argv[1],{"-e","process.exit(7)"});int status=0;assert(waitpid(externally_reaped.Pid(),&status,0)==externally_reaped.Pid());assert(WIFEXITED(status)&&WEXITSTATUS(status)==7);
 assert(!WorkerProcessTestAccess::Wait(externally_reaped));assert(!WorkerProcessTestAccess::Known(externally_reaped));assert(WorkerProcessTestAccess::Error(externally_reaped)==ECHILD);
 bool rejected=false;try{externally_reaped.Exited();}catch(const std::runtime_error&){rejected=true;}assert(rejected&&!WorkerProcessTestAccess::Known(externally_reaped));
 } // noexcept destructor must not fabricate exit0 or signal a reused PID.
 std::cout<<"Actual native Node host PID/stdio/exit/signal/retry-fence proof passed\n";
}
