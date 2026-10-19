// SPDX-License-Identifier: MIT
#include "descendant_owner.h"
#include "retained_source_process.h"
#include <fstream>
#include <map>
#include <memory>
#include <thread>
namespace sr=suitecut::runner;
namespace {
volatile sig_atomic_t interrupted=0;
void Interrupted(int value){if(!interrupted)interrupted=value;}
struct Options {std::string report;double timeout=120000,grace=1000,physical=6000;std::vector<std::string> command;};
double Number(const char* value){size_t end=0;const std::string text=value;const double n=std::stod(text,&end);if(end!=text.size()||!std::isfinite(n)||n<0||n>3600000)throw std::invalid_argument("Invalid supervisor bound");return n;}
Options Parse(int argc,char** argv){Options o;for(int i=1;i<argc;++i){const std::string flag=argv[i];if(flag=="--"){for(++i;i<argc;++i)o.command.emplace_back(argv[i]);break;}if(i+1>=argc)throw std::invalid_argument("Missing supervisor option value");const char* value=argv[++i];if(flag=="--report")o.report=value;else if(flag=="--timeout-ms")o.timeout=Number(value);else if(flag=="--cleanup-grace-ms")o.grace=Number(value);else if(flag=="--physical-ms")o.physical=Number(value);else throw std::invalid_argument("Unknown supervisor option");}if(o.report.empty()||o.command.empty()||o.timeout<=0||o.physical<100||o.grace>o.physical)throw std::invalid_argument("Invalid supervisor configuration");return o;}
std::string Quote(const std::string& value){std::string result="\"";constexpr char hex[]="0123456789abcdef";for(const unsigned char ch:value){if(ch=='"'||ch=='\\'){result+='\\';result+=static_cast<char>(ch);}else if(ch<32){result+="\\u00";result+=hex[ch>>4];result+=hex[ch&15];}else result+=static_cast<char>(ch);}return result+'"';}
class ProofChild {
 public:
  struct Exit {bool signaled;int code;int raw;};
  explicit ProofChild(const std::vector<std::string>& command){
    int gate[2];if(pipe2(gate,O_CLOEXEC))throw std::runtime_error("Proof launch gate failed");
    std::vector<char*> args;for(const auto& value:command)args.push_back(const_cast<char*>(value.c_str()));args.push_back(nullptr);
    const pid_t parent=getpid();pid_=fork();
    if(!pid_){close(gate[1]);if(prctl(PR_SET_PDEATHSIG,SIGKILL)||getppid()!=parent)_exit(126);char ready=0;ssize_t n;do{n=read(gate[0],&ready,1);}while(n<0&&errno==EINTR);close(gate[0]);if(n!=1||ready!='G')_exit(126);execvp(args[0],args.data());_exit(127);}
    close(gate[0]);gate_=gate[1];if(pid_<0){close(gate_);gate_=-1;throw std::runtime_error("Proof fork failed");}
    identity_=sr::RetainedSourceProcess::ReadStat(pid_);if(identity_.parent!=getpid())throw std::runtime_error("Proof child parent changed");
  }
  ~ProofChild(){if(gate_>=0)close(gate_);} // No destructor invents/reaps a pending exit.
  pid_t Pid()const{return pid_;}
  uint64_t Start()const{return identity_.start;}
  void Release(){char go='G';ssize_t n;do{n=write(gate_,&go,1);}while(n<0&&errno==EINTR);close(gate_);gate_=-1;if(n!=1)throw std::runtime_error("Proof launch gate write failed");}
  std::optional<Exit> Exited(){if(exit_)return exit_;int status=0;pid_t result;do{result=waitpid(pid_,&status,WNOHANG);}while(result<0&&errno==EINTR);if(result<0)throw std::runtime_error("Proof original child waitpid failed");if(result==pid_){if(!WIFEXITED(status)&&!WIFSIGNALED(status))throw std::runtime_error("Proof child waitpid status invalid");exit_=Exit{WIFSIGNALED(status),WIFSIGNALED(status)?WTERMSIG(status):WEXITSTATUS(status),status};}return exit_;}
  void Signal(int signal){if(!Exited()&&kill(pid_,signal))throw std::runtime_error("Proof original child signal failed");}
 private:pid_t pid_=-1;int gate_=-1;sr::RetainedSourceProcess::Stat identity_;std::optional<Exit> exit_;
};
struct Identity {pid_t pid=0,parent=0;uint64_t start=0;char state=0;};
struct Result {pid_t supervisor=0,child=0;uint64_t child_start=0;std::optional<ProofChild::Exit> child_exit;bool timeout=false,retired=false;int interruption=0;std::string error;std::map<pid_t,Identity> identities;std::vector<sr::DescendantOwner::Exit> exits;};
bool CleanupFailed(const Result& r){bool failed=!r.retired||!r.error.empty();for(const auto& e:r.exits)failed|=e.signaled||e.code!=0||e.kill_requested;return failed;}
void Write(const Options& o,const Result& r){std::ofstream s(o.report);if(!s)throw std::runtime_error("Proof report open failed");s<<"{\n\"scope\":\"isolated native supervisor; inherited child argv/environment/stdin/stdout/stderr\",\n\"supervisorPID\":"<<r.supervisor<<",\"childPID\":"<<r.child<<",\"childStart\":"<<r.child_start<<",\n\"childExit\":";if(r.child_exit)s<<"{\"signaled\":"<<(r.child_exit->signaled?"true":"false")<<",\"code\":"<<r.child_exit->code<<",\"rawWaitpidStatus\":"<<r.child_exit->raw<<"}";else s<<"null";s<<",\n\"cleanupFailed\":"<<(CleanupFailed(r)?"true":"false")<<",\n\"timeout\":"<<(r.timeout?"true":"false")<<",\"interruption\":"<<r.interruption<<",\"descendantsRetired\":"<<(r.retired?"true":"false")<<",\"error\":"<<Quote(r.error)<<",\n\"descendants\":[";bool comma=false;for(const auto& e:r.exits){const auto found=r.identities.find(e.pid);if(found==r.identities.end())throw std::runtime_error("Missing actual descendant incarnation receipt");const auto& id=found->second;if(comma)s<<',';comma=true;s<<"{\"pid\":"<<id.pid<<",\"parentAtObservation\":"<<id.parent<<",\"start\":"<<id.start<<",\"stateBeforeWait\":"<<Quote(std::string(1,id.state))<<",\"signaled\":"<<(e.signaled?"true":"false")<<",\"code\":"<<e.code<<",\"killRequested\":"<<(e.kill_requested?"true":"false")<<"}";}s<<"],\n\"observedIdentities\":[";comma=false;for(const auto& [pid,id]:r.identities){(void)pid;if(comma)s<<',';comma=true;s<<"{\"pid\":"<<id.pid<<",\"parent\":"<<id.parent<<",\"start\":"<<id.start<<"}";}s<<"]\n}\n";s.flush();if(!s)throw std::runtime_error("Proof report write failed");}
}
int main(int argc,char** argv){Options options;try{options=Parse(argc,argv);}catch(const std::exception& e){dprintf(2,"Proof supervisor configuration: %s\n",e.what());return 125;}Result result;result.supervisor=getpid();std::unique_ptr<ProofChild> child;
  try{
    sr::DescendantOwner owner; // Before any child exists; one native reaper domain.
    struct sigaction action{};action.sa_handler=Interrupted;sigemptyset(&action.sa_mask);for(const int value:{SIGINT,SIGTERM,SIGHUP})if(sigaction(value,&action,nullptr))throw std::runtime_error("Proof supervisor signal setup failed");
    child=std::make_unique<ProofChild>(options.command);result.child=child->Pid();result.child_start=child->Start();owner.Begin(*child);child->Release();const double began=sr::DedicatedRealMilliseconds();double terminating=0;
    while(!child->Exited()){
      const double now=sr::DedicatedRealMilliseconds();if(!terminating&&(interrupted||now-began>=options.timeout)){result.timeout=!interrupted;result.interruption=interrupted;child->Signal(interrupted?interrupted:SIGTERM);terminating=now;}
      if(terminating&&now-terminating>=250)child->Signal(SIGKILL);
      if(terminating&&now-terminating>=options.physical)throw std::runtime_error("Proof original child exit unconfirmed after bounded termination");
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    result.child_exit=child->Exited();const double closing=sr::DedicatedRealMilliseconds();owner.BeginRetirement(*child,closing,options.grace);
    while(true){
      const auto observe=[&](pid_t pid){const auto stat=sr::RetainedSourceProcess::ReadStat(pid);if(stat.parent!=getpid())throw std::runtime_error("Proof adopted child not actually owned");const auto found=result.identities.find(pid);if(found!=result.identities.end()&&found->second.start!=stat.start)throw std::runtime_error("Proof adopted child incarnation changed");result.identities[pid]={pid,static_cast<pid_t>(stat.parent),stat.start,stat.state};};
      const double now=sr::DedicatedRealMilliseconds();bool retired=false;try{retired=owner.Tick(now,observe);}catch(const std::exception& error){if(result.error.empty())result.error=error.what();}result.exits=owner.Exits();if(retired){result.retired=true;break;}
      if(now-closing>=options.physical)throw std::runtime_error("Proof adopted descendant retirement exceeded physical cleanup bound");
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
  }catch(const std::exception& e){result.error=e.what();if(child&&!result.child_exit)result.child_exit=child->Exited();}
  result.interruption=interrupted;try{Write(options,result);}catch(const std::exception& e){dprintf(2,"Proof supervisor report: %s\n",e.what());return 125;}
  const bool cleanup_failed=CleanupFailed(result);
  if(result.child_exit&&result.child_exit->signaled){signal(result.child_exit->code,SIG_DFL);raise(result.child_exit->code);return 128+result.child_exit->code;}
  if(result.interruption){signal(result.interruption,SIG_DFL);raise(result.interruption);return 128+result.interruption;}
  if(!result.child_exit||cleanup_failed||result.timeout)return result.child_exit&&result.child_exit->code?result.child_exit->code:125;
  return result.child_exit->code;
}
