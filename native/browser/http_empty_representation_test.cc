// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "curl_transport.h"
#include "http_content_decoder.h"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cassert>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
namespace {
void Write(int peer,const std::string&text){size_t offset=0;while(offset<text.size()){const auto n=send(peer,text.data()+offset,text.size()-offset,MSG_NOSIGNAL);assert(n>0);offset+=size_t(n);}}
struct Case{std::string method,encoding,headers,body;bool good=true;CURLcode result=CURLE_OK;};
}
int main(){
 using namespace suitecut;std::vector<Case>cases;
 for(const auto&encoding:{"br","gzip","deflate"}){
  cases.push_back({"HEAD",encoding,"Content-Length: 256\r\n",""});
  cases.push_back({"PUT",encoding,"Content-Length: 0\r\n",""});
  cases.push_back({"PUT",encoding,"Transfer-Encoding: chunked\r\n","0\r\n\r\n"});
  cases.push_back({"PUT",encoding,"",""});
 }
 cases.push_back({"PUT","br","Content-Length: 1\r\n","",false,CURLE_PARTIAL_FILE});
 cases.push_back({"PUT","gzip","Transfer-Encoding: chunked\r\n","",false,CURLE_PARTIAL_FILE});
 cases.push_back({"PUT","br","Content-Length: 1\r\n",std::string(1,'\x0b'),false,CURLE_WRITE_ERROR});
 cases.push_back({"PUT","gzip","Content-Length: 2\r\n",std::string("\x1f\x8b",2),false,CURLE_WRITE_ERROR});
 cases.push_back({"PUT","deflate","Content-Length: 1\r\n",std::string(1,'\x78'),false,CURLE_WRITE_ERROR});
 cases.push_back({"PUT","unknown-native-coding","Content-Length: 0\r\n","",false,CURLE_OK});
 const int listener=socket(AF_INET,SOCK_STREAM,0);assert(listener>=0);sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);assert(bind(listener,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0&&listen(listener,4)==0);socklen_t size=sizeof(address);assert(getsockname(listener,reinterpret_cast<sockaddr*>(&address),&size)==0);
 std::thread origin([&]{for(const auto&item:cases){const int peer=accept(listener,nullptr,nullptr);assert(peer>=0);std::string request;char bytes[4096];while(request.find("\r\n\r\n")==std::string::npos){const auto n=recv(peer,bytes,sizeof(bytes),0);assert(n>0);request.append(bytes,size_t(n));assert(request.size()<16384);}assert(request.starts_with(item.method+" /"));Write(peer,"HTTP/1.1 200 OK\r\nContent-Encoding: "+item.encoding+"\r\n"+item.headers+"Connection: close\r\n\r\n"+item.body);close(peer);}});
 NativeCurlTransport transport;int owner=0;
 for(const auto&item:cases){NativeCurlTransport::Request request;request.url="http://127.0.0.1:"+std::to_string(ntohs(address.sin_port))+"/";request.method=item.method;bool done=false;std::string error,body;NativeCurlTransport::Response response;
  assert(transport.Start(++owner,std::move(request),[&](const void*bytes,size_t length){if(!bytes)body.clear();else body.append(static_cast<const char*>(bytes),length);return true;},[&](auto result){response=std::move(result);done=true;},error));const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);while(!done&&std::chrono::steady_clock::now()<deadline){transport.Tick();std::this_thread::yield();}
  if(!done||response.error_code!=item.result||response.error.empty()!=item.good||!body.empty()){std::cerr<<item.method<<" "<<item.encoding<<" "<<item.headers<<" code="<<response.error_code<<" error="<<response.error<<" body="<<body.size()<<"\n";assert(false);}assert(response.status==200&&response.os_errno==0&&transport.size()==0&&NativeHTTPContentDecoder::GlobalBytes()==0);
 }
 origin.join();assert(close(listener)==0);std::string output;NativeHTTPContentDecoder strict([&](auto data){output.append(reinterpret_cast<const char*>(data.data()),data.size());return true;});assert(strict.Configure("br")&&!strict.Finish());
 std::cout<<"PASS18 genuine HTTP sockets: HEAD+PUT supported coding, CL0/chunked0/close-delimited0, actual framing18, nonempty truncation23 and unknowncoding exclusions; strict empty decoder unchanged\n";
}
