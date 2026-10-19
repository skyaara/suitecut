// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "curl_transport.h"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cerrno>
#include <string>
#include <thread>

int main() {
  using namespace std::chrono_literals;
  const int listener=socket(AF_INET,SOCK_STREAM,0);assert(listener>=0);
  sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
  assert(bind(listener,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0);assert(listen(listener,1)==0);
  socklen_t length=sizeof(address);assert(getsockname(listener,reinterpret_cast<sockaddr*>(&address),&length)==0);
  std::atomic<bool> inspect_idle=false,inspected=false,closed=false;
  std::thread server([&] {
    const int peer=accept(listener,nullptr,nullptr);assert(peer>=0);
    timeval timeout{5,0};assert(setsockopt(peer,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout))==0);
    std::string request;char bytes[4096];
    while(request.find("\r\n\r\n")==std::string::npos){const auto count=recv(peer,bytes,sizeof(bytes),0);assert(count>0);request.append(bytes,static_cast<size_t>(count));}
    assert(request.starts_with("GET /pool HTTP/1.1"));
    const std::string reply="HTTP/1.1 200 OK\r\nContent-Length: 4\r\nConnection: keep-alive\r\n\r\nidle";
    size_t written=0;while(written<reply.size()){const auto count=send(peer,reply.data()+written,reply.size()-written,MSG_NOSIGNAL);assert(count>0);written+=static_cast<size_t>(count);}
    const auto deadline=std::chrono::steady_clock::now()+5s;
    while(!inspect_idle && std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(1ms);
    assert(inspect_idle);
    assert(recv(peer,bytes,sizeof(bytes),MSG_DONTWAIT)==-1 && (errno==EAGAIN || errno==EWOULDBLOCK));
    inspected=true;
    assert(recv(peer,bytes,sizeof(bytes),0)==0);closed=true;close(peer);
  });
  suitecut::NativeCurlTransport transport;
  suitecut::NativeCurlTransport::Request request;request.url="http://127.0.0.1:"+std::to_string(ntohs(address.sin_port))+"/pool";request.method="GET";
  bool done=false;std::string body,error;
  assert(transport.Start(1,std::move(request),[&](const void* bytes,size_t count){if(bytes)body.append(static_cast<const char*>(bytes),count);return true;},[&](auto response){assert(response.error.empty() && response.status==200);done=true;},error));
  const auto deadline=std::chrono::steady_clock::now()+5s;
  while(!done && std::chrono::steady_clock::now()<deadline){transport.Tick();std::this_thread::sleep_for(1ms);}
  assert(done && body=="idle" && transport.size()==0);inspect_idle=true;
  while(!inspected && std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(1ms);
  assert(inspected && !closed);transport.ResetIdle();server.join();assert(closed);close(listener);
}
