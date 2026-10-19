// SPDX-License-Identifier: MIT
// Test-only syscall gate. It pauses a genuine native fsync and never fabricates
// its result. Native parent deadlines/cancellation still control actual children.
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dlfcn.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
static int mode(void){char data[16384];int fd=open("/proc/self/cmdline",O_RDONLY);if(fd<0)return 0;ssize_t n=read(fd,data,sizeof(data));close(fd);if(n<=0)return 0;if(memmem(data,(size_t)n,"--native-output-directory-cleanup",sizeof("--native-output-directory-cleanup")-1))return 4;if(memmem(data,(size_t)n,"--native-output-directory-worker",sizeof("--native-output-directory-worker")-1))return 3;if(memmem(data,(size_t)n,"--native-attachment-cleanup",27))return 2;if(memmem(data,(size_t)n,"--native-attachment-worker",26))return 1;return 0;}
__attribute__((constructor)) static void size_limit(void){if(mode()==4&&getenv("SUITECUT_OUTPUT_TEST_CLEANUP_DENY")&&geteuid()==0){if(setgid(65534)||setuid(65534))_exit(125);}if(mode()==1&&getenv("SUITECUT_ATTACHMENT_TEST_EFBIG")){struct rlimit value;if(getrlimit(RLIMIT_FSIZE,&value)==0){value.rlim_cur=4;setrlimit(RLIMIT_FSIZE,&value);}}}
extern "C" int fsync(int fd){
 static int(*actual)(int);if(!actual)actual=(int(*)(int))dlsym(RTLD_NEXT,"fsync");
 const int kind=mode();const bool output=kind>=3;
 const char* stage=getenv(output?"SUITECUT_OUTPUT_TEST_HOLD":"SUITECUT_ATTACHMENT_TEST_HOLD");const char* marker=getenv(output?(kind==4?"SUITECUT_OUTPUT_TEST_CLEANUP_MARKER":"SUITECUT_OUTPUT_TEST_MARKER"):"SUITECUT_ATTACHMENT_TEST_MARKER");const char* release=getenv(output?(kind==4?"SUITECUT_OUTPUT_TEST_CLEANUP_RELEASE":"SUITECUT_OUTPUT_TEST_RELEASE"):"SUITECUT_ATTACHMENT_TEST_RELEASE");struct stat info;
 if(stage&&marker&&release&&fstat(fd,&info)==0&&((kind==1&&!S_ISDIR(info.st_mode)&&!strcmp(stage,"file"))||(kind==1&&S_ISDIR(info.st_mode)&&!strcmp(stage,"directory"))||(kind==2&&S_ISDIR(info.st_mode)&&!strcmp(stage,"cleanup"))||(kind==3&&S_ISDIR(info.st_mode)&&(!strcmp(stage,"directory")||!strcmp(stage,"both")))||(kind==4&&S_ISDIR(info.st_mode)&&(!strcmp(stage,"cleanup")||!strcmp(stage,"both"))))){
  const int opened=open(marker,O_WRONLY|O_CREAT|O_EXCL,0600);
  if(opened>=0){char link[64],path[8192],text[16500];snprintf(link,sizeof(link),"/proc/self/fd/%d",fd);ssize_t n=readlink(link,path,sizeof(path));if(n<0)n=0;static const char hex[]="0123456789abcdef";char encoded[16385];for(ssize_t i=0;i<n;++i){encoded[2*i]=hex[(unsigned char)path[i]>>4];encoded[2*i+1]=hex[(unsigned char)path[i]&15];}encoded[2*n]=0;const int length=snprintf(text,sizeof(text),"%ld %llu %llu %d %s\n",(long)getpid(),(unsigned long long)info.st_dev,(unsigned long long)info.st_ino,S_ISDIR(info.st_mode)!=0,encoded);if(length>0&&(size_t)length<sizeof(text))write(opened,text,(size_t)length);close(opened);
   for(;;){const int gate=open(release,O_RDONLY);if(gate>=0){close(gate);break;}const struct timespec pause={0,1000000};nanosleep(&pause,NULL);}
   if(getenv(output?"SUITECUT_OUTPUT_TEST_EXIT_ZERO":"SUITECUT_ATTACHMENT_TEST_EXIT_ZERO"))_exit(0);
  }
 }
 return actual(fd);
}
