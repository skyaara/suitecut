// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "curl_transport.h"
#include "http_content_decoder.h"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>
using Bytes=std::vector<unsigned char>;
Bytes Encode(const Bytes&input,int bits){z_stream z{};assert(deflateInit2(&z,Z_DEFAULT_COMPRESSION,Z_DEFLATED,bits,8,Z_DEFAULT_STRATEGY)==Z_OK);Bytes output(compressBound(input.size())+64);z.next_in=const_cast<unsigned char*>(input.data());z.avail_in=input.size();z.next_out=output.data();z.avail_out=output.size();assert(deflate(&z,Z_FINISH)==Z_STREAM_END);output.resize(z.total_out);deflateEnd(&z);return output;}
void Write(int peer,const void*data,size_t size){size_t offset=0;while(offset<size){const auto count=send(peer,static_cast<const unsigned char*>(data)+offset,size-offset,MSG_NOSIGNAL);assert(count>0);offset+=size_t(count);}}
struct Case{std::string name,method,headers;Bytes body;std::string expected;bool good=true;int curl=CURLE_OK;bool cancel=false;};
int main(){
 using namespace suitecut;const Bytes hello{'h','e','l','l','o'};const auto gzip=Encode(hello,MAX_WBITS+16);
 auto truncated=gzip;truncated.resize(truncated.size()-8);
 const Bytes brotli{0x0b,0x02,0x80,'h','e','l','l','o',0x03};
 const auto content=[](std::string coding,const Bytes&body){return "Content-Encoding: "+coding+"\r\nContent-Length: "+std::to_string(body.size())+"\r\n";};
 const auto chunk=[](const Bytes&bytes){const char*hex="0123456789abcdef";std::string digits;size_t n=bytes.size();do{digits.insert(digits.begin(),hex[n%16]);n/=16;}while(n);Bytes result(digits.begin(),digits.end());result.insert(result.end(),{'\r','\n'});result.insert(result.end(),bytes.begin(),bytes.end());const std::string end="\r\n0\r\n\r\n";result.insert(result.end(),end.begin(),end.end());return result;};
 std::vector<Case>cases{
  {"gzip","GET",content("gzip",gzip),gzip,"hello"},
  {"authentication empty encoded challenge","GET",content("gzip",gzip),gzip,"hello"},
  {"brotli","GET",content("br",brotli),brotli,"hello"},
  {"wrapped deflate","GET",content("deflate",Encode(hello,MAX_WBITS)),Encode(hello,MAX_WBITS),"hello"},
  {"raw deflate","GET",content("deflate",Encode(hello,-MAX_WBITS)),Encode(hello,-MAX_WBITS),"hello"},
  {"footer incomplete","GET",content("gzip",truncated),truncated,"hello",false,CURLE_WRITE_ERROR},
  {"Brotli incomplete","GET",content("br",Bytes{0x0b}),Bytes{0x0b},"",false,CURLE_WRITE_ERROR},
  {"HTTP premature EOF","GET","Content-Encoding: gzip\r\nContent-Length: 100\r\n",Bytes(gzip.begin(),gzip.begin()+10),"",false,CURLE_PARTIAL_FILE},
  {"HEAD body metadata","HEAD","Content-Encoding: gzip\r\nContent-Length: 256\r\n",{},""},
  {"304 body metadata","GET","Content-Encoding: br\r\nContent-Length: 256\r\n",{},""},
  {"encoded empty","GET",content("gzip",Encode({},MAX_WBITS+16)),Encode({},MAX_WBITS+16),""},
  {"chunked gzip","GET","Content-Encoding: gzip\r\nTransfer-Encoding: chunked\r\n",chunk(gzip),"hello"},
  {"chunked gzip missing footer","GET","Content-Encoding: gzip\r\nTransfer-Encoding: chunked\r\n",chunk(truncated),"hello",false,CURLE_OK},
  {"physical cancel","GET","Content-Encoding: gzip\r\nTransfer-Encoding: chunked\r\n",Bytes{'2','\r','\n',0x1f,0x8b,'\r','\n'},"",false,CURLE_OK,true},
 };
 const int listener=socket(AF_INET,SOCK_STREAM,0);assert(listener>=0);sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
 assert(bind(listener,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0&&listen(listener,4)==0);socklen_t size=sizeof(address);assert(getsockname(listener,reinterpret_cast<sockaddr*>(&address),&size)==0);
 std::atomic<bool>cancel_peer_closed=false;
 std::thread origin([&]{for(const auto&item:cases){int peer=accept(listener,nullptr,nullptr);assert(peer>=0);std::string request;char bytes[4096];while(request.find("\r\n\r\n")==std::string::npos){const auto n=recv(peer,bytes,sizeof(bytes),0);assert(n>0);request.append(bytes,size_t(n));assert(request.size()<16384);}assert(request.starts_with(item.method+" /"));
  if(item.name=="authentication empty encoded challenge"){
    assert(request.find("Authorization:")==std::string::npos);
    const std::string challenge="HTTP/1.1 401 Unauthorized\r\nWWW-Authenticate: Basic realm=\"native\"\r\nContent-Encoding: gzip\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    Write(peer,challenge.data(),challenge.size());close(peer);peer=accept(listener,nullptr,nullptr);assert(peer>=0);request.clear();
    while(request.find("\r\n\r\n")==std::string::npos){const auto n=recv(peer,bytes,sizeof(bytes),0);assert(n>0);request.append(bytes,size_t(n));assert(request.size()<16384);}
    assert(request.find("Authorization: Basic bmF0aXZlOnBhc3M=")!=std::string::npos);
  }

  const std::string header=std::string("HTTP/1.1 ")+(item.name=="304 body metadata"?"304 Not Modified":"200 OK")+"\r\n"+item.headers+"Connection: close\r\n\r\n";Write(peer,header.data(),header.size());Write(peer,item.body.data(),item.body.size());
  if(item.cancel){const auto n=recv(peer,bytes,sizeof(bytes),0);assert(n==0||(n<0&&errno==ECONNRESET));cancel_peer_closed=true;}
  close(peer);
 }});
 NativeCurlTransport transport;int owner=0;
 for(const auto&item:cases){NativeCurlTransport::Request request;request.url="http://127.0.0.1:"+std::to_string(ntohs(address.sin_port))+"/";request.method=item.method;if(item.name=="authentication empty encoded challenge"){request.credentials=true;request.username="native";request.password="pass";}bool done=false;std::string error,body;NativeCurlTransport::Response response;
  assert(transport.Start(++owner,std::move(request),[&](const void*bytes,size_t length){if(!bytes)body.clear();else body.append(static_cast<const char*>(bytes),length);return true;},[&](auto result){response=std::move(result);done=true;assert(NativeHTTPContentDecoder::GlobalBytes()==0);},error));
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
  while(!done&&std::chrono::steady_clock::now()<deadline){transport.Tick();if(item.cancel&&NativeHTTPContentDecoder::GlobalBytes()>0)break;std::this_thread::yield();}
  if(item.cancel){assert(!done&&transport.size()==1&&NativeHTTPContentDecoder::GlobalBytes()>0);transport.Cancel(owner);assert(!done&&transport.size()==0&&NativeHTTPContentDecoder::GlobalBytes()==0);continue;}
  if(!done||response.error_code!=item.curl||response.error.empty()!=item.good||body!=item.expected){std::cerr<<item.name<<" code="<<response.error_code<<" error="<<response.error<<" bytes="<<body.size()<<"\n";assert(false);}
  assert(transport.size()==0&&NativeHTTPContentDecoder::GlobalBytes()==0);if(!item.good)assert(response.os_errno==0);
 }
 origin.join();assert(cancel_peer_closed&&close(listener)==0);
 std::cout<<"PASS14 real sockets: gzip/br/wrapped+raw deflate, real auth retry after discarded encoded challenge, gzip footer and Brotli EOF, actual premature HTTP EOF, HEAD/304/empty, transfer decoding, actual curl code preservation, physical cancel before codec release\n";
}
