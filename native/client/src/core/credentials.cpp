// SPDX-License-Identifier: MIT
#include "suitecut/credentials.hpp"
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace suitecut {
namespace {
constexpr std::size_t header_bytes = 48, tag_bytes = 16;
constexpr std::size_t payload_limit = CredentialVault::max_file_bytes - header_bytes - tag_bytes;
constexpr int kdf_iterations = 600000;
constexpr std::array<unsigned char,8> magic{'S','C','V','A','U','L','T',0};
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
[[noreturn]] void fail(const char* message) { throw std::runtime_error(message); }
void cleanse_json(Json& value) noexcept {
  try {
    if (value.is_string()) {
      auto& text = value.as_string();
      if (!text.empty()) OPENSSL_cleanse(text.data(),text.size());
    } else if (value.is_array()) {
      for (auto& item : value.as_array()) cleanse_json(item);
    } else if (value.is_object()) {
      for (auto& [name,item] : value.as_object()) { (void)name; cleanse_json(item); }
    }
  } catch (...) {}
}
struct JsonGuard {
  Json& value;
  ~JsonGuard() { cleanse_json(value); }
};
struct SecureBytes {
  std::vector<unsigned char> value;
  ~SecureBytes() { if (!value.empty()) OPENSSL_cleanse(value.data(),value.size()); }
};
struct SecureString {
  std::string value;
  ~SecureString() { if (!value.empty()) OPENSSL_cleanse(value.data(),value.size()); }
};
struct Key {
  std::array<unsigned char,32> value{};
  ~Key() { OPENSSL_cleanse(value.data(),value.size()); }
};
using Cipher = std::unique_ptr<EVP_CIPHER_CTX,decltype(&EVP_CIPHER_CTX_free)>;
void random_bytes(unsigned char* output,std::size_t size) {
  if (size>static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
      RAND_bytes(output,static_cast<int>(size))!=1) fail("Credential vault random generation failed");
}
void derive(const SecureBytes& password,const unsigned char* salt,Key& key) {
  if (PKCS5_PBKDF2_HMAC(reinterpret_cast<const char*>(password.value.data()),
      static_cast<int>(password.value.size()),salt,16,kdf_iterations,EVP_sha256(),
      static_cast<int>(key.value.size()),key.value.data())!=1)
    fail("Credential vault key derivation failed");
}
void store_u32(unsigned char* output,std::uint32_t value) {
  for (int i=3;i>=0;--i) { output[i]=static_cast<unsigned char>(value); value>>=8; }
}
std::uint32_t read_u32(const unsigned char* input) {
  std::uint32_t value=0;
  for (int i=0;i<4;++i) value=(value<<8)|input[i];
  return value;
}
void valid_name(const std::string& name) {
  if (name.empty() || name.size()>CredentialVault::max_name_bytes ||
      std::any_of(name.begin(),name.end(),[](unsigned char c){return c<0x20||c==0x7f;}))
    throw std::invalid_argument("Credential name must be bounded nonempty UTF-8 text without controls");
  try { Json(name).dump(); } catch (...) { throw std::invalid_argument("Credential name must be valid UTF-8"); }
}
JsonLimits payload_limits() {
  JsonLimits limits;
  limits.max_bytes=payload_limit; limits.max_string_bytes=CredentialVault::max_credential_bytes;
  limits.max_depth=32; limits.max_nodes=65536;
  return limits;
}
void valid_credential(const Json& credential) {
  JsonLimits limits=payload_limits();
  limits.max_bytes=CredentialVault::max_credential_bytes; limits.max_nodes=8192;
  try { SecureString encoded{credential.dump(limits)}; }
  catch (...) { throw std::invalid_argument("Credential value exceeds supported JSON or size limits"); }
}
std::vector<unsigned char> encrypt(const SecureBytes& password,const std::string& plaintext) {
  if (plaintext.size()>payload_limit) fail("Credential vault exceeds its encrypted file limit");
  std::vector<unsigned char> output(header_bytes+plaintext.size()+tag_bytes);
  std::copy(magic.begin(),magic.end(),output.begin());
  store_u32(output.data()+8,1); store_u32(output.data()+12,kdf_iterations);
  random_bytes(output.data()+16,16); random_bytes(output.data()+32,12);
  store_u32(output.data()+44,static_cast<std::uint32_t>(plaintext.size()));
  Key key; derive(password,output.data()+16,key);
  Cipher cipher(EVP_CIPHER_CTX_new(),EVP_CIPHER_CTX_free);
  if (!cipher || EVP_EncryptInit_ex(cipher.get(),EVP_aes_256_gcm(),nullptr,nullptr,nullptr)!=1 ||
      EVP_CIPHER_CTX_ctrl(cipher.get(),EVP_CTRL_GCM_SET_IVLEN,12,nullptr)!=1 ||
      EVP_EncryptInit_ex(cipher.get(),nullptr,nullptr,key.value.data(),output.data()+32)!=1)
    fail("Credential vault encryption failed");
  int written=0,final=0;
  if (EVP_EncryptUpdate(cipher.get(),nullptr,&written,output.data(),header_bytes)!=1 ||
      EVP_EncryptUpdate(cipher.get(),output.data()+header_bytes,&written,
          reinterpret_cast<const unsigned char*>(plaintext.data()),static_cast<int>(plaintext.size()))!=1 ||
      written!=static_cast<int>(plaintext.size()) ||
      EVP_EncryptFinal_ex(cipher.get(),output.data()+header_bytes+written,&final)!=1 || final!=0 ||
      EVP_CIPHER_CTX_ctrl(cipher.get(),EVP_CTRL_GCM_GET_TAG,tag_bytes,
          output.data()+header_bytes+plaintext.size())!=1)
    fail("Credential vault encryption failed");
  return output;
}
Json decrypt(const SecureBytes& password,const std::vector<unsigned char>& input) {
  if (input.size()<header_bytes+tag_bytes || input.size()>CredentialVault::max_file_bytes ||
      !std::equal(magic.begin(),magic.end(),input.begin()) || read_u32(input.data()+8)!=1 ||
      read_u32(input.data()+12)!=kdf_iterations ||
      read_u32(input.data()+44)!=input.size()-header_bytes-tag_bytes)
    fail("Credential vault authentication failed");
  const std::size_t size=input.size()-header_bytes-tag_bytes;
  Key key; derive(password,input.data()+16,key);
  Cipher cipher(EVP_CIPHER_CTX_new(),EVP_CIPHER_CTX_free);
  SecureBytes plaintext; plaintext.value.resize(size+16);
  if (!cipher || EVP_DecryptInit_ex(cipher.get(),EVP_aes_256_gcm(),nullptr,nullptr,nullptr)!=1 ||
      EVP_CIPHER_CTX_ctrl(cipher.get(),EVP_CTRL_GCM_SET_IVLEN,12,nullptr)!=1 ||
      EVP_DecryptInit_ex(cipher.get(),nullptr,nullptr,key.value.data(),input.data()+32)!=1)
    fail("Credential vault decryption failed");
  int written=0,final=0;
  if (EVP_DecryptUpdate(cipher.get(),nullptr,&written,input.data(),header_bytes)!=1 ||
      EVP_DecryptUpdate(cipher.get(),plaintext.value.data(),&written,input.data()+header_bytes,
          static_cast<int>(size))!=1 || written!=static_cast<int>(size) ||
      EVP_CIPHER_CTX_ctrl(cipher.get(),EVP_CTRL_GCM_SET_TAG,tag_bytes,
          const_cast<unsigned char*>(input.data()+header_bytes+size))!=1 ||
      EVP_DecryptFinal_ex(cipher.get(),plaintext.value.data()+written,&final)!=1 || final!=0)
    fail("Credential vault authentication failed");
  Json result;
  try {
    result=Json::parse(std::string_view(reinterpret_cast<const char*>(plaintext.value.data()),size),payload_limits());
    if (!result.is_object() || result.as_object().size()>CredentialVault::max_entries)
      fail("Credential vault payload exceeds entry limits");
    for (const auto& [name,value] : result.as_object()) { valid_name(name); valid_credential(value); }
  } catch (...) {
    cleanse_json(result);
    fail("Credential vault payload is invalid or exceeds limits");
  }
  return result;
}
#if !defined(_WIN32)
struct Fd {
  int value=-1;
  Fd()=default;
  explicit Fd(int fd):value(fd) {}
  ~Fd() { if (value>=0) ::close(value); }
  Fd(Fd&& other) noexcept:value(std::exchange(other.value,-1)) {}
  Fd& operator=(Fd&& other) noexcept {
    if (this!=&other) { if (value>=0) ::close(value); value=std::exchange(other.value,-1); }
    return *this;
  }
  Fd(const Fd&)=delete;
};
void private_file(int fd) {
  struct stat info{};
  if (::fstat(fd,&info)!=0 || !S_ISREG(info.st_mode) || info.st_uid!=::geteuid() ||
      (info.st_mode&07777)!=0600 || info.st_nlink!=1)
    fail("Credential vault file must be a private regular file");
}
std::pair<Fd,std::string> private_parent(const std::string& path) {
  if (path.empty()||path.size()>4096||path.find('\0')!=std::string::npos)
    throw std::invalid_argument("Credential vault path is invalid");
  std::filesystem::path absolute;
  try { absolute=std::filesystem::absolute(std::filesystem::path(path)); }
  catch (...) { fail("Credential vault path is invalid"); }
  for (const auto& part:absolute) if (part=="..")
    throw std::invalid_argument("Credential vault path must not contain parent traversal");
  absolute=absolute.lexically_normal();
  const auto name=absolute.filename().string();
  if (name.empty()||name=="."||name==".."||name.size()>180)
    throw std::invalid_argument("Credential vault filename is invalid");
  Fd parent(::open("/",O_RDONLY|O_DIRECTORY|O_CLOEXEC));
  if (parent.value<0) fail("Credential vault parent cannot be opened");
  for (const auto& part:absolute.parent_path().relative_path()) {
    const auto component=part.string();
    if (component.empty()||component==".") continue;
    int opened=::openat(parent.value,component.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if (opened<0&&errno==ENOENT) {
      if (::mkdirat(parent.value,component.c_str(),0700)!=0&&errno!=EEXIST)
        fail("Credential vault private directory cannot be created");
      opened=::openat(parent.value,component.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    }
    if (opened<0) fail("Credential vault directory is unavailable or contains a symlink");
    parent=Fd(opened);
  }
  struct stat info{};
  if (::fstat(parent.value,&info)!=0||!S_ISDIR(info.st_mode)||info.st_uid!=::geteuid()||
      (info.st_mode&07777)!=0700)
    fail("Credential vault parent directory must be privately owned with mode0700");
  return {std::move(parent),name};
}
std::shared_ptr<std::timed_mutex> path_mutex(int directory,const std::string& name) {
  struct stat info{};
  if (::fstat(directory,&info)!=0) fail("Credential vault directory identity is unavailable");
  const auto key=std::to_string(info.st_dev)+":"+std::to_string(info.st_ino)+":"+name;
  static std::mutex registry_mutex;
  static std::map<std::string,std::weak_ptr<std::timed_mutex>> registry;
  std::lock_guard lock(registry_mutex);
  for (auto item=registry.begin();item!=registry.end();) {
    if (item->second.expired()) item=registry.erase(item); else ++item;
  }
  auto found=registry.find(key);
  if (found!=registry.end()) if (auto existing=found->second.lock()) return existing;
  if (registry.size()>=4096) fail("Credential vault open-path capacity exceeded");
  auto result=std::make_shared<std::timed_mutex>(); registry[key]=result; return result;
}
struct FileLock {
  std::unique_lock<std::timed_mutex> local;
  Fd file;
  FileLock(std::timed_mutex& mutex,int directory,const std::string& name)
      :local(mutex,std::defer_lock) {
    const auto deadline=Clock::now()+5s;
    if (!local.try_lock_until(deadline)) fail("Credential vault lock deadline exceeded");
    struct stat parent{};
    if (::fstat(directory,&parent)!=0||!S_ISDIR(parent.st_mode)||parent.st_uid!=::geteuid()||
        (parent.st_mode&07777)!=0700) fail("Credential vault directory permissions are unsafe");
    file=Fd(::openat(directory,name.c_str(),O_RDWR|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC));
    if (file.value<0) fail("Credential vault lock file is unavailable or unsafe");
    private_file(file.value);
    struct stat info{};
    if (::fstat(file.value,&info)!=0||info.st_size!=0) fail("Credential vault lock file is invalid");
    for (;;) {
      if (::flock(file.value,LOCK_EX|LOCK_NB)==0) break;
      if (errno!=EINTR&&errno!=EWOULDBLOCK&&errno!=EAGAIN) fail("Credential vault lock failed");
      if (Clock::now()>=deadline) fail("Credential vault lock deadline exceeded");
      std::this_thread::sleep_until(std::min(deadline,Clock::now()+10ms));
    }
  }
  ~FileLock() { ::flock(file.value,LOCK_UN); }
};
void sync_file(int fd) {
  while (::fsync(fd)!=0) if (errno!=EINTR) fail("Credential vault synchronization failed");
}
#endif
} // namespace

struct CredentialVault::Impl {
  SecureBytes password;
#if !defined(_WIN32)
  Fd directory;
  std::string name,lock_name;
  std::shared_ptr<std::timed_mutex> mutex;
  Impl(const std::string& path,const std::string& master) {
    if (master.empty()||master.size()>4096) throw std::invalid_argument("Credential vault master password must contain1..4096 bytes");
    password.value.assign(master.begin(),master.end());
    auto parent=private_parent(path); directory=std::move(parent.first); name=std::move(parent.second);
    mutex=path_mutex(directory.value,name);
    lock_name=name+".lock";
    Fd lock(::openat(directory.value,lock_name.c_str(),O_RDWR|O_CREAT|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC,0600));
    if (lock.value<0) fail("Credential vault lock file is unavailable or unsafe");
    private_file(lock.value);
    struct stat lock_info{};
    if (::fstat(lock.value,&lock_info)!=0||lock_info.st_size!=0) fail("Credential vault lock file is invalid");
    FileLock held(*mutex,directory.value,lock_name);
    struct stat info{};
    if (::fstatat(directory.value,name.c_str(),&info,AT_SYMLINK_NOFOLLOW)!=0) {
      if (errno!=ENOENT) fail("Credential vault file cannot be inspected");
      write(Json::object{});
    } else {
      Json authenticated=read(); JsonGuard guard{authenticated};
    }
  }
  Json read() const {
    Fd file(::openat(directory.value,name.c_str(),O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC));
    if (file.value<0) fail("Credential vault file is missing, unavailable or unsafe");
    private_file(file.value);
    struct stat info{};
    if (::fstat(file.value,&info)!=0||info.st_size<0||
        static_cast<std::uint64_t>(info.st_size)>CredentialVault::max_file_bytes)
      fail("Credential vault file exceeds its size limit");
    std::vector<unsigned char> bytes(static_cast<std::size_t>(info.st_size));
    std::size_t offset=0;
    while (offset<bytes.size()) {
      const auto received=::read(file.value,bytes.data()+offset,bytes.size()-offset);
      if (received<0&&errno==EINTR) continue;
      if (received<=0) fail("Credential vault file read failed");
      offset+=static_cast<std::size_t>(received);
    }
    unsigned char extra=0;
    for (;;) {
      const auto received=::read(file.value,&extra,1);
      if (received<0&&errno==EINTR) continue;
      if (received!=0) fail("Credential vault file changed while being read");
      break;
    }
    return decrypt(password,bytes);
  }
  void write(const Json& entries) const {
    SecureString plaintext;
    try { plaintext.value=entries.dump(payload_limits()); }
    catch (...) { fail("Credential vault exceeds supported JSON or file limits"); }
    const auto bytes=encrypt(password,plaintext.value);
    std::array<unsigned char,12> random{};
    Fd temporary;
    std::string temporary_name;
    for (int attempt=0;attempt<8;++attempt) {
      random_bytes(random.data(),random.size());
      constexpr char hex[]="0123456789abcdef";
      temporary_name=".suitecut-vault-";
      for (auto value:random) {temporary_name+=hex[value>>4];temporary_name+=hex[value&15];}
      temporary_name+=".tmp";
      temporary=Fd(::openat(directory.value,temporary_name.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600));
      if (temporary.value>=0) break;
      if (errno!=EEXIST) fail("Credential vault temporary file cannot be created");
    }
    if (temporary.value<0) fail("Credential vault temporary file capacity exceeded");
    bool renamed=false;
    try {
      if (::fchmod(temporary.value,0600)!=0) fail("Credential vault file permissions cannot be set");
      std::size_t offset=0;
      while (offset<bytes.size()) {
        const auto written=::write(temporary.value,bytes.data()+offset,bytes.size()-offset);
        if (written<0&&errno==EINTR) continue;
        if (written<=0) fail("Credential vault file write failed");
        offset+=static_cast<std::size_t>(written);
      }
      sync_file(temporary.value);
      struct stat existing{};
      if (::fstatat(directory.value,name.c_str(),&existing,AT_SYMLINK_NOFOLLOW)==0) {
        if (!S_ISREG(existing.st_mode)||existing.st_uid!=::geteuid()||(existing.st_mode&07777)!=0600||existing.st_nlink!=1)
          fail("Credential vault destination is unsafe");
      } else if (errno!=ENOENT) fail("Credential vault destination cannot be inspected");
      if (::renameat(directory.value,temporary_name.c_str(),directory.value,name.c_str())!=0)
        fail("Credential vault atomic replacement failed");
      renamed=true; sync_file(directory.value);
    } catch (...) {
      if (!renamed) ::unlinkat(directory.value,temporary_name.c_str(),0);
      throw;
    }
  }
#else
  Impl(const std::string&,const std::string&) { fail("Credential vault currently requires POSIX file locking"); }
#endif
};

CredentialVault::CredentialVault(const std::string& path,const std::string& password)
    :impl_(std::make_unique<Impl>(path,password)) {}
CredentialVault::~CredentialVault()=default;
CredentialVault::CredentialVault(CredentialVault&&) noexcept=default;
CredentialVault& CredentialVault::operator=(CredentialVault&&) noexcept=default;
void CredentialVault::put(const std::string& name,Json credential) {
  JsonGuard credential_guard{credential};
  if (!impl_) fail("Credential vault is moved or closed");
  valid_name(name); valid_credential(credential);
#if !defined(_WIN32)
  FileLock held(*impl_->mutex,impl_->directory.value,impl_->lock_name);
  Json entries=impl_->read(); JsonGuard guard{entries};
  if (!entries.contains(name)&&entries.as_object().size()>=max_entries) fail("Credential vault entry capacity exceeded");
  if (entries.contains(name)) cleanse_json(entries.at(name));
  entries[name]=std::move(credential);
  impl_->write(entries);
#endif
}
Json CredentialVault::get(const std::string& name) const {
  if (!impl_) fail("Credential vault is moved or closed");
  valid_name(name);
#if !defined(_WIN32)
  FileLock held(*impl_->mutex,impl_->directory.value,impl_->lock_name);
  Json entries=impl_->read(); JsonGuard guard{entries};
  if (!entries.contains(name)) fail("Credential vault entry was not found");
  return entries.at(name);
#else
  fail("Credential vault currently requires POSIX file locking");
#endif
}
std::vector<std::string> CredentialVault::list() const {
  if (!impl_) fail("Credential vault is moved or closed");
  std::vector<std::string> names;
#if !defined(_WIN32)
  FileLock held(*impl_->mutex,impl_->directory.value,impl_->lock_name);
  Json entries=impl_->read(); JsonGuard guard{entries};
  for (const auto& [name,value]:entries.as_object()) {(void)value;names.push_back(name);}
#endif
  return names;
}
bool CredentialVault::remove(const std::string& name) {
  if (!impl_) fail("Credential vault is moved or closed");
  valid_name(name);
#if !defined(_WIN32)
  FileLock held(*impl_->mutex,impl_->directory.value,impl_->lock_name);
  Json entries=impl_->read(); JsonGuard guard{entries};
  if (!entries.contains(name)) return false;
  cleanse_json(entries.at(name)); entries.as_object().erase(name);
  impl_->write(entries); return true;
#else
  fail("Credential vault currently requires POSIX file locking");
#endif
}
} // namespace suitecut
