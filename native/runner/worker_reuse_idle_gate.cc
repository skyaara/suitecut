// SPDX-License-Identifier: MIT
// Test-only syscall observer. Holds genuine SDK idle-ACK bytes unchanged until
// the controller observes actual native teardown sends after a real SIGINT.
// Never linked/preloaded into the production executable by default.
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <unistd.h>
namespace {
std::atomic<bool> held{false};
bool Enabled(){return std::getenv("SUITECUT_REUSE_IDLE_GATE")!=nullptr;}
bool FullWrite(int fd,const void* data,size_t size){
  const auto* p=static_cast<const char*>(data);
  while(size){const auto n=syscall(SYS_write,fd,p,size);if(n<0&&errno==EINTR)continue;if(n<=0)return false;p+=n;size-=static_cast<size_t>(n);}return true;
}
int Notify(const char* kind,int original,const char* bytes,size_t size,ssize_t result){
  const char* path=std::getenv("SUITECUT_REUSE_IDLE_GATE");if(!path||std::strlen(path)>=sizeof(sockaddr_un::sun_path)){errno=EINVAL;return -1;}
  const int socket=::socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0);if(socket<0)return -1;
  sockaddr_un address{};address.sun_family=AF_UNIX;std::memcpy(address.sun_path,path,std::strlen(path)+1);
  if(::connect(socket,reinterpret_cast<const sockaddr*>(&address),sizeof(address))){const auto saved=errno;::close(socket);errno=saved;return -1;}
  char header[160];const auto count=std::snprintf(header,sizeof(header),"%s %ld %ld %d %zu %zd\n",kind,static_cast<long>(getpid()),static_cast<long>(getppid()),original,size,result);
  if(count<=0||static_cast<size_t>(count)>=sizeof(header)||!FullWrite(socket,header,static_cast<size_t>(count))||!FullWrite(socket,bytes,size)){::close(socket);errno=EIO;return -1;}return socket;
}
bool Hold(int fd,const char* bytes,size_t size){
  constexpr char prefix[]="WORKER_IDLE_READY ";
  if(fd!=3||size<sizeof(prefix)-1||std::memcmp(bytes,prefix,sizeof(prefix)-1)||!Enabled()||held.exchange(true))return true;
  const int observer=Notify("ACK_HELD",fd,bytes,size,-1);if(observer<0)return false;
  char command=0;ssize_t count;do{count=syscall(SYS_read,observer,&command,1);}while(count<0&&errno==EINTR);::close(observer);
  if(count!=1||command!='G'){errno=EIO;return false;}return true;
}
void Observed(const char* kind,int fd,const char* bytes,size_t size,ssize_t result){
  const auto saved=errno;const int observer=Notify(kind,fd,bytes,size,result);if(observer>=0)::close(observer);errno=saved;
}
}
extern "C" ssize_t write(int fd,const void* buffer,size_t size){
  const auto* bytes=static_cast<const char*>(buffer);if(!Hold(fd,bytes,size))return -1;
  const auto result=syscall(SYS_write,fd,buffer,size);
  if(Enabled()&&fd==3&&size>=18&&!std::memcmp(bytes,"WORKER_IDLE_READY ",18))Observed("ACK_WRITTEN",fd,bytes,size,result);
  return result;
}
extern "C" ssize_t writev(int fd,const iovec* rows,int count){
  char bytes[65536];size_t size=0;bool candidate=false;
  if(fd==3&&count>0&&Enabled()){
    char prefix[18];size_t got=0;
    for(int i=0;i<count&&got<sizeof(prefix);++i){const auto part=rows[i].iov_len<sizeof(prefix)-got?rows[i].iov_len:sizeof(prefix)-got;std::memcpy(prefix+got,rows[i].iov_base,part);got+=part;}
    candidate=got==sizeof(prefix)&&!std::memcmp(prefix,"WORKER_IDLE_READY ",sizeof(prefix));
    if(candidate)for(int i=0;i<count;++i){if(rows[i].iov_len>sizeof(bytes)-size){errno=EOVERFLOW;return -1;}std::memcpy(bytes+size,rows[i].iov_base,rows[i].iov_len);size+=rows[i].iov_len;}
    if(candidate&&!Hold(fd,bytes,size))return -1;
  }
  const auto result=syscall(SYS_writev,fd,rows,count);if(candidate)Observed("ACK_WRITTEN",fd,bytes,size,result);return result;
}
extern "C" ssize_t send(int fd,const void* buffer,size_t size,int flags){
  const auto result=syscall(SYS_sendto,fd,buffer,size,flags,nullptr,0);
  const auto* bytes=static_cast<const char*>(buffer);
  if(Enabled()&&result>0&&size>=9&&!std::memcmp(bytes,"TEARDOWN ",9))Observed("TEARDOWN_SENT",fd,bytes,static_cast<size_t>(result),result);
  return result;
}
