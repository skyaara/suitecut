// SPDX-License-Identifier: MIT
#include "suitecut/credentials.hpp"
#include "credential_login.hpp"
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

using suitecut::CredentialVault;
using suitecut::Json;
using namespace std::chrono_literals;
namespace {
constexpr const char* password="unit-test master password, supplied by caller";
constexpr const char* secret="private-token-東京-DO-NOT-STORE-AS-PLAINTEXT-123456789";
void check(bool condition,const char* message) {
  if (!condition) throw std::runtime_error(message);
}
template<class Function> void rejects(Function function,const char* message) {
  try { function(); }
  catch (const std::exception& error) {
    check(std::string(error.what()).find(secret)==std::string::npos,"Error leaked credential value");
    return;
  }
  throw std::runtime_error(message);
}
struct Directory {
  std::filesystem::path path;
  Directory() {
    char pattern[]="/tmp/suitecut-credentials-test-XXXXXX";
    const auto created=::mkdtemp(pattern);
    if (!created) throw std::runtime_error("Cannot create private vault test directory");
    path=std::filesystem::canonical(created);
  }
  ~Directory() { std::error_code ignored; std::filesystem::remove_all(path,ignored); }
};
std::vector<unsigned char> read(const std::filesystem::path& path) {
  std::ifstream input(path,std::ios::binary);
  if (!input) throw std::runtime_error("Cannot read encrypted test artifact");
  return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
}
void write(const std::filesystem::path& path,const std::vector<unsigned char>& bytes) {
  const int fd=::open(path.c_str(),O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC,0600);
  if (fd<0) throw std::runtime_error("Cannot write encrypted test artifact");
  std::size_t offset=0;
  while (offset<bytes.size()) {
    const auto count=::write(fd,bytes.data()+offset,bytes.size()-offset);
    if (count<=0) {::close(fd);throw std::runtime_error("Cannot write complete encrypted fixture");}
    offset+=static_cast<std::size_t>(count);
  }
  ::close(fd);
}
int mode(const std::filesystem::path& path) {
  struct stat info{};
  if (::lstat(path.c_str(),&info)!=0) throw std::runtime_error("Cannot stat vault artifact");
  return info.st_mode&07777;
}
Json credential() { return Json{{"username","test-user"},{"password",secret},{"url","https://example.invalid/login"}}; }
void private_contents(const std::filesystem::path& file) {
  const auto bytes=read(file);
  const std::string encoded(bytes.begin(),bytes.end());
  check(encoded.find(secret)==std::string::npos,"Vault contains plaintext credential");
  check(encoded.find("test-user")==std::string::npos,"Vault contains plaintext username");
  check(encoded.find("entry-one")==std::string::npos,"Vault contains plaintext entry metadata");
  check(mode(file)==0600&&mode(file.string()+".lock")==0600,"Vault or lock permissions are not0600");
  check(mode(file.parent_path())==0700,"Vault parent permissions are not0700");
}
void core() {
  Directory directory;
  const auto file=directory.path/"nested"/"vault.enc";
  {
    CredentialVault vault(file.string(),password);
    check(vault.list().empty(),"New vault is not empty");
    vault.put("entry-one",credential());
    check(vault.get("entry-one")==credential(),"Credential failed roundtrip");
    const auto first=read(file);
    vault.put("entry-one",credential());
    const auto second=read(file);
    check(first!=second,"Repeated encryption reused identical ciphertext");
    check(!std::equal(first.begin()+16,first.begin()+32,second.begin()+16),"Salt was reused");
    check(!std::equal(first.begin()+32,first.begin()+44,second.begin()+32),"Nonce was reused");
    vault.put("entry-two",Json{{"token","another-credential"}});
    check(vault.list()==std::vector<std::string>({"entry-one","entry-two"}),"List returned data beyond sorted entry names");
    private_contents(file);
  }
  CredentialVault reopened(file.string(),password);
  check(reopened.get("entry-one")==credential(),"Persisted credential did not survive reopening");
  check(reopened.remove("entry-two"),"Existing entry was not removed");
  check(!reopened.remove("missing"),"Missing entry removal changed vault");
  rejects([&]{reopened.get("entry-two");},"Removed credential is still available");
  auto moved=std::move(reopened);
  rejects([&]{reopened.list();},"Moved vault remained usable");
  check(moved.list()==std::vector<std::string>({"entry-one"}),"Moved vault lost entries");
  private_contents(file);
  std::size_t files=0;
  for (const auto& entry:std::filesystem::directory_iterator(file.parent_path())) {
    (void)entry; ++files;
  }
  check(files==2,"Atomic write left a temporary file behind");
}
void tamper() {
  Directory directory;
  const auto file=directory.path/"vault.enc";
  CredentialVault vault(file.string(),password);
  vault.put("entry-one",credential());
  const auto original=read(file);
  rejects([&]{CredentialVault wrong(file.string(),"incorrect master password");},"Wrong password was accepted");
  check(read(file)==original,"Wrong password overwrote the vault");
  for (const auto offset:std::array<std::size_t,4>{12,32,48,original.size()-1}) {
    auto altered=original; altered.at(offset)^=1; write(file,altered);
    rejects([&]{CredentialVault invalid(file.string(),password);},"Tampered authenticated artifact was accepted");
    rejects([&]{vault.put("new-entry",credential());},"Existing object overwrote a tampered vault");
    check(read(file)==altered,"Authentication failure mutated the encrypted file");
    write(file,original);
  }
  auto truncated=original; truncated.resize(20); write(file,truncated);
  rejects([&]{vault.remove("entry-one");},"Truncated artifact was accepted");
  check(read(file)==truncated,"Truncated artifact was overwritten");
  write(file,original);
  check(vault.get("entry-one")==credential(),"Restoring authentic ciphertext failed");
}
// Independently encode authenticated boundary fixtures so entry-count limits
// can be checked without hundreds of deliberately expensive password KDFs.
void authenticated_fixture(const std::filesystem::path& path,const Json& payload) {
  const auto plaintext=payload.dump();
  std::vector<unsigned char> bytes(48+plaintext.size()+16);
  const std::array<unsigned char,8> prefix{'S','C','V','A','U','L','T',0};
  std::copy(prefix.begin(),prefix.end(),bytes.begin());
  auto integer=[&](std::size_t at,std::uint32_t value) {
    for (int i=3;i>=0;--i) {bytes[at+i]=static_cast<unsigned char>(value);value>>=8;}
  };
  integer(8,1);integer(12,600000);integer(44,plaintext.size());
  check(RAND_bytes(bytes.data()+16,16)==1&&RAND_bytes(bytes.data()+32,12)==1,"Fixture randomness failed");
  std::array<unsigned char,32> key{};
  check(PKCS5_PBKDF2_HMAC(password,static_cast<int>(std::char_traits<char>::length(password)),
      bytes.data()+16,16,600000,EVP_sha256(),key.size(),key.data())==1,"Fixture KDF failed");
  EVP_CIPHER_CTX* cipher=EVP_CIPHER_CTX_new();
  check(cipher!=nullptr,"Fixture cipher allocation failed");
  int count=0,last=0;
  const bool ok=EVP_EncryptInit_ex(cipher,EVP_aes_256_gcm(),nullptr,nullptr,nullptr)==1&&
      EVP_CIPHER_CTX_ctrl(cipher,EVP_CTRL_GCM_SET_IVLEN,12,nullptr)==1&&
      EVP_EncryptInit_ex(cipher,nullptr,nullptr,key.data(),bytes.data()+32)==1&&
      EVP_EncryptUpdate(cipher,nullptr,&count,bytes.data(),48)==1&&
      EVP_EncryptUpdate(cipher,bytes.data()+48,&count,reinterpret_cast<const unsigned char*>(plaintext.data()),plaintext.size())==1&&
      EVP_EncryptFinal_ex(cipher,bytes.data()+48+count,&last)==1&&
      EVP_CIPHER_CTX_ctrl(cipher,EVP_CTRL_GCM_GET_TAG,16,bytes.data()+48+plaintext.size())==1;
  EVP_CIPHER_CTX_free(cipher); OPENSSL_cleanse(key.data(),key.size());
  check(ok,"Fixture encryption failed"); write(path,bytes);
}
void bounds() {
  Directory directory;
  const auto file=directory.path/"vault.enc";
  CredentialVault vault(file.string(),password);
  const auto original=read(file);
  rejects([&]{vault.put("",credential());},"Empty credential name was accepted");
  rejects([&]{vault.put(std::string(CredentialVault::max_name_bytes+1,'n'),credential());},"Oversized credential name was accepted");
  rejects([&]{vault.put("bad\nname",credential());},"Control character credential name was accepted");
  rejects([&]{vault.put("entry-one",Json{{"password",std::string(CredentialVault::max_credential_bytes+1,'s')}});},"Oversized credential was accepted");
  check(read(file)==original,"Resource validation failure changed the vault");
  Json entries=Json::object{};
  for (std::size_t i=0;i<CredentialVault::max_entries;++i) entries["entry-"+std::to_string(i)]=Json{{"token","test-token"}};
  authenticated_fixture(file,entries);
  const auto at_capacity=read(file);
  rejects([&]{vault.put("overflow-entry",credential());},"Entry capacity was not enforced");
  check(read(file)==at_capacity,"Entry overflow changed existing credentials");
  check(vault.list().size()==CredentialVault::max_entries,"Valid capacity boundary was rejected");
  entries["overflow-entry"]=credential(); authenticated_fixture(file,entries);
  const auto overflow=read(file);
  rejects([&]{CredentialVault invalid(file.string(),password);},"Authenticated entry overflow was accepted");
  check(read(file)==overflow,"Authenticated oversized payload was rewritten");
  const int fd=::open(file.c_str(),O_WRONLY|O_CLOEXEC);
  check(fd>=0,"Cannot open oversized file fixture");
  const int resized=::ftruncate(fd,CredentialVault::max_file_bytes+1); ::close(fd);
  check(resized==0,"Cannot resize oversized file fixture");
  rejects([&]{CredentialVault invalid(file.string(),password);},"Encrypted file byte limit was not enforced");
  rejects([&]{CredentialVault empty((directory.path/"empty.enc").string(),"");},"Empty master password was accepted");
}
void paths() {
  Directory directory;
  const auto file=directory.path/"vault.enc";
  CredentialVault vault(file.string(),password);
  const auto original=read(file);
  check(::chmod(file.c_str(),0644)==0,"Cannot change fixture permissions");
  rejects([&]{vault.list();},"Insecure vault file permissions were accepted");
  check(::chmod(file.c_str(),0600)==0,"Cannot restore fixture permissions");
  check(::chmod(directory.path.c_str(),0755)==0,"Cannot change parent permissions");
  rejects([&]{vault.put("entry-one",credential());},"Insecure parent permissions were accepted");
  check(::chmod(directory.path.c_str(),0700)==0,"Cannot restore parent permissions");
  check(read(file)==original,"Insecure permission rejection modified encrypted data");
  const auto linked=directory.path/"link.enc";
  check(::symlink(file.c_str(),linked.c_str())==0,"Cannot create symlink fixture");
  rejects([&]{CredentialVault invalid(linked.string(),password);},"Vault symlink was accepted");
  const auto alias=directory.path/"directory-link";
  check(::symlink(directory.path.c_str(),alias.c_str())==0,"Cannot create directory symlink fixture");
  rejects([&]{CredentialVault invalid((alias/"other.enc").string(),password);},"Directory symlink was traversed");
  const auto lock_link=directory.path/"unsafe.enc.lock";
  check(::symlink(file.c_str(),lock_link.c_str())==0,"Cannot create lock symlink fixture");
  rejects([&]{CredentialVault invalid((directory.path/"unsafe.enc").string(),password);},"Lock symlink was accepted");
  const auto hardlink=directory.path/"hard.enc";
  check(::link(file.c_str(),hardlink.c_str())==0,"Cannot create hardlink fixture");
  rejects([&]{vault.list();},"Hardlinked encrypted file was accepted");
  check(::unlink(hardlink.c_str())==0,"Cannot retire hardlink fixture");
  const auto fifo=directory.path/"fifo.enc";
  check(::mkfifo(fifo.c_str(),0600)==0,"Cannot create nonregular-file fixture");
  const auto start=std::chrono::steady_clock::now();
  rejects([&]{CredentialVault invalid(fifo.string(),password);},"Nonregular encrypted file was accepted");
  check(std::chrono::steady_clock::now()-start<250ms,"Unsafe file open blocked");
  check(read(file)==original,"Unsafe path rejection changed authentic vault");
}
void concurrency() {
  Directory directory;
  const auto file=directory.path/"vault.enc";
  CredentialVault first(file.string(),password),second(file.string(),password);
  std::array<std::exception_ptr,2> errors{};
  std::thread a([&]{try {first.put("thread-a",Json{{"token","a"}});}catch(...){errors[0]=std::current_exception();}});
  std::thread b([&]{try {second.put("thread-b",Json{{"token","b"}});}catch(...){errors[1]=std::current_exception();}});
  a.join();b.join();for(const auto& error:errors)if(error)std::rethrow_exception(error);
  check(first.list()==std::vector<std::string>({"thread-a","thread-b"}),"Concurrent same-process writer lost an update");
  const pid_t child=::fork();
  check(child>=0,"Cannot create supervised cross-process writer fixture");
  if (child==0) {
    try {CredentialVault other(file.string(),password);other.put("process-child",Json{{"token","child"}});::_exit(0);}
    catch (...) {::_exit(1);}
  }
  std::exception_ptr parent_error;
  try {first.put("process-parent",Json{{"token","parent"}});}catch(...){parent_error=std::current_exception();}
  int status=0; while (::waitpid(child,&status,0)<0) if(errno!=EINTR) throw std::runtime_error("Cannot reap vault test child");
  if(parent_error)std::rethrow_exception(parent_error);
  check(WIFEXITED(status)&&WEXITSTATUS(status)==0,"Cross-process writer failed");
  check(first.list()==std::vector<std::string>({"process-child","process-parent","thread-a","thread-b"}),"Concurrent process writer lost an update");
}
void origins() {
  for(const auto& [url,expected]:std::vector<std::pair<std::string,std::string>>{
      {"HTTP://EXAMPLE.COM:80/login","http://example.com"},
      {"https://Example.COM:00443/login","https://example.com"},
      {"https://Example.COM:8443/login?x=1","https://example.com:8443"},
      {"http://127.0.0.1:8080/?x=1","http://127.0.0.1:8080"},
      {"http://localhost:0/login","http://localhost:0"},
      {"https://XN--BCHER-KVA.EXAMPLE/login","https://xn--bcher-kva.example"},
      {"https://example.com./login","https://example.com."},
      {"https://example.com/login/東京","https://example.com"},
      {"http://[2001:0DB8:0:0:0:0:0:1]:80/login","http://[2001:db8::1]"},
      {"https://[::ffff:192.0.2.128]:0443/login","https://[::ffff:c000:280]"},
      {"http://[0:0:0:0:0:0:0:0]/login","http://[::]"},
      {"http://[1:0:0:2:0:0:3:4]/login","http://[1::2:0:0:3:4]"}}) {
    check(suitecut::detail::strict_origin(url)==expected,"Host-side credential origin serialization is incorrect");
  }
  for(const auto& url:std::vector<std::string>{
      "https:example.com","javascript:alert(1)","file:///tmp/file","http:///login",
      "https://user:secret@example.com/login","https://user@example.com/login",
      "https://example%2ecom/login","https://éxample.com/login","https://example.com:/login",
      "https://example.com:65536/login","https://example.com:-1/login","https://example.com:+80/login",
      "https://example.com:443x/login","https://example.com\\@attacker.invalid/login",
      "https://example.com/\nlogin"," https://example.com/login","https://example.com/login ",
      "http://127.1/login","http://2130706433/login","http://0x7f.0.0.1/login",
      "http://0177.0.0.1/login","http://256.0.0.1/login","http://127.0.0.1./login",
      "http://example.123/login","http://bad..example/login","http://-bad.example/login",
      "http://bad-.example/login","http://bad_host.example/login","http://[::1]extra/login",
      "http://[::1]:/login","http://[gggg::1]/login","http://[fe80::1%25eth0]/login",
      "http://::1/login","http://[::1/login","http://example.com:80:90/login"}) {
    rejects([&]{suitecut::detail::strict_origin(url);},"Unsafe or ambiguous credential URL origin was accepted");
  }
  rejects([&]{suitecut::detail::strict_origin("https://"+std::string(64,'a')+".example/login");},"Oversized DNS label was accepted");
  rejects([&]{suitecut::detail::strict_origin("https://example.com/"+std::string(8192,'x'));},"Oversized login URL was accepted");
}
} // namespace
int main(int argc,char** argv) {
  try {
    const std::string group=argc>1?argv[1]:"all";
    bool recognized=group=="all";
    for (const auto& [name,test]:std::vector<std::pair<std::string,void(*)()>>{
        {"core",core},{"tamper",tamper},{"bounds",bounds},{"paths",paths},{"concurrency",concurrency},{"origins",origins}}) {
      if(group=="all"||group==name) {recognized=true;test();}
    }
    check(recognized,"Unknown credential test group");
    std::cout<<"PASS authenticated credential vault "<<group<<" checks\n";
    return 0;
  } catch (const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
