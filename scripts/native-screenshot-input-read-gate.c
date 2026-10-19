// SPDX-License-Identifier: MIT
// Authored Linux proof interposer: hold a real readonly read call at its entry,
// then invoke the actual libc operation unchanged. No fabricated bytes/ACKs.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
static int (*next_open)(const char*,int,...),(*next_open64)(const char*,int,...),(*next_close)(int);
static ssize_t(*next_read)(int,void*,size_t);
static pthread_once_t initialized=PTHREAD_ONCE_INIT;
static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
struct Slot{int fd;unsigned serial;bool entered;};static struct Slot slots[8];static unsigned serial=0;
static void initialize(void){
 next_open=dlsym(RTLD_NEXT,"open");next_open64=dlsym(RTLD_NEXT,"open64");next_read=dlsym(RTLD_NEXT,"read");next_close=dlsym(RTLD_NEXT,"close");
 if(!next_open||!next_open64||!next_read||!next_close)_exit(94);
 for(size_t i=0;i<8;++i)slots[i].fd=-1;
}
static bool target(const char*path){
 const char*baseline=getenv("SUITECUT_SCREENSHOT_GATE_PATH"),*executable=getenv("SUITECUT_SCREENSHOT_GATE_EXECUTABLE");if(!baseline||!executable||strcmp(path,baseline))return false;
 char actual[4096];const long length=syscall(SYS_readlink,"/proc/self/exe",actual,sizeof(actual)-1);if(length<0)return false;actual[length]=0;return !strcmp(actual,executable);
}
static void mark(const char*kind,unsigned id,int fd,int result,int error){
 const char*directory=getenv("SUITECUT_SCREENSHOT_GATE_DIRECTORY");if(!directory)_exit(95);char path[4352];const int length=snprintf(path,sizeof(path),"%s/%s-%u.json",directory,kind,id);if(length<0||(size_t)length>=sizeof(path))_exit(95);
 struct stat info={0};const int stat_result=fstat(fd,&info);const int flags=fcntl(fd,F_GETFL),descriptor_flags=fcntl(fd,F_GETFD);
 char text[512];const int count=snprintf(text,sizeof(text),"{\"pid\":%ld,\"tid\":%ld,\"serial\":%u,\"fd\":%d,\"result\":%d,\"error\":%d,\"statResult\":%d,\"device\":%llu,\"inode\":%llu,\"size\":%lld,\"flags\":%d,\"descriptorFlags\":%d}\n",(long)getpid(),(long)syscall(SYS_gettid),id,fd,result,error,stat_result,(unsigned long long)info.st_dev,(unsigned long long)info.st_ino,(long long)info.st_size,flags,descriptor_flags);
 const int output=(int)syscall(SYS_openat,AT_FDCWD,path,O_CREAT|O_EXCL|O_WRONLY|O_CLOEXEC,0600);if(output<0||count<0||(size_t)count>=sizeof(text))_exit(95);if(syscall(SYS_write,output,text,(size_t)count)!=count)_exit(95);if(syscall(SYS_close,output))_exit(95);
}
static void track(const char*path,int flags,int fd){
 if(fd<0||(flags&O_ACCMODE)!=O_RDONLY||!target(path)){return;}
 pthread_mutex_lock(&mutex);bool admitted=false;for(size_t i=0;i<8;++i)if(slots[i].fd<0){slots[i]=(struct Slot){fd,++serial,false};admitted=true;break;}pthread_mutex_unlock(&mutex);if(!admitted)_exit(96);
}
static int call_open(const char*path,int flags,mode_t mode,bool large){pthread_once(&initialized,initialize);const int fd=large?next_open64(path,flags,mode):next_open(path,flags,mode);const int error=errno;track(path,flags,fd);errno=error;return fd;}
int open(const char*path,int flags,...){mode_t mode=0;if((flags&O_CREAT)||((flags&O_TMPFILE)==O_TMPFILE)){va_list args;va_start(args,flags);mode=(mode_t)va_arg(args,int);va_end(args);}return call_open(path,flags,mode,false);}
int open64(const char*path,int flags,...){mode_t mode=0;if((flags&O_CREAT)||((flags&O_TMPFILE)==O_TMPFILE)){va_list args;va_start(args,flags);mode=(mode_t)va_arg(args,int);va_end(args);}return call_open(path,flags,mode,true);}
ssize_t read(int fd,void*bytes,size_t length){
 pthread_once(&initialized,initialize);unsigned id=0;pthread_mutex_lock(&mutex);for(size_t i=0;i<8;++i)if(slots[i].fd==fd&&!slots[i].entered){slots[i].entered=true;id=slots[i].serial;break;}pthread_mutex_unlock(&mutex);
 if(id){mark("entered",id,fd,0,0);const char*directory=getenv("SUITECUT_SCREENSHOT_GATE_DIRECTORY");char release[4352];const int size=snprintf(release,sizeof(release),"%s/release",directory);if(size<0||(size_t)size>=sizeof(release))_exit(95);struct timespec start,now,pause={0,1000000};clock_gettime(CLOCK_MONOTONIC,&start);struct stat info;
  while(stat(release,&info)){clock_gettime(CLOCK_MONOTONIC,&now);if(now.tv_sec-start.tv_sec>15)_exit(97);nanosleep(&pause,NULL);}
 }
 return next_read(fd,bytes,length);
}
int close(int fd){
 pthread_once(&initialized,initialize);unsigned id=0;pthread_mutex_lock(&mutex);for(size_t i=0;i<8;++i)if(slots[i].fd==fd){id=slots[i].serial;slots[i].fd=-1;break;}pthread_mutex_unlock(&mutex);
 // Record only after the actual libc close receipt. Marker fstat/flags
 // consequently observe the genuinely closed descriptor before marker I/O.
 const int result=next_close(fd),error=errno;if(id)mark("closed",id,fd,result,result<0?error:0);errno=error;return result;
}
