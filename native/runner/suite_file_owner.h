// SPDX-License-Identifier: MIT
#pragma once
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fcntl.h>
#include <stdexcept>
#include <string>
#include <system_error>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
namespace suitecut::runner {
struct SuiteFileToken {
  uint64_t id=0,device=0,inode=0;
  bool operator==(const SuiteFileToken&)const=default;
};
// One actual CLI suite file. The open descriptor retains its native identity
// through group retirement. Version checks fence observed replacement/change;
// they do not pin imported modules or make the JS import atomic with this fd.
class SuiteFileOwner {
 public:
  SuiteFileOwner(uint64_t id,const std::string& input){
    if(!id||input.empty()||input.size()>16384||input.find('\0')!=std::string::npos)
      throw std::invalid_argument("Native suite file identity/path rejected");
    path_=std::filesystem::canonical(input).native();
    if(path_.size()>16384||!UTF8(path_))throw std::invalid_argument("Native suite file path must be bounded UTF8");
    fd_=::open(path_.c_str(),O_RDONLY|O_NONBLOCK|O_CLOEXEC|O_NOFOLLOW);
    if(fd_<0)throw std::system_error(errno,std::generic_category(),"Native suite file open failed");
    try{
      if(::fstat(fd_,&version_)<0)throw std::system_error(errno,std::generic_category(),"Native suite file fstat failed");
      if(!S_ISREG(version_.st_mode))throw std::invalid_argument("Native suite file must be regular");
      token_={id,static_cast<uint64_t>(version_.st_dev),static_cast<uint64_t>(version_.st_ino)};
      Validate();
      opened_=std::chrono::steady_clock::now();
    }catch(...){::close(fd_);fd_=-1;throw;}
  }
  SuiteFileOwner(const SuiteFileOwner&)=delete;
  SuiteFileOwner& operator=(const SuiteFileOwner&)=delete;
  SuiteFileOwner(SuiteFileOwner&& other)noexcept
      :path_(std::move(other.path_)),fd_(std::exchange(other.fd_,-1)),version_(other.version_),token_(other.token_),opened_(other.opened_){}
  SuiteFileOwner& operator=(SuiteFileOwner&&)=delete;
  ~SuiteFileOwner(){if(fd_>=0)::close(fd_);}
  void Validate()const{
    if(fd_<0)throw std::runtime_error("Native suite file owner is closed");
    struct stat held{},named{};
    if(::fstat(fd_,&held)<0||::lstat(path_.c_str(),&named)<0)
      throw std::system_error(errno,std::generic_category(),"Native suite file identity observation failed");
    if(!Same(version_,held)||!Same(version_,named))
      throw std::runtime_error("Native suite file identity/version changed");
  }
  bool Open()const{return fd_>=0;}
  const SuiteFileToken& Token()const{return token_;}
  const std::string& Path()const{return path_;}
  std::string ReadBytes(size_t limit)const{
    Validate();
    if(version_.st_size<0||static_cast<uint64_t>(version_.st_size)>limit)
      throw std::length_error("Native source file exceeds bounded read capacity");
    std::string bytes(static_cast<size_t>(version_.st_size),'\0');size_t offset=0;
    while(offset<bytes.size()){
      const auto count=::pread(fd_,bytes.data()+offset,bytes.size()-offset,static_cast<off_t>(offset));
      if(count<0&&errno==EINTR)continue;
      if(count<=0)throw std::runtime_error("Native source file changed or read failed");
      offset+=static_cast<size_t>(count);
    }
    Validate();
    if(bytes.find('\0')!=std::string::npos||!UTF8(bytes))throw std::runtime_error("Native source excerpt is not bounded UTF8 text");
    return bytes;
  }
  std::chrono::steady_clock::time_point Opened()const{return opened_;}
  std::chrono::steady_clock::time_point Retire(){
    if(fd_<0)throw std::runtime_error("Native suite file owner already closed");
    const auto fd=std::exchange(fd_,-1);
    if(::close(fd)<0)throw std::system_error(errno,std::generic_category(),"Native suite file close receipt failed");
    return std::chrono::steady_clock::now();
  }
 private:
  static bool Same(const struct stat& a,const struct stat& b){
    return S_ISREG(b.st_mode)&&a.st_dev==b.st_dev&&a.st_ino==b.st_ino&&a.st_size==b.st_size&&
      a.st_mtim.tv_sec==b.st_mtim.tv_sec&&a.st_mtim.tv_nsec==b.st_mtim.tv_nsec&&
      a.st_ctim.tv_sec==b.st_ctim.tv_sec&&a.st_ctim.tv_nsec==b.st_ctim.tv_nsec;
  }
  static bool UTF8(const std::string& value){
    for(size_t i=0;i<value.size();){
      const auto first=static_cast<unsigned char>(value[i++]);
      if(first<0x80)continue;
      unsigned remaining;uint32_t code,minimum;
      if(first>=0xc2&&first<=0xdf){remaining=1;code=first&0x1f;minimum=0x80;}
      else if(first>=0xe0&&first<=0xef){remaining=2;code=first&0x0f;minimum=0x800;}
      else if(first>=0xf0&&first<=0xf4){remaining=3;code=first&7;minimum=0x10000;}
      else return false;
      if(value.size()-i<remaining)return false;
      while(remaining--){const auto next=static_cast<unsigned char>(value[i++]);if((next&0xc0)!=0x80)return false;code=(code<<6)|(next&0x3f);}
      if(code<minimum||code>0x10ffff||(code>=0xd800&&code<=0xdfff))return false;
    }
    return true;
  }
  std::string path_;int fd_=-1;struct stat version_{};SuiteFileToken token_;
  std::chrono::steady_clock::time_point opened_;
};
}
