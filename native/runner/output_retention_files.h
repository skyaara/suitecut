// SPDX-License-Identifier: MIT
#pragma once
#include "attachment_files.h"
#include <dirent.h>
#include <memory>
#include <sys/stat.h>
namespace suitecut::runner::output_retention_files {
namespace af=attachment_files;
// This process acts only on a native-created, inode-pinned attempt root. It
// invents no file publication, callback completion or native owner retirement.
inline constexpr size_t kNodes=16384,kDepth=64,kNames=4*1024*1024;
struct Node {std::string name;uint64_t device=0,inode=0,mount=0;mode_t mode=0;size_t end=0;};
struct Counters {uint64_t files=0,directories=0,symlinks=0;};
struct Root {
 uint64_t id=0,device=0,inode=0,parent_device=0,parent_inode=0,mount=0;
 std::string parent,name;double deadline=0;
};
inline uint64_t MountAt(int directory,const char* name,int flags){
 struct statx result{};
 if(::statx(directory,name,flags|AT_NO_AUTOMOUNT,STATX_MNT_ID,&result))af::System("output-retention-mount");
 if(!(result.stx_mask&STATX_MNT_ID))throw af::Error("output-retention-mount",0,"Native filesystem did not provide mount identity");
 return result.stx_mnt_id;
}
inline bool Identity(const struct stat& actual,uint64_t device,uint64_t inode){return static_cast<uint64_t>(actual.st_dev)==device&&static_cast<uint64_t>(actual.st_ino)==inode;}
inline void ExactDirectory(int fd,uint64_t device,uint64_t inode,const char* stage){struct stat actual{};if(::fstat(fd,&actual))af::System(stage);if(!Identity(actual,device,inode)||!S_ISDIR(actual.st_mode))throw af::Error(stage,0,"Native output retention directory identity changed");}
inline void RootCurrent(const Root& root,int parent){
 af::Live(root.deadline);ExactDirectory(parent,root.parent_device,root.parent_inode,"output-retention-parent-identity");
 af::FD current(::open(root.parent.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC));if(current.value<0)af::System("output-retention-current-parent-open");
 ExactDirectory(current.value,root.parent_device,root.parent_inode,"output-retention-current-parent-identity");
 struct stat actual{};if(::fstatat(parent,root.name.c_str(),&actual,AT_SYMLINK_NOFOLLOW))af::System("output-retention-root-stat");
 if(!Identity(actual,root.device,root.inode)||!S_ISDIR(actual.st_mode))throw af::Error("output-retention-root-identity",0,"Native output root was replaced; foreign entry preserved");
}
inline size_t Inventory(int directory,const Root& root,std::vector<Node>& nodes,size_t index,size_t depth,size_t& names){
 if(depth>kDepth)throw af::Error("output-retention-depth",0,"Native output tree depth exceeds bound");
 af::Live(root.deadline);const int copy=::fcntl(directory,F_DUPFD_CLOEXEC,16);if(copy<0)af::System("output-retention-enumeration-copy");
 struct Enumeration {DIR* handle=nullptr;~Enumeration(){if(handle)::closedir(handle);}} entries{::fdopendir(copy)};if(!entries.handle){const int saved=errno;::close(copy);errno=saved;af::System("output-retention-enumeration-open");}
 for(;;){
  af::Live(root.deadline);errno=0;const auto* entry=::readdir(entries.handle);if(!entry){if(errno)af::System("output-retention-enumeration-read");break;}
  const std::string_view name(entry->d_name);if(name=="."||name=="..")continue;
  if(nodes.size()>=kNodes||name.empty()||name.size()>255||name.size()+1>kNames-names)throw af::Error("output-retention-capacity",0,"Native output tree inventory exceeds bound");
  struct stat actual{};if(::fstatat(directory,entry->d_name,&actual,AT_SYMLINK_NOFOLLOW))af::System("output-retention-child-stat");
  const auto mount=MountAt(directory,entry->d_name,AT_SYMLINK_NOFOLLOW);
  if(static_cast<uint64_t>(actual.st_dev)!=root.device||mount!=root.mount)throw af::Error("output-retention-mount",0,"Native output retention refuses another mount");
  const auto child=nodes.size();nodes.push_back({std::string(name),static_cast<uint64_t>(actual.st_dev),static_cast<uint64_t>(actual.st_ino),mount,actual.st_mode,0});names+=nodes.back().name.capacity()+1;
  if(names>kNames)throw af::Error("output-retention-capacity",0,"Native output tree retained name capacity exceeds bound");
  if(S_ISDIR(actual.st_mode)){
   af::FD nested(::openat(directory,entry->d_name,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC));if(nested.value<0)af::System("output-retention-child-open");
   ExactDirectory(nested.value,nodes[child].device,nodes[child].inode,"output-retention-child-identity");
   Inventory(nested.value,root,nodes,child,depth+1,names);
  }else nodes[child].end=child+1;
 }
 nodes[index].end=nodes.size();return nodes[index].end;
}
inline void RemoveChildren(int directory,int parent,const Root& root,const std::vector<Node>& nodes,size_t index,Counters& removed){
 for(size_t child=index+1;child<nodes[index].end;child=nodes[child].end){
  af::Live(root.deadline);RootCurrent(root,parent);const auto& pin=nodes[child];struct stat actual{};
  if(::fstatat(directory,pin.name.c_str(),&actual,AT_SYMLINK_NOFOLLOW))af::System("output-retention-current-child-stat");
  if(!Identity(actual,pin.device,pin.inode)||(actual.st_mode&S_IFMT)!=(pin.mode&S_IFMT)||MountAt(directory,pin.name.c_str(),AT_SYMLINK_NOFOLLOW)!=pin.mount)
   throw af::Error("output-retention-child-identity",0,"Native output child changed; replacement preserved");
  if(S_ISDIR(pin.mode)){
   af::FD nested(::openat(directory,pin.name.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC));if(nested.value<0)af::System("output-retention-current-child-open");
   ExactDirectory(nested.value,pin.device,pin.inode,"output-retention-current-child-identity");
   RemoveChildren(nested.value,parent,root,nodes,child,removed);
   af::Live(root.deadline);if(::fsync(nested.value))af::System("output-retention-child-fsync");
   // Recheck the actual name after recursive work; never remove a known
   // replacement. External filesystem mutation is not serialized by this API.
   if(::fstatat(directory,pin.name.c_str(),&actual,AT_SYMLINK_NOFOLLOW))af::System("output-retention-final-child-stat");
   if(!Identity(actual,pin.device,pin.inode)||!S_ISDIR(actual.st_mode))throw af::Error("output-retention-child-identity",0,"Native output directory child changed; replacement preserved");
   if(::unlinkat(directory,pin.name.c_str(),AT_REMOVEDIR))af::System("output-retention-child-rmdir");
   ++removed.directories;
  }else{
   if(::unlinkat(directory,pin.name.c_str(),0))af::System("output-retention-child-unlink");
   if(S_ISLNK(pin.mode))++removed.symlinks;else ++removed.files;
  }
 }
 af::Live(root.deadline);if(::fsync(directory))af::System("output-retention-directory-fsync");
}
inline int Worker(int control=3){uint64_t id=0;try{
 af::Input input(control);const auto fields=input.Next(af::Now()+30000);
 if(fields.size()!=9||fields[0]!="REMOVE")throw af::Error("protocol",0,"Invalid native output retention packet");
 Root root;root.id=af::UInt(fields[1]);id=root.id;root.parent=af::Unhex(fields[2],8192);root.name=af::Unhex(fields[3],256);root.device=af::UInt(fields[4]);root.inode=af::UInt(fields[5]);root.parent_device=af::UInt(fields[6]);root.parent_inode=af::UInt(fields[7]);root.deadline=af::Number(fields[8]);
 if(!id||!root.inode||!root.parent_inode||root.parent.empty()||root.parent.front()!='/'||root.parent.find('\0')!=std::string::npos||!root.name.starts_with("suitecut-output-")||root.name.find('/')!=std::string::npos||root.name.find('\0')!=std::string::npos)
   throw af::Error("protocol",0,"Invalid native output retention ownership pin");
 af::Live(root.deadline);af::FD parent(::open(root.parent.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC));
 if(parent.value<0){if(errno==ENOENT){af::Send(control,"RETIRED "+std::to_string(id)+" alreadyAbsent 0 0 0");return 0;}af::System("output-retention-parent-open");}
 ExactDirectory(parent.value,root.parent_device,root.parent_inode,"output-retention-parent-identity");struct stat actual{};
 if(::fstatat(parent.value,root.name.c_str(),&actual,AT_SYMLINK_NOFOLLOW)){
  if(errno!=ENOENT)af::System("output-retention-root-stat");
  af::Send(control,"RETIRED "+std::to_string(id)+" alreadyAbsent 0 0 0");return 0;
 }
 if(!Identity(actual,root.device,root.inode)||!S_ISDIR(actual.st_mode)){af::Send(control,"RETIRED "+std::to_string(id)+" foreignPreserved 0 0 0");return 0;}
 af::FD directory(::openat(parent.value,root.name.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC));if(directory.value<0)af::System("output-retention-root-open");
 ExactDirectory(directory.value,root.device,root.inode,"output-retention-root-identity");root.mount=MountAt(directory.value,"",AT_EMPTY_PATH|AT_SYMLINK_NOFOLLOW);
 std::vector<Node> nodes;nodes.reserve(kNodes);nodes.push_back({root.name,root.device,root.inode,root.mount,actual.st_mode,0});size_t names=nodes.front().name.capacity()+1;
 Inventory(directory.value,root,nodes,0,0,names);RootCurrent(root,parent.value);
 Counters removed;RemoveChildren(directory.value,parent.value,root,nodes,0,removed);RootCurrent(root,parent.value);
 af::Live(root.deadline);if(::unlinkat(parent.value,root.name.c_str(),AT_REMOVEDIR))af::System("output-retention-root-rmdir");
 ++removed.directories;
 af::Live(root.deadline);if(::fsync(parent.value))af::System("output-retention-parent-fsync");af::Live(root.deadline);
 af::Send(control,"RETIRED "+std::to_string(id)+" removed "+std::to_string(removed.files)+" "+std::to_string(removed.directories)+" "+std::to_string(removed.symlinks));return 0;
 }catch(const af::Error& error){try{af::Send(control,"ERROR "+std::to_string(id)+" "+std::to_string(error.number)+" "+af::Hex(error.stage)+" "+af::Hex(af::Message(error.what())));}catch(...){}return 1;}
 catch(const std::exception& error){try{af::Send(control,"ERROR "+std::to_string(id)+" 0 "+af::Hex("output-retention-native")+" "+af::Hex(af::Message(error.what())));}catch(...){}return 1;}}
} // namespace suitecut::runner::output_retention_files
