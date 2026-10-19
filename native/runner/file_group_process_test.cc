// SPDX-License-Identifier: MIT
#include "file_group_pool.h"
#include <chrono>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>
namespace sr=suitecut::runner;
static void Check(bool okay,const char* message){if(!okay)throw std::runtime_error(message);}
static double Now(){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();}
// These are real kernel children blocked on an actual pipe. The parent always
// waits the known PID; no process-wide subreaper or waitpid(-1) is introduced.
class Child {
 public:
  explicit Child(double deadline):deadline_(deadline){
    int send[2],receive[2];Check(pipe2(send,O_CLOEXEC)==0,"actual child input pipe failed");
    if(pipe2(receive,O_CLOEXEC)!=0){close(send[0]);close(send[1]);throw std::runtime_error("actual child output pipe failed");}
    pid_=fork();
    if(pid_==0){
      close(send[1]);close(receive[0]);char byte='R';
      if(write(receive[1],&byte,1)!=1)_exit(21);
      if(read(send[0],&byte,1)!=1||byte!='G')_exit(22);
      byte='D';if(write(receive[1],&byte,1)!=1)_exit(23);_exit(0);
    }
    close(send[0]);close(receive[1]);
    if(pid_<0){close(send[1]);close(receive[0]);throw std::runtime_error("actual child fork failed");}
    input_=send[1];output_=receive[0];
  }
  Child(const Child&)=delete;Child& operator=(const Child&)=delete;
  ~Child(){
    if(pid_>0){
      int status=0;pid_t result;do{result=waitpid(pid_,&status,WNOHANG);}while(result<0&&errno==EINTR);
      if(result==0){
        if(kill(pid_,SIGKILL)<0&&errno!=ESRCH)std::cerr<<"Known-child cleanup signal failed\n";
        do{result=waitpid(pid_,&status,0);}while(result<0&&errno==EINTR);
      }
      if(result<0&&errno!=ECHILD)std::cerr<<"Known-child cleanup waitpid failed\n";
    }
    if(input_>=0)close(input_);
    if(output_>=0)close(output_);
  }
  pid_t Pid()const{return pid_;}
  void Ready(){Check(Read()=='R',"actual child ready receipt rejected");}
  void Living(){int status;Check(waitpid(pid_,&status,WNOHANG)==0,"actual held child unexpectedly exited");}
  pid_t Exit(){
    const auto original=pid_;const char byte='G';Check(write(input_,&byte,1)==1,"actual child release failed");
    Check(Read()=='D',"actual child completion IO receipt rejected");
    int status=0;pid_t result;do{result=waitpid(pid_,&status,0);}while(result<0&&errno==EINTR);
    if(result==original)pid_=0;
    Check(result==original&&WIFEXITED(status)&&WEXITSTATUS(status)==0,"actual child waitpid/status receipt rejected");
    return original;
  }
 private:
  char Read(){
    pollfd fd{output_,POLLIN,0};
    while(true){
      const double remaining=deadline_-Now();Check(remaining>0,"original native process-control deadline exceeded");
      const int result=poll(&fd,1,static_cast<int>(remaining)+1);
      if(result<0&&errno==EINTR)continue;
      Check(result>0&&(fd.revents&POLLIN),"actual pipe readiness failed");
      char byte=0;Check(read(output_,&byte,1)==1,"actual child pipe receipt missing");return byte;
    }
  }
  double deadline_;pid_t pid_=0;int input_=-1,output_=-1;
};
int main(){
  std::signal(SIGPIPE,SIG_IGN);
  try{
    char file_path[]="/tmp/suitecut-file-group-kernel-XXXXXX";
    const auto fd=mkstemp(file_path);Check(fd>=0,"actual suite file creation failed");
    Check(write(fd,"x",1)==1&&close(fd)==0,"actual suite file write/close failed");
    sr::SuiteFileOwner file(1,file_path);Check(unlink(file_path)==0,"actual suite file unlink failed");
    sr::NativeTestGroupTree tree({1,2,3},{},{},{});
    sr::FileGroupPool pool(sr::FileWorkGroups(file.Token(),tree,{1,2,3},true),2);
    const auto first=*pool.Next(),second=*pool.Next();Check(first.file==file.Token()&&second.file==file.Token(),"native actual file group selection rejected");
    const auto original_deadline=Now()+5000;Child a(original_deadline),b(original_deadline);a.Ready();b.Ready();a.Living();b.Living();
    Check(a.Pid()!=b.Pid()&&pool.WorkerStarted(first,1,a.Pid())&&pool.WorkerStarted(second,2,b.Pid()),"actual distinct child worker admission rejected");
    Check(pool.Complete(first,sr::FileGroupPool::Outcome::Passed)&&!pool.Retired(first)&&!pool.Done()&&!pool.Next(),"logical completion released a real held group");
    a.Living();b.Living();const auto a_pid=a.Exit();Check(pool.WorkerExited(first,1,a_pid)&&pool.Retired(first),"actual first known-PID exit not admitted");
    Check(!pool.Done()&&pool.Physical()==1,"file aggregate advanced before all actual group exits");
    b.Living();const auto b_pid=b.Exit();Check(pool.WorkerExited(second,2,b_pid)&&!pool.Retired(second)&&!pool.Done(),"physical exit alone released logical group completion");
    Check(pool.Complete(second,sr::FileGroupPool::Outcome::Passed)&&pool.Retired(second),"actual second group retirement rejected");
    const auto remaining=pool.Next();Check(remaining&&remaining->index==2,"remaining native file group missing after actual exits");
    pool.Stop();Check(pool.CanceledBeforeStart(*remaining)&&pool.Done(),"exact real file never-started cancellation missing");
    Check(file.Retire()>=file.Opened(),"actual suite file close receipt missing after all group exits");
    std::cout<<"Native anonymous file groups: 2 actual known-PID kernel retirement controls passed\n";return 0;
  }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
