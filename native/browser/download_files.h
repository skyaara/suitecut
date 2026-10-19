// SPDX-License-Identifier: MIT
#pragma once
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace suitecut {
class DownloadDescriptor {
 public:
  explicit DownloadDescriptor(int fd=-1):fd_(fd){}
  ~DownloadDescriptor(){if(fd_>=0)close(fd_);}
  DownloadDescriptor(const DownloadDescriptor&)=delete;
  DownloadDescriptor& operator=(const DownloadDescriptor&)=delete;
  int get()const{return fd_;}
 private:int fd_;
};

// Server filenames remain metadata; CEF only receives this private opaque path.
class DownloadFiles {
 public:
  explicit DownloadFiles(std::filesystem::path parent=std::filesystem::temp_directory_path()){
    std::string pattern=(parent/"suitecut-download-XXXXXX").string();
    std::vector<char> name(pattern.begin(),pattern.end());name.push_back('\0');
    const auto root=mkdtemp(name.data());if(!root)throw std::runtime_error("Cannot create private native download directory");
    root_=root;path_=root_+"/body.download";
  }
  ~DownloadFiles(){Remove();}
  DownloadFiles(const DownloadFiles&)=delete;
  DownloadFiles& operator=(const DownloadFiles&)=delete;
  const std::string& path()const{return path_;}
  const std::string& directory()const{return root_;}
  std::uint64_t DiskBytes()const{
    std::uint64_t bytes=0;std::error_code error;
    for(const auto& entry:std::filesystem::directory_iterator(root_,error)){
      if(entry.is_regular_file(error)&&!entry.is_symlink(error)){const auto size=entry.file_size(error);if(!error)bytes+=size;}
      error.clear();
    }
    return bytes;
  }
  bool HasOpenDescriptor()const{
#if defined(__linux__)
    std::error_code error;
    for(const auto& entry:std::filesystem::directory_iterator("/proc/self/fd",error)){
      const auto target=std::filesystem::read_symlink(entry.path(),error).string();
      if(!error&&target.starts_with(root_+"/"))return true;
      error.clear();
    }
#endif
    return false;
  }
  void Adopt(const std::string& actual){
    if(actual.empty())return;
    const auto path=std::filesystem::path(actual);
    if(!path.is_absolute()||path.parent_path().lexically_normal()!=std::filesystem::path(root_))
      throw std::runtime_error("CEF download path escaped its private native directory");
    path_=path.string();
  }
  std::uint64_t Complete(){
    DownloadDescriptor file(open(path_.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW));
    struct stat metadata{};
    if(file.get()<0||fstat(file.get(),&metadata)||!S_ISREG(metadata.st_mode)||metadata.st_size<0)
      throw std::runtime_error("Native download completed without a regular owned file");
    if(fchmod(file.get(),0600))throw std::runtime_error("Cannot secure completed native download file");
    return static_cast<std::uint64_t>(metadata.st_size);
  }
  void Remove()noexcept{
    if(root_.empty())return;
    std::error_code error;std::filesystem::remove_all(root_,error);
  }
 private:std::string root_,path_;
};

struct DownloadCopyResult {bool ok=false;std::string error;std::uint64_t bytes=0;};
inline DownloadCopyResult CopyDownloadFile(const std::string& source,const std::string& destination,
    const std::atomic<bool>& canceled,double deadline,const std::function<double()>& now,std::uint64_t limit=256ull*1048576){
  std::string temporary;
  const auto cleanup=[&]{if(!temporary.empty())unlink(temporary.c_str());};
  const auto interrupted=[&]()->std::string{
    if(canceled.load())return "canceled";
    if(deadline&&now()>=deadline)return "Timeout exceeded: native download saveAs exceeded its original action deadline";
    return "";
  };
  try{
    if(destination.empty()||destination.size()>4096||destination.find('\0')!=std::string::npos||!std::filesystem::path(destination).is_absolute())
      throw std::invalid_argument("Native download saveAs requires an absolute destination path");
    if(const auto error=interrupted();!error.empty())return {false,error,0};
    std::filesystem::create_directories(std::filesystem::path(destination).parent_path());
    DownloadDescriptor input(open(source.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW));
    struct stat metadata{};
    if(input.get()<0||fstat(input.get(),&metadata)||!S_ISREG(metadata.st_mode)||metadata.st_size<0)throw std::runtime_error("Native download source is unavailable");
    const auto expected=static_cast<std::uint64_t>(metadata.st_size);
    if(expected>limit)throw std::runtime_error("Native download source exceeds its reserved copy budget");
    std::string pattern=(std::filesystem::path(destination).parent_path()/".suitecut-download-save-XXXXXX").string();
    std::vector<char> name(pattern.begin(),pattern.end());name.push_back('\0');
    DownloadDescriptor output(mkstemp(name.data()));
    if(output.get()<0)throw std::runtime_error("Cannot create native download saveAs staging file");
    temporary=name.data();if(fchmod(output.get(),0600)||fcntl(output.get(),F_SETFD,FD_CLOEXEC)<0)throw std::runtime_error("Cannot secure native download saveAs staging file");
    std::vector<char> buffer(65536);std::uint64_t written=0;
    while(true){
      if(const auto error=interrupted();!error.empty()){cleanup();return {false,error,written};}
      ssize_t size=read(input.get(),buffer.data(),buffer.size());
      if(size<0&&errno==EINTR)continue;
      if(size<0)throw std::runtime_error("Cannot read native download source");
      if(!size)break;
      if(static_cast<std::uint64_t>(size)>expected-written)throw std::runtime_error("Native download source changed while copying");
      ssize_t offset=0;
      while(offset<size){
        if(const auto error=interrupted();!error.empty()){cleanup();return {false,error,written};}
        const auto count=write(output.get(),buffer.data()+offset,static_cast<size_t>(size-offset));
        if(count<0&&errno==EINTR)continue;
        if(count<=0)throw std::runtime_error("Cannot write native download saveAs staging file");
        offset+=count;written+=static_cast<std::uint64_t>(count);
      }
    }
    if(written!=expected)throw std::runtime_error("Native download source changed while copying");
    if(fsync(output.get()))throw std::runtime_error("Cannot flush native download saveAs staging file");
    if(const auto error=interrupted();!error.empty()){cleanup();return {false,error,written};}
    if(rename(temporary.c_str(),destination.c_str()))throw std::runtime_error("Cannot commit native download saveAs destination");
    temporary.clear();return {true,"",written};
  }catch(const std::exception& error){cleanup();return {false,error.what(),0};}
}
} // namespace suitecut
