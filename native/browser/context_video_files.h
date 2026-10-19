// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
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
#include <utility>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
namespace suitecut {
inline void ContextVideoActive(const std::atomic<bool>&canceled,double deadline,const std::function<double()>&now){
  if(canceled.load())throw std::runtime_error("Native video file operation canceled");
  if(deadline&&now()>=deadline)throw std::runtime_error("Native video original file operation deadline expired");
}
class ContextVideoDirectory{
 public:
  static std::shared_ptr<ContextVideoDirectory>Create(const std::string&directory,const std::atomic<bool>&canceled,double deadline,const std::function<double()>&now){
    ContextVideoActive(canceled,deadline,now);
    if(directory.empty()||directory.size()>4096||directory.find('\0')!=std::string::npos||!std::filesystem::path(directory).is_absolute())throw std::runtime_error("Native recordVideo.dir must be an absolute path within 4096 bytes");
    std::filesystem::create_directories(directory);const auto parent=std::filesystem::canonical(directory);
    std::string pattern=(parent/".suitecut-video-XXXXXX").string();if(pattern.size()>4000)throw std::runtime_error("Native video directory leaves no bounded artifact filename");
    std::vector<char>bytes(pattern.begin(),pattern.end());bytes.push_back('\0');ContextVideoActive(canceled,deadline,now);
    const auto created=mkdtemp(bytes.data());if(!created)throw std::runtime_error("Cannot create private native video directory");
    const int fd=open(created,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if(fd<0){rmdir(created);throw std::runtime_error("Cannot retain native video directory");}
    auto result=std::shared_ptr<ContextVideoDirectory>(new ContextVideoDirectory(fd,created));ContextVideoActive(canceled,deadline,now);return result;
  }
  ~ContextVideoDirectory(){
    struct stat held{},current{};
    const bool owned=!fstat(fd_,&held)&&!lstat(path_.c_str(),&current)&&S_ISDIR(current.st_mode)&&current.st_dev==held.st_dev&&current.st_ino==held.st_ino;
    close(fd_);
    if(owned)rmdir(path_.c_str()); // Empty-only, identity-checked cleanup.
  }
  const std::string&Path()const{return path_;}
  int FD()const{return fd_;}
  void Check()const{
    struct stat current{},held{};
    if(fstat(fd_,&held)||lstat(path_.c_str(),&current)||!S_ISDIR(current.st_mode)||current.st_dev!=held.st_dev||current.st_ino!=held.st_ino)throw std::runtime_error("Native video directory identity changed");
  }
 private:ContextVideoDirectory(int fd,std::string path):fd_(fd),path_(std::move(path)){}int fd_;std::string path_;
};
class ContextVideoFile{
 public:
  static constexpr uint64_t kMaxBytes=512ull*1024*1024;
  static std::shared_ptr<ContextVideoFile>Seal(std::shared_ptr<ContextVideoDirectory>directory,const std::string&name,uint64_t expected){
    directory->Check();if(name.empty()||name.find('/')!=std::string::npos||name=="."||name==".."||!expected||expected>kMaxBytes)throw std::runtime_error("Invalid completed native video file metadata");
    const int fd=openat(directory->FD(),name.c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)throw std::runtime_error("Completed native video file is unavailable");
    struct stat metadata{};if(fstat(fd,&metadata)||!S_ISREG(metadata.st_mode)||metadata.st_size<0||static_cast<uint64_t>(metadata.st_size)!=expected){close(fd);throw std::runtime_error("Completed native video file does not match actual encoder bytes");}
    return std::shared_ptr<ContextVideoFile>(new ContextVideoFile(std::move(directory),name,fd,metadata));
  }
  ~ContextVideoFile(){close(fd_);}
  std::string Path()const{CheckPath();return (std::filesystem::path(directory_->Path())/name_).string();}
  void Copy(const std::string&destination,const std::atomic<bool>&canceled,double deadline,const std::function<double()>&now)const{
    ContextVideoActive(canceled,deadline,now);CheckHeld();
    if(destination.empty()||destination.size()>4096||destination.find('\0')!=std::string::npos||!std::filesystem::path(destination).is_absolute()||std::filesystem::path(destination).filename().empty()||std::filesystem::path(destination).filename()=="."||std::filesystem::path(destination).filename()=="..")throw std::runtime_error("Native video saveAs requires a bounded absolute file path");
    const auto target=std::filesystem::path(destination);std::filesystem::create_directories(target.parent_path());const auto parent=std::filesystem::canonical(target.parent_path());
    const int parent_fd=open(parent.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(parent_fd<0)throw std::runtime_error("Cannot retain native video saveAs destination directory");
    int output=-1;std::string temporary;bool committed=false;
    try{
      struct stat existing{};if(fstatat(parent_fd,target.filename().c_str(),&existing,AT_SYMLINK_NOFOLLOW)==0&&existing.st_dev==metadata_.st_dev&&existing.st_ino==metadata_.st_ino){ContextVideoActive(canceled,deadline,now);close(parent_fd);return;}
      static std::atomic<uint64_t>sequence{0};
      for(unsigned attempt=0;attempt<32&&output<0;++attempt){temporary=".suitecut-video-save-"+std::to_string(getpid())+"-"+std::to_string(++sequence);output=openat(parent_fd,temporary.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(output<0&&errno!=EEXIST)break;}
      if(output<0)throw std::runtime_error("Cannot create native video saveAs staging file");
      std::vector<char>buffer(65536);uint64_t position=0;
      while(position<static_cast<uint64_t>(metadata_.st_size)){
        ContextVideoActive(canceled,deadline,now);const auto count=pread(fd_,buffer.data(),std::min<uint64_t>(buffer.size(),static_cast<uint64_t>(metadata_.st_size)-position),static_cast<off_t>(position));
        if(count<0&&errno==EINTR)continue;
        if(count<=0)throw std::runtime_error("Cannot read complete native video source");
        ssize_t offset=0;while(offset<count){ContextVideoActive(canceled,deadline,now);const auto wrote=write(output,buffer.data()+offset,static_cast<size_t>(count-offset));if(wrote<0&&errno==EINTR)continue;
          if(wrote<=0)throw std::runtime_error("Cannot write native video saveAs staging file");
          offset+=wrote;}position+=static_cast<uint64_t>(count);
      }
      CheckHeld();if(fsync(output))throw std::runtime_error("Cannot flush native video saveAs file");ContextVideoActive(canceled,deadline,now);
      if(renameat(parent_fd,temporary.c_str(),parent_fd,target.filename().c_str()))throw std::runtime_error("Cannot publish native video saveAs file");
      committed=true;
      if(fsync(parent_fd))throw std::runtime_error("Native video saveAs committed but directory flush failed");
      ContextVideoActive(canceled,deadline,now);
      close(output);close(parent_fd);
    }catch(...){if(output>=0)close(output);if(!committed&&!temporary.empty())unlinkat(parent_fd,temporary.c_str(),0);close(parent_fd);throw;}
  }
  void Delete(const std::atomic<bool>&canceled,double deadline,const std::function<double()>&now,bool&removed)const{
    ContextVideoActive(canceled,deadline,now);CheckPath();
    if(unlinkat(directory_->FD(),name_.c_str(),0))throw std::runtime_error("Cannot delete native video artifact");
    removed=true; // Actual unlink is retained even if later durability/cancel checks fail.
    if(fsync(directory_->FD()))throw std::runtime_error("Native video artifact deleted but directory flush failed");
  }
 private:
  ContextVideoFile(std::shared_ptr<ContextVideoDirectory>directory,std::string name,int fd,struct stat metadata):directory_(std::move(directory)),name_(std::move(name)),fd_(fd),metadata_(metadata){}
  static bool SameModified(const struct stat&a,const struct stat&b){
#if defined(__APPLE__)
    return a.st_mtimespec.tv_sec==b.st_mtimespec.tv_sec&&a.st_mtimespec.tv_nsec==b.st_mtimespec.tv_nsec;
#else
    return a.st_mtim.tv_sec==b.st_mtim.tv_sec&&a.st_mtim.tv_nsec==b.st_mtim.tv_nsec;
#endif
  }
  void CheckHeld()const{
    struct stat actual{};if(fstat(fd_,&actual)||!S_ISREG(actual.st_mode)||actual.st_dev!=metadata_.st_dev||actual.st_ino!=metadata_.st_ino||actual.st_size!=metadata_.st_size||!SameModified(actual,metadata_))throw std::runtime_error("Native video artifact bytes changed after encoder finalization");
  }
  void CheckPath()const{
    directory_->Check();CheckHeld();struct stat actual{};if(fstatat(directory_->FD(),name_.c_str(),&actual,AT_SYMLINK_NOFOLLOW)||!S_ISREG(actual.st_mode)||actual.st_dev!=metadata_.st_dev||actual.st_ino!=metadata_.st_ino)throw std::runtime_error("Native video artifact path no longer identifies its owned file");
  }
  std::shared_ptr<ContextVideoDirectory>directory_;std::string name_;int fd_;struct stat metadata_;
};
}
