// SPDX-License-Identifier: MIT
#include "work_group_pool.h"
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
    sr::ProjectGraph graph({{"setup",{},std::nullopt,true},{"body",{"setup"},std::nullopt,true}});
    const auto selection=graph.Select();sr::TestWorkGroups groups({{1,0},{2,0},{3,1}},{},{},{},selection,{{1,2},{3}},{true,true});
    sr::WorkGroupPool pool(std::move(graph),{selection,false},std::move(groups),3);
    const auto first=*pool.Next(),second=*pool.Next();Check(first.project==0&&second.project==0,"native setup group selection rejected");
    const auto original_deadline=Now()+5000;Child a(original_deadline),b(original_deadline);a.Ready();b.Ready();a.Living();b.Living();
    Check(a.Pid()!=b.Pid()&&pool.WorkerStarted(first,1,a.Pid())&&pool.WorkerStarted(second,2,b.Pid()),"actual distinct child worker admission rejected");
    Check(pool.Complete(first,sr::WorkGroupPool::Outcome::Passed)&&!pool.Retired(first)&&!pool.Next(),"logical completion released a real held group");
    a.Living();b.Living();const auto a_pid=a.Exit();Check(pool.WorkerExited(first,1,a_pid)&&pool.Retired(first),"actual first known-PID exit not admitted");
    Check(pool.Projects()[0].state==sr::WorkGroupPool::State::Running&&!pool.Next(),"dependency advanced before all actual group exits");
    b.Living();const auto b_pid=b.Exit();Check(pool.WorkerExited(second,2,b_pid)&&!pool.Retired(second)&&!pool.Next(),"physical exit alone released logical group completion");
    Check(pool.Complete(second,sr::WorkGroupPool::Outcome::Passed)&&pool.Retired(second)&&pool.Projects()[0].state==sr::WorkGroupPool::State::Passed,"actual second group retirement rejected");
    const auto dependent=pool.Next();Check(dependent&&dependent->project==1,"dependency missing after all genuine group exits");
    pool.Stop("kernel control ends before dependent worker admission");Check(pool.CanceledBeforeStart(*dependent)&&pool.Done(),"real native never-started cancellation missing");
    std::cout<<"Native work groups: 2 actual known-PID kernel retirement controls passed\n";return 0;
  }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
