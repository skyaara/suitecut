// SPDX-License-Identifier: MIT
#include "curl_transport.h"
#include "http_cookie.h"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <atomic>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <chrono>
#include <iostream>
#include <random>
#include <thread>

int main(int argc,char** argv) {
  using namespace std::chrono_literals;
  const int listener=socket(AF_INET,SOCK_STREAM,0);assert(listener>=0);
  sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);assert(bind(listener,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0);assert(listen(listener,8)==0);
  socklen_t length=sizeof(address);assert(getsockname(listener,reinterpret_cast<sockaddr*>(&address),&length)==0);
  std::atomic<int> served{0};
  std::thread server([&]{for(int round=0;round<6;++round){
    const int peer=accept(listener,nullptr,nullptr);assert(peer>=0);std::string request;char bytes[4096];size_t body=0,header=0;
    while(true){const auto count=recv(peer,bytes,sizeof(bytes),0);assert(count>0);request.append(bytes,static_cast<size_t>(count));const auto end=request.find("\r\n\r\n");if(end!=std::string::npos){header=end+4;auto name=request.find("Content-Length:");if(name==std::string::npos)name=request.find("content-length:");if(name!=std::string::npos)body=std::strtoull(request.c_str()+name+15,nullptr,10);if(request.size()>=header+body)break;}}
    std::string reply;
    if(round==0){assert(request.starts_with("GET /body HTTP/"));assert(body==100003);assert(request.substr(header)==std::string(100003,'x'));reply="HTTP/1.1 200 OK\r\nSet-Cookie: first=1; Path=/\r\nSet-Cookie: second=2; Path=/\r\nContent-Length: 7\r\nConnection: close\r\n\r\nnative!";}
    else if(round==1){assert(request.find("Authorization:")==std::string::npos);reply="HTTP/1.1 401 Unauthorized\r\nWWW-Authenticate: Basic realm=\"native\"\r\nContent-Length: 9\r\nConnection: close\r\n\r\nchallenge";}
    else if(round==2){assert(request.find("Authorization: Basic bmF0aXZlOnBhc3N3b3Jk")!=std::string::npos);reply="HTTP/1.1 200 OK\r\nContent-Length: 13\r\nConnection: close\r\n\r\nauthenticated";}
    else {const std::string encoding=round==3?"gzip":round==4?"br":"deflate";assert(request.starts_with("GET /corrupted/"+encoding+" HTTP/"));reply="HTTP/1.1 200 OK\r\nContent-Encoding: "+encoding+"\r\nContent-Length: 13\r\nConnection: close\r\n\r\nHello, world!";}
    size_t written=0;while(written<reply.size()){const auto count=send(peer,reply.data()+written,reply.size()-written,MSG_NOSIGNAL);assert(count>0);written+=static_cast<size_t>(count);}close(peer);++served;
  }});
  const auto origin="http://127.0.0.1:"+std::to_string(ntohs(address.sin_port));suitecut::NativeCurlTransport transport;
  std::vector<uint8_t> upload(100003,'x');std::string received;suitecut::NativeCurlTransport::Response response;bool done=false;std::string error;
  suitecut::NativeCurlTransport::Request request;request.url=origin+"/body";request.method="GET";request.body=upload;
  auto data=[&](const void* bytes,size_t size){if(!bytes)received.clear();else received.append(static_cast<const char*>(bytes),size);return true;};
  auto finish=[&](auto result){response=std::move(result);done=true;};
  assert(transport.Start(1,std::move(request),data,finish,error));
  auto wait=[&](const std::string& encoding=""){const auto deadline=std::chrono::steady_clock::now()+5s;while(!done && std::chrono::steady_clock::now()<deadline){transport.Tick();std::this_thread::sleep_for(1ms);}assert(done);if(encoding.empty())assert(response.error.empty());else{const auto prefix="failed to decompress '"+encoding+"' encoding: ";// The native content decoder rejects inside the real curl write callback;
// retain that actual completion code instead of inventing a libcurl decoder code.
assert(response.error.starts_with(prefix)&&response.error.size()>prefix.size());assert(response.error_code==CURLE_WRITE_ERROR&&response.status==200);}assert(transport.size()==0);};wait();
  assert(response.method=="GET" && response.status==200 && received=="native!");size_t cookies=0;for(const auto& [name,value]:response.headers)if(name=="Set-Cookie")++cookies;assert(cookies==2);
  request={};request.url=origin+"/auth";request.method="GET";request.credentials=true;request.username="native";request.password="password";done=false;assert(transport.Start(2,std::move(request),data,finish,error));wait();assert(response.status==200 && received=="authenticated");
  int owner=10;for(const std::string encoding:{"gzip","br","deflate"}){request={};request.url=origin+"/corrupted/"+encoding;request.method="GET";done=false;assert(transport.Start(owner++,std::move(request),data,finish,error));wait(encoding);}
  server.join();close(listener);assert(served==6);
  request={};request.url="http://127.0.0.1:1/cancel";request.method="GET";bool canceled_callback=false;assert(transport.Start(3,std::move(request),data,[&](auto){canceled_callback=true;},error));transport.Cancel(3);transport.Tick();assert(!canceled_callback && transport.size()==0);
  const auto parsed=suitecut::ParseNativeHTTPCookie("native=quoted; Domain=.example.test; Path=/scope; HttpOnly; SameSite=Lax; Max-Age=3600; Expires=Thu, 01 Jan 1970 00:00:00 GMT","example.test","/original/path",1000);assert(parsed && parsed->domain=="example.test" && parsed->path=="/scope" && parsed->http_only && parsed->same_site==2 && parsed->expires==4600);
  assert(!suitecut::ParseNativeHTTPCookie("bad=x; Domain=foreign.test","example.test","/",1000));assert(!suitecut::ParseNativeHTTPCookie("__Host-bad=x; Secure; Domain=example.test","example.test","/",1000));
  const auto rounds=argc>1?std::strtoull(argv[1],nullptr,10):100000;std::mt19937 random(0x43524c);
  for(uint64_t round=0;round<rounds;++round){std::string raw;const auto count=random()%1024;for(unsigned i=0;i<count;++i)raw+=static_cast<char>(random()%256);const auto cookie=suitecut::ParseNativeHTTPCookie(raw,"example.test","/scope/page",1000);if(cookie){assert(!cookie->name.empty() && raw.size()<=4096);assert(cookie->path.starts_with('/'));assert(!cookie->expires || *cookie->expires<=253402300799.0);}}
  std::cout<<"Native curl: exact GET body, repeated headers, real auth, genuine corrupt gzip/br/deflate diagnostics, cancellation; cookie parser "<<rounds<<" seeded cases passed\n";
}
