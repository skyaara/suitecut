// SPDX-License-Identifier: MIT
#pragma once
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string>

// The lock belongs to the native process and remains held through CefShutdown.
// A persistent directory is never removed as part of the lock's lifecycle.
class SuiteCutProfileLock {
 public:
  SuiteCutProfileLock()=default;
  SuiteCutProfileLock(const SuiteCutProfileLock&)=delete;
  SuiteCutProfileLock& operator=(const SuiteCutProfileLock&)=delete;
  ~SuiteCutProfileLock(){if(fd_>=0)close(fd_);}
  bool Acquire(const std::string& directory,std::string& error){
    if(fd_>=0){error="Native persistent profile lock is already held";return false;}
    int file=open((directory+"/.suitecut-profile.lock").c_str(),O_CREAT|O_RDWR|O_CLOEXEC|O_NOFOLLOW,0600);
    if(file<0){error="Could not open native persistent profile lock";return false;}
    struct stat info{};
    if(fstat(file,&info)!=0||!S_ISREG(info.st_mode)){close(file);error="Native persistent profile lock must be a regular file";return false;}
    if(flock(file,LOCK_EX|LOCK_NB)!=0){close(file);error="Native persistent profile is already in use";return false;}
    fd_=file;return true;
  }
 private:
  int fd_=-1;
};
