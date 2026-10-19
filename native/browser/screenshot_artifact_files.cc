// SPDX-License-Identifier: MIT
#include "screenshot_artifact_files.h"
#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <utility>
#include <fcntl.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
namespace suitecut {
namespace {
constexpr size_t kPNGBytes=2*1024*1024;
struct Failure:std::runtime_error{int number;Failure(const std::string&step,int code):std::runtime_error("Native screenshot artifact "+step),number(code){}};
struct FD {
  int value=-1;explicit FD(int fd=-1):value(fd){}
  FD(const FD&)=delete;FD&operator=(const FD&)=delete;
  ~FD(){if(value>=0)::close(value);}
  void Close(const char*step){if(value<0)return;const int fd=std::exchange(value,-1);if(::close(fd))throw Failure(step,errno);}
};
void System(const char*step){throw Failure(step,errno);}
bool Same(const struct stat&a,const struct stat&b){return a.st_dev==b.st_dev&&a.st_ino==b.st_ino;}
void PNG(const std::vector<uint8_t>&bytes){constexpr std::array<uint8_t,8>magic{137,80,78,71,13,10,26,10};if(bytes.size()<33||bytes.size()>kPNGBytes||!std::equal(magic.begin(),magic.end(),bytes.begin()))throw Failure("requires an admitted PNG of at most2MiB",0);}
std::string Nonce(){std::array<unsigned char,12>bytes;size_t offset=0;while(offset<bytes.size()){const auto count=::getrandom(bytes.data()+offset,bytes.size()-offset,0);if(count>0)offset+=size_t(count);else if(count<0&&errno==EINTR)continue;else System("unique directory entropy failed");}static constexpr char hex[]="0123456789abcdef";std::string value;value.reserve(bytes.size()*2);for(const auto byte:bytes){value+=hex[byte>>4];value+=hex[byte&15];}return value;}
struct File {const char*name;struct stat identity{};bool created=false;};
}
ScreenshotArtifactPublication WriteScreenshotArtifacts(const std::string&root,const std::vector<uint8_t>&actual,const std::vector<uint8_t>&diff,double deadline,const std::atomic_bool&canceled,const std::function<double()>&now){
  ScreenshotArtifactPublication result;FD parent,own;struct stat parent_identity{},own_identity{};std::string root_path,staging,final,current;bool directory_created=false;
  std::array<File,2>files{{{"actual.png",{},false},{"diff.png",{},false}}};
  const auto check=[&]{if(canceled.load(std::memory_order_acquire))throw Failure("finalization canceled",0);if(!now||!std::isfinite(deadline)||deadline<=0||now()>=deadline)throw Failure("finalization original cleanup deadline exceeded",0);};
  const auto root_current=[&]{struct stat state{};if(::stat(root_path.c_str(),&state)||!S_ISDIR(state.st_mode)||!Same(state,parent_identity))throw Failure("output directory identity changed",errno);};
  const auto cleanup=[&]{
    if(!directory_created)return;
    bool clean=true;
    // A checked close can itself cross cancellation/deadline. Reopen only the
    // exact pinned parent/directory for rollback; preserve replacements.
    if(parent.value<0){parent.value=::open(root_path.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);struct stat state{};if(parent.value<0||::fstat(parent.value,&state)||!Same(state,parent_identity)){result.cleanup_failed=true;result.cleanup_error="Native screenshot artifact rollback parent identity unavailable";return;}}
    if(own.value<0){own.value=::openat(parent.value,current.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);struct stat state{};if(own.value<0||::fstat(own.value,&state)||!Same(state,own_identity)){result.cleanup_failed=true;result.cleanup_error="Native screenshot artifact rollback directory identity unavailable";return;}}
    // Use the actually opened directory, never a replacement path. Refuse to
    // unlink any entry whose real inode differs from the created file.
    for(const auto&file:files)if(file.created){struct stat state{};if(::fstatat(own.value,file.name,&state,AT_SYMLINK_NOFOLLOW)==0){if(!Same(state,file.identity)||!S_ISREG(state.st_mode)||::unlinkat(own.value,file.name,0))clean=false;}else if(errno!=ENOENT)clean=false;}
    if(own.value>=0&&::fsync(own.value))clean=false;
    struct stat state{};
    if(parent.value<0||::fstatat(parent.value,current.c_str(),&state,AT_SYMLINK_NOFOLLOW)||!Same(state,own_identity)||!S_ISDIR(state.st_mode)||::unlinkat(parent.value,current.c_str(),AT_REMOVEDIR))clean=false;
    if(parent.value>=0&&::fsync(parent.value))clean=false;
    result.rollback=clean;result.cleanup_failed=!clean;if(!clean)result.cleanup_error="Native screenshot artifact rollback did not remove its exact owned directory/files";
  };
  try{
    check();if(root.empty()||root.size()>32768||root.find('\0')!=std::string::npos||!std::filesystem::path(root).is_absolute())throw Failure("output path must be a bounded absolute path",0);
    PNG(actual);if(!diff.empty())PNG(diff);
    std::error_code error;std::filesystem::create_directories(root,error);if(error)throw Failure("output parent creation failed",error.value());check();
    root_path=std::filesystem::canonical(root,error).string();if(error)throw Failure("output parent resolution failed",error.value());
    parent.value=::open(root_path.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);if(parent.value<0)System("output parent open failed");if(::fstat(parent.value,&parent_identity))System("output parent stat failed");root_current();
    bool made=false;for(unsigned attempt=0;attempt<16&&!made;++attempt){const auto nonce=Nonce();staging=".screenshot-comparison-"+nonce;final="screenshot-comparison-"+nonce;if(::mkdirat(parent.value,staging.c_str(),0700)==0)made=true;else if(errno!=EEXIST)System("staging directory creation failed");}if(!made)throw Failure("unique directory capacity exceeded",EEXIST);
    current=staging;directory_created=true;own.value=::openat(parent.value,staging.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);if(own.value<0)System("staging directory open failed");if(::fstat(own.value,&own_identity))System("staging directory stat failed");result.directory_device=uint64_t(own_identity.st_dev);result.directory_inode=uint64_t(own_identity.st_ino);
    const auto write=[&](File&file,const std::vector<uint8_t>&bytes){
      check();FD output(::openat(own.value,file.name,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600));if(output.value<0)System("PNG create failed");if(::fstat(output.value,&file.identity))System("PNG initial stat failed");file.created=true;
      size_t offset=0;while(offset<bytes.size()){check();const auto count=::write(output.value,bytes.data()+offset,std::min<size_t>(65536,bytes.size()-offset));if(count>0)offset+=size_t(count);else if(count<0&&errno==EINTR)continue;else System("PNG write failed");}
      check();if(::fsync(output.value))System("PNG fsync failed");check();struct stat state{};if(::fstat(output.value,&state)||!Same(state,file.identity)||state.st_size<0||uint64_t(state.st_size)!=bytes.size())throw Failure("PNG physical output identity/size changed",errno);output.Close("PNG close failed");
    };
    write(files[0],actual);if(!diff.empty())write(files[1],diff);check();if(::fsync(own.value))System("staging directory fsync failed");check();root_current();struct stat staged{};if(::fstatat(parent.value,staging.c_str(),&staged,AT_SYMLINK_NOFOLLOW)||!S_ISDIR(staged.st_mode)||!Same(staged,own_identity))throw Failure("staging directory identity changed",errno);
#if defined(__linux__) && defined(SYS_renameat2)
    if(::syscall(SYS_renameat2,parent.value,staging.c_str(),parent.value,final.c_str(),1U))System("atomic directory publication failed");
#else
    throw Failure("atomic directory publication unsupported on this native platform",0);
#endif
    current=final;if(::fsync(parent.value))System("output parent fsync failed");check();root_current();
    struct stat state{};if(::fstatat(parent.value,final.c_str(),&state,AT_SYMLINK_NOFOLLOW)||!Same(state,own_identity)||!S_ISDIR(state.st_mode))throw Failure("published directory identity changed",errno);
    // Publish no paths until every genuine write/fsync/close has completed.
    const auto actual_path=(std::filesystem::path(root_path)/final/files[0].name).string();const auto diff_path=diff.empty()?std::string{}:(std::filesystem::path(root_path)/final/files[1].name).string();
    own.Close("output directory close failed");parent.Close("output parent close failed");check();
    result.actual_path=actual_path;result.diff_path=diff_path;result.committed=true;return result;
  }catch(const Failure&error){result.error=error.what();result.error_number=error.number;}
   catch(const std::exception&error){result.error="Native screenshot artifact file operation failed: "+std::string(error.what());}
   catch(...){result.error="Native screenshot artifact file operation failed";}
  cleanup();return result;
}
}
