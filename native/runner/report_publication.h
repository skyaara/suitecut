// SPDX-License-Identifier: MIT
#pragma once
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <stdexcept>
#include <system_error>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
namespace suitecut::runner {
namespace report_detail {class AtomicFile;}
// Owns the descriptor of the actual committed report inode. This is a native
// publication handle, never an execution, attachment or author callback receipt.
class NativeReportPublication {
 public:
  NativeReportPublication()=default;
  NativeReportPublication(const NativeReportPublication&)=delete;
  NativeReportPublication& operator=(const NativeReportPublication&)=delete;
  NativeReportPublication(NativeReportPublication&& other)noexcept
      :fd_(std::exchange(other.fd_,-1)),bytes_(other.bytes_),device_(other.device_),inode_(other.inode_){}
  NativeReportPublication& operator=(NativeReportPublication&& other)noexcept{
    if(this!=&other){Close();fd_=std::exchange(other.fd_,-1);bytes_=other.bytes_;device_=other.device_;inode_=other.inode_;}return *this;
  }
  ~NativeReportPublication(){Close();}
  bool Ready()const noexcept{return fd_>=0;}
  size_t Bytes()const noexcept{return bytes_;}
  int Descriptor()const noexcept{return fd_;}
  void Validate()const{
    struct stat observed{};
    if(fd_<0)throw std::runtime_error("Native reporter lacks its committed report descriptor");
    if(::fstat(fd_,&observed)<0)throw std::system_error(errno,std::generic_category(),"Native reporter committed inode stat");
    if(!S_ISREG(observed.st_mode)||observed.st_dev!=device_||observed.st_ino!=inode_||observed.st_size<0||static_cast<uint64_t>(observed.st_size)!=bytes_)
      throw std::runtime_error("Native reporter committed report identity or length changed");
  }
 private:
  friend class report_detail::AtomicFile;
  static NativeReportPublication Duplicate(int original,size_t bytes){
    NativeReportPublication result;result.fd_=::fcntl(original,F_DUPFD_CLOEXEC,3);
    if(result.fd_<0)throw std::system_error(errno,std::generic_category(),"Native reporter committed descriptor retention");
    struct stat identity{};
    if(::fstat(result.fd_,&identity)<0)throw std::system_error(errno,std::generic_category(),"Native reporter publication inode stat");
    result.bytes_=bytes;result.device_=identity.st_dev;result.inode_=identity.st_ino;result.Validate();return result;
  }
  void Close()noexcept{if(fd_>=0){::close(fd_);fd_=-1;}}
  int fd_=-1;size_t bytes_=0;dev_t device_=0;ino_t inode_=0;
};
} // namespace suitecut::runner
