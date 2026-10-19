// SPDX-License-Identifier: MIT
#include "curl_transport.h"
#include "http_transport_error.h"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
int main(){
  using namespace suitecut;
  const int listener=socket(AF_INET,SOCK_STREAM,0);assert(listener>=0);
  sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
  assert(bind(listener,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0&&listen(listener,4)==0);
  socklen_t length=sizeof(address);assert(getsockname(listener,reinterpret_cast<sockaddr*>(&address),&length)==0);
  std::thread origin([&]{
    for(int round=0;round<4;++round){
      const int peer=accept(listener,nullptr,nullptr);assert(peer>=0);
      std::string request;char buffer[4096];
      while(request.find("\r\n\r\n")==std::string::npos){const auto count=recv(peer,buffer,sizeof(buffer),0);assert(count>0);request.append(buffer,size_t(count));assert(request.size()<16384);}
      if(round==0){linger reset{1,0};assert(setsockopt(peer,SOL_SOCKET,SO_LINGER,&reset,sizeof(reset))==0);}
      else if(round==2){const std::string response="HTTP/1.1 200 OK\r\nContent-Length: 10\r\nConnection: close\r\n\r\nECONNRESET";assert(send(peer,response.data(),response.size(),MSG_NOSIGNAL)==ssize_t(response.size()));}
      else if(round==3){const std::string response="HTTP/1.1 200 OK\r\nContent-Length: 10\r\nConnection: close\r\n\r\nXY";assert(send(peer,response.data(),response.size(),MSG_NOSIGNAL)==ssize_t(response.size()));}
      assert(close(peer)==0);
    }
  });
  NativeCurlTransport transport;const auto url="http://127.0.0.1:"+std::to_string(ntohs(address.sin_port));
  for(int round=0;round<4;++round){
    NativeCurlTransport::Request request;request.url=url;request.method="GET";
    bool completed=false;NativeCurlTransport::Response response;std::string error,body;
    assert(transport.Start(round+1,std::move(request),[&](const void*data,size_t count){if(data)body.append(static_cast<const char*>(data),count);return body.size()<16384;},[&](auto result){response=std::move(result);completed=true;},error));
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!completed&&std::chrono::steady_clock::now()<deadline){transport.Tick();std::this_thread::yield();}
    assert(completed&&transport.size()==0);
    const auto actual=HTTPTransportErrorObservation(response.error_code,response.os_errno);
    if(round==0){assert(response.error_code==CURLE_RECV_ERROR&&response.os_errno==ECONNRESET);assert(actual&&actual->code=="ECONNRESET"&&actual->curl_code==int(response.error_code)&&actual->os_errno==response.os_errno);}
    else if(round==1){assert(response.error_code==CURLE_GOT_NOTHING&&response.os_errno==0);assert(actual&&actual->code.empty()&&actual->os_errno==0);}
    else if(round==2){assert(response.status==200&&response.error.empty()&&response.error_code==CURLE_OK&&body=="ECONNRESET");assert(!actual);}
    else {assert(response.status==200&&response.error_code==CURLE_PARTIAL_FILE&&response.os_errno==0&&body=="XY");assert(actual&&actual->curl_code==int(CURLE_PARTIAL_FILE)&&actual->os_errno==0&&actual->code.empty());}
  }
  origin.join();assert(close(listener)==0);
  const auto stale=HTTPTransportErrorObservation(CURLE_PEER_FAILED_VERIFICATION,ECONNREFUSED);assert(stale&&stale->os_errno==ECONNREFUSED&&stale->code.empty());
  const auto partial_stale=HTTPTransportErrorObservation(CURLE_PARTIAL_FILE,ECONNRESET);assert(partial_stale&&partial_stale->os_errno==ECONNRESET&&partial_stale->code.empty());
  const auto absent=HTTPTransportErrorObservation(CURLE_RECV_ERROR,0);assert(absent&&absent->code.empty());
  assert(!HTTPTransportErrorObservation(CURLE_OK,ECONNRESET));
  assert(!HTTPTransportErrorObservation(CURL_LAST,0));
  assert(!HTTPTransportErrorObservation(CURLE_RECV_ERROR,-1));
  std::cout<<"Real TCP reset, orderly EOF, actual truncated body without invented reset, origin text nonforgery, observed errno and stale TLS errno attribution passed\n";
}
