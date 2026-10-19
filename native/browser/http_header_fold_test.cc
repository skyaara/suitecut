// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "curl_transport.h"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>
#include <zlib.h>
using Bytes=std::vector<unsigned char>;
Bytes Gzip(){const std::string input="hello";z_stream z{};assert(deflateInit2(&z,Z_DEFAULT_COMPRESSION,Z_DEFLATED,MAX_WBITS+16,8,Z_DEFAULT_STRATEGY)==Z_OK);Bytes result(100);z.next_in=reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));z.avail_in=input.size();z.next_out=result.data();z.avail_out=result.size();assert(deflate(&z,Z_FINISH)==Z_STREAM_END);result.resize(z.total_out);deflateEnd(&z);return result;}
void Write(int peer,const void*data,size_t size){size_t offset=0;while(offset<size){const auto n=send(peer,static_cast<const char*>(data)+offset,size-offset,MSG_NOSIGNAL);assert(n>0);offset+=size_t(n);}}
int main(){
 const auto gzip=Gzip();const int listener=socket(AF_INET,SOCK_STREAM,0);assert(listener>=0);sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);assert(bind(listener,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0&&listen(listener,4)==0);socklen_t size=sizeof(address);assert(getsockname(listener,reinterpret_cast<sockaddr*>(&address),&size)==0);
 std::mutex mutex;std::condition_variable received;bool first_body=false;
 std::thread origin([&]{for(int round=0;round<4;++round){const int peer=accept(listener,nullptr,nullptr);assert(peer>=0);std::string request;char bytes[4096];while(request.find("\r\n\r\n")==std::string::npos){const auto n=recv(peer,bytes,sizeof(bytes),0);assert(n>0);request.append(bytes,size_t(n));assert(request.size()<16384);}
  std::string header="HTTP/1.1 200 OK\r\n";
  if(round==0)header+="X-Trace: start\r\n Set-Cookie: forged=yes\r\n Content-Encoding: gzip\r\n Content-Length: 1\r\n";
  if(round==1)header+="Content-Encoding:\r\n gzip\r\nContent-Length: "+std::to_string(gzip.size())+"\r\n";
  if(round==2)header+="Set-Cookie: real=yes;\r\n Path=/;\r\n HttpOnly\r\nContent-Length: 5\r\n";
  if(round==3){header+="X-Long: start\r\n";for(int line=0;line<1024;++line)header+=" a\r\n";}
  header+="Connection: close\r\n\r\n";Write(peer,header.data(),header.size());
  if(round==0){Write(peer,"h",1);std::unique_lock lock(mutex);assert(received.wait_for(lock,std::chrono::seconds(5),[&]{return first_body;}));lock.unlock();Write(peer,"ello",4);}
  else if(round==1)Write(peer,gzip.data(),gzip.size());else if(round==2)Write(peer,"hello",5);
  close(peer);
 }});
 suitecut::NativeCurlTransport transport;
 for(int round=0;round<4;++round){suitecut::NativeCurlTransport::Request request;request.url="http://127.0.0.1:"+std::to_string(ntohs(address.sin_port))+"/";request.method="GET";bool done=false;int callbacks=0;std::string error,body;suitecut::NativeCurlTransport::Response response;
  assert(transport.Start(round+1,std::move(request),[&](const void*data,size_t length){if(!data){body.clear();return true;}++callbacks;body.append(static_cast<const char*>(data),length);if(round==0&&body.size()==1){std::lock_guard lock(mutex);first_body=true;received.notify_one();}return true;},[&](auto result){response=std::move(result);done=true;},error));
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);while(!done&&std::chrono::steady_clock::now()<deadline){transport.Tick();std::this_thread::yield();}
  if(round==3){assert(done&&response.error=="Native HTTP response header count exceeded"&&response.error_code==CURLE_WRITE_ERROR&&response.set_cookies.empty()&&transport.size()==0);continue;}
  if(!done||!response.error.empty()||body!="hello"){std::cerr<<round<<" error="<<response.error<<" body="<<body<<'\n';assert(false);}assert(response.error_code==CURLE_OK&&transport.size()==0);
  if(round==0){assert(callbacks>=2&&response.set_cookies.empty());assert(response.headers[0].first=="X-Trace"&&response.headers[0].second=="start Set-Cookie: forged=yes Content-Encoding: gzip Content-Length: 1");}
  if(round==1){assert(response.set_cookies.empty());assert(response.headers[0].first=="Content-Encoding"&&response.headers[0].second=="gzip");}
  if(round==2){assert(response.set_cookies.size()==1&&response.set_cookies[0]=="real=yes; Path=/; HttpOnly");}
 }
 origin.join();assert(close(listener)==0);std::cout<<"PASS4 actual folded fields: no invented cookie/coding/length, real first-body receipt barrier, folded genuine gzip and Set-Cookie complete values, physical continuation-line admission bound\n";
}
