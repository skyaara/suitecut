// SPDX-License-Identifier: MIT
#pragma once
#include "attachment_files.h"
namespace suitecut::runner::output_directory_files {
namespace af=attachment_files;
// Native helpers perform all potentially blocking mkdir/fsync/rmdir work.
// A CREATED message pins the actual inode; READY is never an OS-exit receipt.
inline int Worker(int control=3){uint64_t id=0;try{
 af::Input input(control);const auto f=input.Next(af::Now()+30000);
 if(f.size()!=5||f[0]!="CREATE")throw af::Error("protocol",0,"Invalid native output directory initialization");
 id=af::UInt(f[1]);const auto parent=af::Unhex(f[2],8192),name=af::Unhex(f[3],256);const auto deadline=af::Number(f[4]);
 if(!id||parent.empty()||parent.front()!='/'||parent.find('\0')!=std::string::npos||name.empty()||name=="."||name==".."||name.find('/')!=std::string::npos||name.find('\0')!=std::string::npos)throw af::Error("protocol",0,"Invalid native output directory admission");
 af::Live(deadline);std::filesystem::create_directories(parent);af::Live(deadline);
 af::FD directory(::open(parent.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC));if(directory.value<0)af::System("output-parent-open");
 struct stat parent_info{};if(::fstat(directory.value,&parent_info))af::System("output-parent-stat");
 if(::mkdirat(directory.value,name.c_str(),0700))af::System("output-mkdir");
 af::FD output(::openat(directory.value,name.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC));if(output.value<0)af::System("output-open");
 struct stat actual{};if(::fstat(output.value,&actual))af::System("output-stat");
 af::Send(control,"CREATED "+std::to_string(id)+" "+std::to_string(static_cast<uint64_t>(actual.st_dev))+" "+std::to_string(static_cast<uint64_t>(actual.st_ino))+" "+std::to_string(static_cast<uint64_t>(parent_info.st_dev))+" "+std::to_string(static_cast<uint64_t>(parent_info.st_ino)));
 af::Live(deadline);if(::fsync(output.value))af::System("output-fsync");af::Live(deadline);if(::fsync(directory.value))af::System("output-parent-fsync");af::Live(deadline);
 af::FD current(::open(parent.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC));if(current.value<0)af::System("output-current-parent-open");struct stat checked{};if(::fstat(current.value,&checked))af::System("output-current-parent-stat");if(checked.st_dev!=parent_info.st_dev||checked.st_ino!=parent_info.st_ino)throw af::Error("output-parent-identity",0,"Native output parent identity changed before publication");
 if(::fstatat(current.value,name.c_str(),&checked,AT_SYMLINK_NOFOLLOW))af::System("output-current-directory-stat");
 if(checked.st_dev!=actual.st_dev||checked.st_ino!=actual.st_ino||!S_ISDIR(checked.st_mode))throw af::Error("output-directory-identity",0,"Native output directory identity changed before publication");
 af::Send(control,"READY "+std::to_string(id));return 0;
 }catch(const af::Error& error){try{af::Send(control,"ERROR "+std::to_string(id)+" "+std::to_string(error.number)+" "+af::Hex(error.stage)+" "+af::Hex(af::Message(error.what())));}catch(...){}return 1;}
 catch(const std::system_error& error){try{af::Send(control,"ERROR "+std::to_string(id)+" "+std::to_string(error.code().value())+" "+af::Hex("output-filesystem")+" "+af::Hex(af::Message(error.what())));}catch(...){}return 1;}
 catch(const std::exception& error){try{af::Send(control,"ERROR "+std::to_string(id)+" 0 "+af::Hex("output-native")+" "+af::Hex(af::Message(error.what())));}catch(...){}return 1;}}
inline int Cleanup(int control=3){uint64_t id=0;try{
 af::Input input(control);const auto f=input.Next(af::Now()+30000);
 if(f.size()!=9||f[0]!="ROLLBACK")throw af::Error("protocol",0,"Invalid native output directory rollback initialization");
 id=af::UInt(f[1]);const auto parent=af::Unhex(f[2],8192),name=af::Unhex(f[3],256);const auto device=af::UInt(f[4]),inode=af::UInt(f[5]);const auto parent_device=af::UInt(f[6]),parent_inode=af::UInt(f[7]);const auto deadline=af::Number(f[8]);
 if(!id||!inode||parent.empty()||parent.front()!='/'||parent.find('\0')!=std::string::npos||name.empty()||name=="."||name==".."||name.find('/')!=std::string::npos||name.find('\0')!=std::string::npos)throw af::Error("protocol",0,"Invalid native output directory rollback identity");
 af::Live(deadline);af::FD directory(::open(parent.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC));if(directory.value<0)af::System("output-cleanup-parent-open");
 struct stat parent_info{};if(::fstat(directory.value,&parent_info))af::System("output-cleanup-parent-stat");if(static_cast<uint64_t>(parent_info.st_dev)!=parent_device||static_cast<uint64_t>(parent_info.st_ino)!=parent_inode)throw af::Error("output-cleanup-parent-identity",0,"Native output directory rollback parent identity changed");
 struct stat actual{};bool removed=false,preserved=false;
 if(::fstatat(directory.value,name.c_str(),&actual,AT_SYMLINK_NOFOLLOW)){if(errno!=ENOENT)af::System("output-cleanup-stat");}
 else if(static_cast<uint64_t>(actual.st_dev)!=device||static_cast<uint64_t>(actual.st_ino)!=inode||!S_ISDIR(actual.st_mode))preserved=true;
 else{af::Live(deadline);if(::unlinkat(directory.value,name.c_str(),AT_REMOVEDIR))af::System("output-cleanup-rmdir");removed=true;}
 af::Live(deadline);if(::fsync(directory.value))af::System("output-cleanup-parent-fsync");
 af::Send(control,"CLEANED "+std::to_string(id)+" "+(removed?"removed":preserved?"foreignPreserved":"alreadyAbsent"));return 0;
 }catch(const af::Error& error){try{af::Send(control,"ERROR "+std::to_string(id)+" "+std::to_string(error.number)+" "+af::Hex(error.stage)+" "+af::Hex(af::Message(error.what())));}catch(...){}return 1;}
 catch(const std::exception& error){try{af::Send(control,"ERROR "+std::to_string(id)+" 0 "+af::Hex("output-cleanup-native")+" "+af::Hex(af::Message(error.what())));}catch(...){}return 1;}}
} // namespace suitecut::runner::output_directory_files
