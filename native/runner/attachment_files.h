// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <fcntl.h>
#include <filesystem>
#include <poll.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
namespace suitecut::runner::attachment_files {
inline constexpr size_t kChunk=16384,kBytes=16*1024*1024;
enum class FailureOrigin { Operation, Infrastructure };
struct Error:std::runtime_error{std::string stage;int number;FailureOrigin origin;Error(std::string where,int code,std::string message,FailureOrigin cause=FailureOrigin::Infrastructure):std::runtime_error(std::move(message)),stage(std::move(where)),number(code),origin(cause){}};
struct FD{int value=-1;explicit FD(int fd=-1):value(fd){}~FD(){if(value>=0)::close(value);}FD(const FD&)=delete;FD& operator=(const FD&)=delete;};
inline double Now(){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();}
inline std::string Hex(std::string_view value){static constexpr char digits[]="0123456789abcdef";std::string out;out.reserve(value.size()*2);for(const unsigned char byte:value){out+=digits[byte>>4];out+=digits[byte&15];}return out.empty()?"-":out;}
inline std::string Unhex(std::string_view value,size_t maximum){if(value=="-")return {};if(value.size()%2||value.size()/2>maximum)throw Error("protocol",0,"Native attachment scalar exceeds bound");std::string out;out.reserve(value.size()/2);auto digit=[](char x)->int{if(x>='0'&&x<='9')return x-'0';if(x>='a'&&x<='f')return x-'a'+10;throw Error("protocol",0,"Invalid native attachment hexadecimal scalar");};for(size_t i=0;i<value.size();i+=2)out+=static_cast<char>((digit(value[i])<<4)|digit(value[i+1]));return out;}
inline uint64_t UInt(std::string_view value){uint64_t result=0;const auto actual=std::from_chars(value.data(),value.data()+value.size(),result);if(value.empty()||actual.ec!=std::errc()||actual.ptr!=value.data()+value.size())throw Error("protocol",0,"Invalid native attachment integer");return result;}
inline double Number(std::string_view value){double result=0;const auto actual=std::from_chars(value.data(),value.data()+value.size(),result);if(value.empty()||actual.ec!=std::errc()||actual.ptr!=value.data()+value.size()||!std::isfinite(result)||result<=0)throw Error("protocol",0,"Invalid native attachment original deadline");return result;}
inline std::vector<std::string> Fields(const std::string& line){std::istringstream in(line);std::vector<std::string> out;std::string field;while(in>>field){if(out.size()>=14)throw Error("protocol",0,"Native attachment field count exceeded");out.push_back(std::move(field));}return out;}
inline std::string Message(std::string_view value){
 if(value.size()<=4096)return std::string(value);
 std::string prefix(value.substr(0,4096));
 while(!prefix.empty()&&(static_cast<unsigned char>(prefix.back())&0xc0)==0x80)prefix.pop_back();
 if(!prefix.empty()&&static_cast<unsigned char>(prefix.back())>=0xc0)prefix.pop_back();
 return prefix;
}
inline void Live(double deadline){if(Now()>=deadline)throw Error("deadline",0,"Native attachment original deadline exceeded",FailureOrigin::Operation);}
inline void System(const char* stage,FailureOrigin origin=FailureOrigin::Operation){const int actual=errno;throw Error(stage,actual,"Native attachment filesystem operation failed",origin);}
inline void Send(int fd,const std::string& line){if(line.size()>65536)throw Error("protocol",0,"Native attachment outgoing packet exceeded");const auto data=line+'\n';size_t offset=0;while(offset<data.size()){const auto count=::send(fd,data.data()+offset,data.size()-offset,MSG_NOSIGNAL);if(count>0)offset+=static_cast<size_t>(count);else if(count<0&&errno==EINTR)continue;else System("control-write",FailureOrigin::Infrastructure);}}
class Input{
 public:explicit Input(int fd):fd_(fd){}
 std::vector<std::string> Next(double deadline){for(;;){Live(deadline);const auto end=buffer_.find('\n');if(end!=std::string::npos){const auto line=buffer_.substr(0,end);buffer_.erase(0,end+1);return Fields(line);}pollfd poll{fd_,POLLIN,0};const int ready=::poll(&poll,1,10);if(ready<0&&errno==EINTR)continue;if(ready<0)System("control-poll",FailureOrigin::Infrastructure);if(!ready)continue;std::array<char,4096> data;const auto count=::read(fd_,data.data(),data.size());if(count>0){if(buffer_.size()+static_cast<size_t>(count)>65536)throw Error("protocol",0,"Native attachment incoming packet exceeded");buffer_.append(data.data(),static_cast<size_t>(count));}else if(count<0&&errno==EINTR)continue;else throw Error("control-read",count<0?errno:0,"Native attachment owner control closed");}}
 private:int fd_;std::string buffer_;
};
inline void WriteAll(int fd,std::string_view data,double deadline){size_t offset=0;while(offset<data.size()){Live(deadline);const auto count=::write(fd,data.data()+offset,data.size()-offset);if(count>0)offset+=static_cast<size_t>(count);else if(count<0&&errno==EINTR)continue;else if(count<0)System("write");else throw Error("write",0,"Native attachment filesystem write made no progress",FailureOrigin::Operation);}}
// The caller launches this native worker in a separate owned process. All
// potentially long read/write/fsync/link work stays off the runner event loop.
inline int Worker(int control=3){uint64_t id=0;bool typed_failure=false;try{
 ::signal(SIGXFSZ,SIG_IGN);
 Input input(control);const auto init=input.Next(Now()+30000);
 if((init.size()!=10&&init.size()!=11)||init[0]!="INIT"||(init.size()==11&&init[10]!="R"))throw Error("protocol",0,"Invalid native attachment initialization");
 typed_failure=init.size()==11;
 id=UInt(init[1]);const auto expected=UInt(init[3]),dev=UInt(init[8]),ino=UInt(init[9]);auto directory=Unhex(init[4],8192);const auto name=Unhex(init[5],1024),source=Unhex(init[6],8192);const auto deadline=Number(init[7]);
 if(!id||(init[2]!="B"&&init[2]!="P")||expected>kBytes||directory.empty()||directory.find('\0')!=std::string::npos||name.empty()||name.find('/')!=std::string::npos||name=="."||name==".."||name.find('\0')!=std::string::npos||source.find('\0')!=std::string::npos)throw Error("protocol",0,"Invalid native attachment file admission");
 Live(deadline);
 const bool create_directory=!dev&&!ino;
 if(create_directory){
  std::filesystem::create_directories(directory);
  auto pattern=(std::filesystem::path(directory)/"suitecut-attachments-XXXXXX").string();std::vector<char> bytes(pattern.begin(),pattern.end());bytes.push_back(0);
  const auto made=::mkdtemp(bytes.data());if(!made)System("directory-create");directory=made;
 }
 FD dir(::open(directory.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC));if(dir.value<0)System("directory-open");struct stat dir_info{};if(fstat(dir.value,&dir_info))System("directory-stat");if(!create_directory&&(static_cast<uint64_t>(dir_info.st_dev)!=dev||static_cast<uint64_t>(dir_info.st_ino)!=ino))throw Error("directory",0,"Native attachment directory identity changed");
 if(create_directory)Send(control,"DIRECTORY "+std::to_string(id)+" "+Hex(directory)+" "+std::to_string(static_cast<uint64_t>(dir_info.st_dev))+" "+std::to_string(static_cast<uint64_t>(dir_info.st_ino)));
 FD output(::openat(dir.value,".",O_TMPFILE|O_RDWR|O_CLOEXEC,0600));if(output.value<0)System("anonymous-open");struct stat info{};if(fstat(output.value,&info))System("output-stat");Send(control,"OPEN "+std::to_string(id)+" "+std::to_string(static_cast<uint64_t>(info.st_dev))+" "+std::to_string(static_cast<uint64_t>(info.st_ino)));
 uint64_t count=0,sequence=0;
 if(init[2]=="B"){
  for(;;){const auto fields=input.Next(deadline);if(fields.size()==2&&fields[0]=="FINISH"&&UInt(fields[1])==id){if(count!=expected)throw Error("body",0,"Native attachment body size does not match actual chunks");break;}if(fields.size()!=4||fields[0]!="CHUNK"||UInt(fields[1])!=id||sequence==UINT64_MAX||UInt(fields[2])!=++sequence)throw Error("protocol",0,"Native attachment body chunk sequence mismatch");const auto bytes=Unhex(fields[3],kChunk);if(bytes.empty()||bytes.size()>expected-count)throw Error("body",0,"Native attachment body chunk exceeds admitted bytes");WriteAll(output.value,bytes,deadline);count+=bytes.size();Send(control,"CHUNK_OK "+std::to_string(id)+" "+std::to_string(sequence)+" "+std::to_string(count));}
 }else{
  Live(deadline);FD file(::open(source.c_str(),O_RDONLY|O_NONBLOCK|O_CLOEXEC));if(file.value<0)System("source-open");struct stat actual{};if(fstat(file.value,&actual))System("source-stat");if(!S_ISREG(actual.st_mode))throw Error("source",0,"Native attachment source must be a regular file",FailureOrigin::Operation);if(actual.st_size<0||static_cast<uint64_t>(actual.st_size)>kBytes)throw Error("limit",0,"Native attachment file exceeds16MiB",FailureOrigin::Operation);std::array<char,kChunk> data;for(;;){Live(deadline);const auto size=::read(file.value,data.data(),data.size());if(size<0&&errno==EINTR)continue;if(size<0)System("source-read");if(!size)break;if(static_cast<uint64_t>(size)>kBytes-count)throw Error("limit",0,"Native attachment actual file bytes exceed16MiB",FailureOrigin::Operation);WriteAll(output.value,std::string_view(data.data(),static_cast<size_t>(size)),deadline);count+=static_cast<uint64_t>(size);}
 }
 Live(deadline);if(::fsync(output.value))System("file-fsync");if(fstat(output.value,&info))System("output-stat");if(info.st_size<0||static_cast<uint64_t>(info.st_size)!=count)throw Error("file",0,"Native attachment actual output byte size changed");
 Send(control,"READY "+std::to_string(id)+" "+std::to_string(count)+" "+std::to_string(static_cast<uint64_t>(info.st_dev))+" "+std::to_string(static_cast<uint64_t>(info.st_ino)));
 const auto commit=input.Next(deadline);if(commit.size()!=2||commit[0]!="COMMIT"||UInt(commit[1])!=id)throw Error("protocol",0,"Native attachment publication was not admitted");Live(deadline);
 if(::linkat(output.value,"",dir.value,name.c_str(),AT_EMPTY_PATH)){const int first=errno;if(first!=EPERM&&first!=ENOENT)System("publish-link");const auto descriptor="/proc/self/fd/"+std::to_string(output.value);if(::linkat(AT_FDCWD,descriptor.c_str(),dir.value,name.c_str(),AT_SYMLINK_FOLLOW))System("publish-link");}
 if(::fsync(dir.value))System("directory-fsync");
 Send(control,"COMMITTED "+std::to_string(id)+" "+std::to_string(count));
 return 0;
 }catch(const Error& error){try{Send(control,"ERROR "+std::to_string(id)+" "+std::to_string(error.number)+" "+Hex(error.stage)+" "+Hex(Message(error.what()))+(typed_failure?(error.origin==FailureOrigin::Operation?" O":" I"):""));}catch(...){}return 1;}catch(const std::system_error& error){try{Send(control,"ERROR "+std::to_string(id)+" "+std::to_string(error.code().value())+" "+Hex("filesystem")+" "+Hex(Message(error.what()))+(typed_failure?" O":""));}catch(...){}return 1;}catch(const std::exception& error){try{Send(control,"ERROR "+std::to_string(id)+" 0 "+Hex("native")+" "+Hex(Message(error.what()))+(typed_failure?" I":""));}catch(...){}return 1;}}
// Rollback also runs in an owned native process. Parent cancellation/timeout
// never releases the physical job while actual unlink/fsync work is pending.
inline int Cleanup(int control=3){uint64_t id=0;try{
 Input input(control);const auto f=input.Next(Now()+30000);
 if(f.size()!=9||f[0]!="CLEANUP")throw Error("protocol",0,"Invalid native attachment rollback packet");
 id=UInt(f[1]);const auto directory=Unhex(f[2],8192),name=Unhex(f[3],1024);const auto dev=UInt(f[4]),ino=UInt(f[5]),file_dev=UInt(f[6]),file_ino=UInt(f[7]);const auto deadline=Number(f[8]);
 if(!id||directory.empty()||directory.front()!='/'||directory.find('\0')!=std::string::npos||name.empty()||name.find('/')!=std::string::npos||name.find('\0')!=std::string::npos||name=="."||name==".."||!ino)throw Error("protocol",0,"Invalid native attachment rollback identity");
 Live(deadline);FD dir(::open(directory.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC));if(dir.value<0)System("cleanup-directory-open");struct stat actual{};if(fstat(dir.value,&actual))System("cleanup-directory-stat");
 if(static_cast<uint64_t>(actual.st_dev)!=dev||static_cast<uint64_t>(actual.st_ino)!=ino)throw Error("cleanup",0,"Native attachment rollback directory identity changed");
 if(file_ino){
  Live(deadline);if(fstatat(dir.value,name.c_str(),&actual,AT_SYMLINK_NOFOLLOW)){if(errno!=ENOENT)System("cleanup-file-stat");}
  else if(static_cast<uint64_t>(actual.st_dev)!=file_dev||static_cast<uint64_t>(actual.st_ino)!=file_ino){} // Preserve actual foreign inode.
  else if(unlinkat(dir.value,name.c_str(),0))System("cleanup-file-unlink");
 }
 Live(deadline);if(::fsync(dir.value))System("cleanup-directory-fsync");
 // Directory removal is intentionally omitted: an empty, native-created
 // directory is harmless and does not require path-based deletion after its
 // pinned descriptor was closed. No temporary or canceled artifact bytes remain.
 Send(control,"CLEANED "+std::to_string(id));return 0;
 }catch(const Error& error){try{Send(control,"ERROR "+std::to_string(id)+" "+std::to_string(error.number)+" "+Hex(error.stage)+" "+Hex(Message(error.what())));}catch(...){}return 1;}catch(const std::exception& error){try{Send(control,"ERROR "+std::to_string(id)+" 0 "+Hex("native")+" "+Hex(Message(error.what())));}catch(...){}return 1;}
}
} // namespace suitecut::runner::attachment_files
