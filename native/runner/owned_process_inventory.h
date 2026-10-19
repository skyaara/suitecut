// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <cerrno>
#include <charconv>
#include <cstdint>
#include <dirent.h>
#include <fcntl.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
namespace suitecut::runner {
// Observe every numeric /proc entry afresh. No PID cache, skipped processes or
// inferred exits: callers still reap each actual owned child with exact waitpid.
inline std::vector<pid_t> ReadOwnedChildren(){
  struct Directory {DIR* value=nullptr;~Directory(){if(value)::closedir(value);}} directory;
  directory.value=::opendir("/proc");
  if(!directory.value)throw std::runtime_error("Native process ownership directory observation unavailable");
  const auto root=::dirfd(directory.value);const auto owner=::getpid();
  std::vector<pid_t> values;size_t inspected=0;
  const auto vanished=[&](const char* name){struct stat info{};return ::fstatat(root,name,&info,0)<0&&(errno==ENOENT||errno==ESRCH);};
  for(;;){
    errno=0;const auto* entry=::readdir(directory.value);
    if(!entry){if(errno)throw std::runtime_error("Native process ownership directory observation failed");break;}
    const std::string_view name(entry->d_name);
    if(name.empty()||name.find_first_not_of("0123456789")!=std::string_view::npos)continue;
    if(++inspected>65536)throw std::runtime_error("Native process observation capacity65536 exceeded");
    std::string relative(name);relative+="/stat";
    struct Descriptor {int value=-1;~Descriptor(){if(value>=0)::close(value);}} file;
    do{file.value=::openat(root,relative.c_str(),O_RDONLY|O_CLOEXEC);}while(file.value<0&&errno==EINTR);
    if(file.value<0){if(vanished(entry->d_name))continue;throw std::runtime_error("Native process ownership observation unavailable");}
    std::array<char,4096> buffer;size_t length=0;bool failed=false;
    for(;;){
      const auto count=::read(file.value,buffer.data()+length,buffer.size()-length);
      if(count>0){length+=static_cast<size_t>(count);if(length==buffer.size()||buffer[length-1]=='\n')break;continue;}
      if(count<0&&errno==EINTR)continue;
      failed=count<0;break;
    }
    const std::string_view record(buffer.data(),length);const auto close=record.rfind(')');
    if(failed||close==std::string_view::npos){if(vanished(entry->d_name))continue;throw std::runtime_error("Invalid native process ownership observation");}
    auto fields=record.substr(close+1);
    const auto whitespace=[](char c){return c==' '||c=='\t'||c=='\n'||c=='\r'||c=='\f'||c=='\v';};
    while(!fields.empty()&&whitespace(fields.front()))fields.remove_prefix(1);
    if(fields.empty())throw std::runtime_error("Invalid native process parent observation");
    fields.remove_prefix(1); // Kernel state, including Z, is not an exit receipt.
    while(!fields.empty()&&whitespace(fields.front()))fields.remove_prefix(1);
    int64_t parent=0;const auto parsed=std::from_chars(fields.data(),fields.data()+fields.size(),parent);
    if(parsed.ec!=std::errc{}||parsed.ptr==fields.data())throw std::runtime_error("Invalid native process parent observation");
    if(parent!=owner)continue;
    int64_t pid=0;const auto number=std::from_chars(name.data(),name.data()+name.size(),pid);
    if(number.ec!=std::errc{}||number.ptr!=name.data()+name.size()||pid<=0||pid>INT32_MAX||values.size()>=4096)throw std::runtime_error("Native child ownership observation limit exceeded");
    values.push_back(static_cast<pid_t>(pid));
  }
  return values;
}
}
