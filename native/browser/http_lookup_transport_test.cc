// SPDX-License-Identifier: MIT
#include "curl_transport.h"
#include <sys/socket.h>
#include <unistd.h>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
#include <array>
int main(){
  using namespace std::chrono_literals;using Transport=suitecut::NativeCurlTransport;
  const int first=socket(AF_INET,SOCK_STREAM,0),second=socket(AF_INET,SOCK_STREAM,0);assert(first>=0&&second>=0);
  sockaddr_in address{};address.sin_family=AF_INET;assert(inet_pton(AF_INET,"127.0.0.1",&address.sin_addr)==1);assert(bind(first,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0);socklen_t length=sizeof(address);assert(getsockname(first,reinterpret_cast<sockaddr*>(&address),&length)==0);const int port=ntohs(address.sin_port);
  assert(inet_pton(AF_INET,"127.0.0.2",&address.sin_addr)==1);assert(bind(second,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0);assert(listen(first,8)==0&&listen(second,8)==0);
  auto serve=[port](int listener,int count,const std::string&body){for(int round=0;round<count;++round){const int peer=accept(listener,nullptr,nullptr);assert(peer>=0);std::string request;char bytes[4096];while(request.find("\r\n\r\n")==std::string::npos){const auto received=recv(peer,bytes,sizeof(bytes),0);assert(received>0);request.append(bytes,received);}assert(request.find("Host: native-dns.invalid:"+std::to_string(port)+"\r\n")!=std::string::npos);const auto response="HTTP/1.1 200 OK\r\nContent-Length: "+std::to_string(body.size())+"\r\nConnection: close\r\n\r\n"+body;assert(send(peer,response.data(),response.size(),MSG_NOSIGNAL)==static_cast<ssize_t>(response.size()));close(peer);}};
  std::thread a(serve,first,2,"first"),b(serve,second,3,"second");
  Transport transport;std::array<std::string,5>bodies;std::array<bool,5>done{};
  for(int owner=0;owner<5;++owner){Transport::Request request;request.url="http://native-dns.invalid:"+std::to_string(port)+"/actual";request.method="GET";request.lookup=Transport::Request::Lookup{"native-dns.invalid",port,{{owner%2?"127.0.0.2":"127.0.0.1",4}}};if(owner==4)request.lookup->addresses={{"127.0.0.3",4},{"127.0.0.2",4}};std::string error;
    assert(transport.Start(owner+1,std::move(request),[&,owner](const void*bytes,size_t size){if(bytes)bodies[owner].append(static_cast<const char*>(bytes),size);return true;},[&,owner](auto response){assert(response.error.empty()&&response.status==200);assert(response.remote_ip==(owner%2||owner==4?"127.0.0.2":"127.0.0.1"));done[owner]=true;},error));
  }
  const auto deadline=std::chrono::steady_clock::now()+5s;while(transport.size()&&std::chrono::steady_clock::now()<deadline){transport.Tick();std::this_thread::sleep_for(1ms);}assert(transport.size()==0);
  for(int i=0;i<5;++i){assert(done[i]);assert(bodies[i]==(i%2||i==4?"second":"first"));}
  a.join();b.join();close(first);close(second);
  std::cout<<"Native HTTP custom DNS: five concurrent same-authority requests retain real Host, isolated destinations, actual socket address and multi-IP fallback\n";
}
