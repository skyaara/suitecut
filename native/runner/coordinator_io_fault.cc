// SPDX-License-Identifier: MIT
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dlfcn.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
// The actual native helper completes fsync, then remains physically blocked at
// a FIFO. Parent cancellation must kill/reap it and perform genuine rollback.
// Native/SDK descriptors/PIDs/control packets are never proxied or replaced.
static void audit(const char* event,unsigned long long a,unsigned long long b,unsigned long long c){
 const char* path=getenv("SUITECUT_COORDINATOR_IO_AUDIT");if(!path)return;struct timespec now;if(clock_gettime(CLOCK_MONOTONIC,&now))return;
 char line[256];const int size=snprintf(line,sizeof(line),"%s %ld %llu %llu %llu %llu\n",event,(long)getpid(),a,b,c,(unsigned long long)now.tv_sec*1000000000ULL+(unsigned long long)now.tv_nsec);if(size<=0||(size_t)size>=sizeof(line))return;
 const int fd=open(path,O_WRONLY|O_CREAT|O_APPEND|O_CLOEXEC,0600);if(fd<0)return;(void)write(fd,line,(size_t)size);close(fd);
}
static int worker_kind(void){
 char bytes[16384];int fd=open("/proc/self/cmdline",O_RDONLY|O_CLOEXEC);if(fd<0)return 0;ssize_t count=read(fd,bytes,sizeof(bytes)-1);close(fd);if(count<=0)return 0;bytes[count]=0;
 unsigned argument=0;for(ssize_t i=0;i<count;){const char* arg=bytes+i;if(!strcmp(arg,"--native-attachment-worker"))return 1;if(!strcmp(arg,"--native-output-directory-worker"))return 2;if(!strcmp(arg,"--native-attachment-cleanup"))return 3;if(!strcmp(arg,"--native-output-directory-cleanup"))return 4;const char* base=strrchr(arg,'/');if(argument==1&&!strcmp(base?base+1:arg,"native-runner-host.js"))return 5;i+=(ssize_t)strlen(arg)+1;++argument;}return 0;
}
__attribute__((constructor)) static void started(void){const int kind=worker_kind();if(kind)audit("helper",(unsigned long long)getppid(),(unsigned long long)kind,0);}
extern "C" pid_t waitpid(pid_t pid,int* status,int options){
 static pid_t(*actual)(pid_t,int*,int)=NULL;if(!actual)actual=reinterpret_cast<pid_t(*)(pid_t,int*,int)>(dlsym(RTLD_NEXT,"waitpid"));if(!actual){errno=ENOSYS;return -1;}
 const pid_t result=actual(pid,status,options);const int saved=errno;if(result>0)audit("wait",(unsigned long long)result,status?(unsigned long long)*status:0,(unsigned long long)pid);errno=saved;return result;
}
extern "C" int unlinkat(int fd,const char* path,int flags){
 static int(*actual)(int,const char*,int)=NULL;if(!actual)actual=reinterpret_cast<int(*)(int,const char*,int)>(dlsym(RTLD_NEXT,"unlinkat"));if(!actual){errno=ENOSYS;return -1;}
 struct stat info;const int observed=fstatat(fd,path,&info,AT_SYMLINK_NOFOLLOW);const int result=actual(fd,path,flags);const int saved=errno;if(result==0&&observed==0)audit("unlink",(unsigned long long)info.st_dev,(unsigned long long)info.st_ino,(unsigned long long)flags);errno=saved;return result;
}
static void held(int fd,int kind){
 const char* marker=getenv("SUITECUT_COORDINATOR_IO_READY");const char* gate=getenv("SUITECUT_COORDINATOR_IO_GATE");const char* expected=getenv("SUITECUT_COORDINATOR_IO_KIND");if(!marker||!gate||!expected||atoi(expected)!=kind)return;
 struct stat info;if(fstat(fd,&info)||!S_ISDIR(info.st_mode))return;
 char descriptor[64],path[PATH_MAX];int length=snprintf(descriptor,sizeof(descriptor),"/proc/self/fd/%d",fd);if(length<=0||(size_t)length>=sizeof(descriptor))return;ssize_t count=readlink(descriptor,path,sizeof(path)-1);if(count<=0)return;path[count]=0;
 if(kind==2){if(!strstr(path,"/suitecut-output-"))return;}
 else{
  if(!strstr(path,"/suitecut-attachments-"))return;
  DIR* directory=opendir(path);if(!directory)return;struct dirent* entry;int found=0;
  while((entry=readdir(directory))){if(entry->d_name[0]=='.')continue;struct stat file;if(fstatat(fd,entry->d_name,&file,AT_SYMLINK_NOFOLLOW)||!S_ISREG(file.st_mode)||!file.st_nlink)continue;size_t base=strlen(path),name=strlen(entry->d_name);if(base+name+2>sizeof(path))continue;path[base]='/';memcpy(path+base+1,entry->d_name,name+1);info=file;found=1;break;}
  closedir(directory);if(!found)return;
 }
 int output=open(marker,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,0600);if(output<0)return;
 char record[PATH_MAX+128];length=snprintf(record,sizeof(record),"%ld %llu %llu %s\n",(long)getpid(),(unsigned long long)info.st_dev,(unsigned long long)info.st_ino,path);if(length>0&&(size_t)length<sizeof(record))(void)write(output,record,(size_t)length);close(output);
 // No writer is opened on this FIFO. This is a real interruptible native IO
 // operation; SIGKILL/waitpid, not a synthesized receipt, must retire it.
 const int waiting=open(gate,O_RDONLY|O_CLOEXEC);if(waiting>=0){char byte;(void)read(waiting,&byte,1);close(waiting);}
}
extern "C" int fsync(int fd){
 static int(*actual)(int)=NULL;if(!actual)actual=reinterpret_cast<int(*)(int)>(dlsym(RTLD_NEXT,"fsync"));if(!actual){errno=ENOSYS;return -1;}
 const int result=actual(fd);if(result==0){const int kind=worker_kind();if(kind)held(fd,kind);}return result;
}
