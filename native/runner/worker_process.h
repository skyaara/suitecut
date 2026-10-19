// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <deque>
#include <fcntl.h>
#include <optional>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>
#include <vector>
namespace suitecut::runner {
inline double RealMilliseconds(){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();}
class WorkerProcess {
 public:
  struct Exit {bool signaled;int code;};
  WorkerProcess(const std::string& executable,const std::vector<std::string>& args){
    int in[2],out[2],err[2];
    if(socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0,in))throw std::runtime_error("Native host stdin pipe failed");
    if(pipe2(out,O_CLOEXEC)){::close(in[0]);::close(in[1]);throw std::runtime_error("Native host stdout pipe failed");}
    if(pipe2(err,O_CLOEXEC)){for(int fd:in)::close(fd);for(int fd:out)::close(fd);throw std::runtime_error("Native host stderr pipe failed");}
    std::vector<char*> argv;argv.push_back(const_cast<char*>(executable.c_str()));for(const auto& a:args)argv.push_back(const_cast<char*>(a.c_str()));argv.push_back(nullptr);
    const pid_t parent=getpid();
    pid_=fork();
    if(pid_==0){
      if(prctl(PR_SET_PDEATHSIG,SIGKILL)!=0||getppid()!=parent)_exit(126);
      if(dup2(in[0],STDIN_FILENO)<0||dup2(out[1],STDOUT_FILENO)<0||dup2(err[1],STDERR_FILENO)<0)_exit(126);
      for(int fd:in)::close(fd);
      for(int fd:out)::close(fd);
      for(int fd:err)::close(fd);
      execvp(executable.c_str(),argv.data());_exit(127);
    }
    ::close(in[0]);::close(out[1]);::close(err[1]);input_=in[1];output_=out[0];errors_=err[0];
    if(pid_<0){CloseFDs();throw std::runtime_error("Native host fork failed");}
    for(int fd:{input_,output_,errors_}){const auto flags=fcntl(fd,F_GETFL,0);if(flags<0||fcntl(fd,F_SETFL,flags|O_NONBLOCK)<0){Kill();WaitBlocking();CloseFDs();throw std::runtime_error("Native host nonblocking IO setup failed");}}
  }
  ~WorkerProcess()noexcept{if(pid_>0&&!exit_){Kill();WaitBlocking();}CloseFDs();}
  WorkerProcess(const WorkerProcess&)=delete;WorkerProcess& operator=(const WorkerProcess&)=delete;
  pid_t Pid()const{return pid_;}
  bool Send(std::string line){
    if(exit_||line.size()>65536||line.find('\n')!=std::string::npos||writes_.size()>=64||write_bytes_+line.size()+1>1048576)return false;
    line+='\n';write_bytes_+=line.size();writes_.push_back(std::move(line));Flush();return true;
  }
  std::vector<std::string> Poll(int milliseconds=10){
    Flush();std::array<pollfd,2> fds{{{output_,POLLIN,0},{errors_,POLLIN,0}}};
    const auto ready=::poll(fds.data(),fds.size(),milliseconds);if(ready<0&&errno!=EINTR)throw std::runtime_error("Native host poll failed");
    Read(output_,output_text_,1048576);Read(errors_,stderr_,1048576);
    std::vector<std::string> lines;size_t position;
    while((position=output_text_.find('\n'))!=std::string::npos){if(position>65536)throw std::runtime_error("Native host protocol line limit exceeded");lines.push_back(output_text_.substr(0,position));output_text_.erase(0,position+1);}
    if(output_text_.size()>65536)throw std::runtime_error("Native host partial protocol line limit exceeded");
    ObserveExit();return lines;
  }
  std::optional<Exit> Exited(){ObserveExit();return exit_;}
  const std::string& Stderr()const{return stderr_;}
  void Kill(){if(pid_>0&&!exit_&&!wait_error_)::kill(pid_,SIGKILL);}
  void Terminate(double now,double grace){if(exit_||wait_error_||termination_deadline_)return;::kill(pid_,SIGTERM);termination_deadline_=now+grace;}
  void Tick(double now){ObserveExit();if(!exit_&&termination_deadline_&&now>=termination_deadline_)Kill();}
 private:
  friend struct WorkerProcessTestAccess;
  static void Read(int fd,std::string& into,size_t limit){
    std::array<char,4096> data;
    for(;;){const auto count=::read(fd,data.data(),data.size());if(count>0){if(into.size()+static_cast<size_t>(count)>limit)throw std::runtime_error("Native host stream limit exceeded");into.append(data.data(),static_cast<size_t>(count));continue;}if(count<0&&errno==EINTR)continue;if(count<0&&errno!=EAGAIN&&errno!=EWOULDBLOCK)throw std::runtime_error("Native host read failed");return;}
  }
  void Flush(){
    while(!writes_.empty()){auto& line=writes_.front();const auto count=::send(input_,line.data()+write_offset_,line.size()-write_offset_,MSG_NOSIGNAL);if(count>0){write_offset_+=static_cast<size_t>(count);write_bytes_-=static_cast<size_t>(count);if(write_offset_==line.size()){writes_.pop_front();write_offset_=0;}continue;}if(count<0&&errno==EINTR)continue;if(count<0&&(errno==EAGAIN||errno==EWOULDBLOCK))return;return;}
  }
  void ObserveExit(){
    if(exit_||pid_<=0)return;
    if(wait_error_)throw std::runtime_error("Native host exit status unavailable after waitpid error");
    int status=0;const auto result=waitpid(pid_,&status,WNOHANG);
    if(result==pid_)exit_=Exit{WIFSIGNALED(status),WIFSIGNALED(status)?WTERMSIG(status):WEXITSTATUS(status)};
    else if(result<0&&errno!=EINTR){wait_error_=errno;throw std::runtime_error("Native host waitpid failed");}
  }
  bool WaitBlocking()noexcept{
    if(exit_)return true;
    if(wait_error_||pid_<=0)return false;
    int status=0;pid_t result;
    do{result=waitpid(pid_,&status,0);}while(result<0&&errno==EINTR);
    if(result!=pid_){wait_error_=result<0?errno:EAGAIN;return false;}
    exit_=Exit{WIFSIGNALED(status),WIFSIGNALED(status)?WTERMSIG(status):WEXITSTATUS(status)};return true;
  }
  void CloseFDs(){for(int* fd:{&input_,&output_,&errors_})if(*fd>=0){::close(*fd);*fd=-1;}}
  pid_t pid_=-1;int input_=-1,output_=-1,errors_=-1;std::optional<Exit> exit_;std::optional<int> wait_error_;std::deque<std::string> writes_;size_t write_bytes_=0,write_offset_=0;std::string output_text_,stderr_;double termination_deadline_=0;
};
}
