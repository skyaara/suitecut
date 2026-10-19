// SPDX-License-Identifier: MIT
#include "accepted_engine_work_wake.h"
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <iostream>
#include <fcntl.h>
#include <mutex>
#include <sys/socket.h>
using Wake=suitecut::AcceptedEngineWorkWake;
struct Channel{
  int descriptors[2];Channel(){assert(socketpair(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0,descriptors)==0);}
  ~Channel(){for(const auto value:descriptors)if(value>=0)close(value);}
  void Send(char value){assert(send(descriptors[0],&value,1,MSG_NOSIGNAL)==1);}
  char Read(){char value=0;assert(recv(descriptors[1],&value,1,MSG_DONTWAIT)==1);return value;}
};
struct Posts{
  std::mutex mutex;std::condition_variable changed;std::deque<std::shared_ptr<Wake::Post>> values;size_t admissions=0;
  bool Post(std::shared_ptr<Wake::Post> value){std::lock_guard lock(mutex);values.push_back(std::move(value));++admissions;changed.notify_one();return true;}
  std::shared_ptr<Wake::Post> Take(){std::unique_lock lock(mutex);assert(changed.wait_for(lock,std::chrono::seconds(5),[&]{return !values.empty();}));auto value=std::move(values.front());values.pop_front();return value;}
};
int main(){size_t groups=0;
 {Channel channel;Posts posts;Wake wake(channel.descriptors[1],[&](auto value){return posts.Post(std::move(value));});channel.Send('a');auto token=posts.Take();assert(channel.Read()=='a'&&wake.PendingPosts()==1&&!wake.Failed());wake.Stop();assert(wake.PendingPosts()==1);token.reset();assert(!wake.PendingPosts());++groups;}
 {Channel channel;Posts posts;Wake wake(channel.descriptors[1],[&](auto value){return posts.Post(std::move(value));});channel.Send('a');channel.Send('b');auto first=posts.Take();assert(channel.Read()=='a'&&wake.PendingPosts()==1);{std::lock_guard lock(posts.mutex);assert(posts.admissions==1&&posts.values.empty());}first.reset();auto second=posts.Take();assert(channel.Read()=='b'&&wake.PendingPosts()==1);wake.Stop();second.reset();assert(!wake.PendingPosts());++groups;}
 {Channel channel;Posts posts;Wake wake(channel.descriptors[1],[&](auto value){return posts.Post(std::move(value));});auto coordinator=wake.RetainPost();assert(wake.PendingPosts()==1);channel.Send('c');coordinator.reset();auto token=posts.Take();assert(channel.Read()=='c'&&wake.PendingPosts()==1);wake.Stop();token.reset();assert(!wake.PendingPosts());++groups;}
 {Channel channel;Posts posts;Wake wake(channel.descriptors[1],[&](auto value){return posts.Post(std::move(value));});channel.Send('d');auto original=posts.Take();auto copied=original;assert(channel.Read()=='d');original.reset();assert(wake.PendingPosts()==1);wake.Stop();copied.reset();assert(!wake.PendingPosts());++groups;}
 {Channel channel;std::mutex mutex;std::condition_variable changed;bool entered=false;Wake wake(channel.descriptors[1],[&](auto){std::lock_guard lock(mutex);entered=true;changed.notify_one();return false;});channel.Send('e');{std::unique_lock lock(mutex);assert(changed.wait_for(lock,std::chrono::seconds(5),[&]{return entered;}));}wake.Stop();assert(wake.Failed()&&!wake.PendingPosts());++groups;}
 {Channel channel;std::mutex mutex;std::condition_variable changed;bool entered=false;Wake wake(channel.descriptors[1],[&](auto)->bool{{std::lock_guard lock(mutex);entered=true;changed.notify_one();}throw std::runtime_error("actual native poster unwound");});channel.Send('f');{std::unique_lock lock(mutex);assert(changed.wait_for(lock,std::chrono::seconds(5),[&]{return entered;}));}wake.Stop();assert(wake.Failed()&&!wake.PendingPosts());++groups;}
 {Channel channel;Posts posts;Wake wake(channel.descriptors[1],[&](auto value){return posts.Post(std::move(value));});close(channel.descriptors[0]);channel.descriptors[0]=-1;auto token=posts.Take();char byte=0;assert(recv(channel.descriptors[1],&byte,1,MSG_DONTWAIT)==0&&wake.PendingPosts()==1);wake.Stop();token.reset();assert(!wake.PendingPosts());++groups;}
 {Channel channel;Posts posts;Wake wake(channel.descriptors[1],[&](auto value){return posts.Post(std::move(value));});wake.Stop();wake.Stop();assert(!wake.Failed()&&!wake.PendingPosts());bool rejected=false;try{Wake invalid(-1,[](auto){return true;});}catch(const std::invalid_argument&){rejected=true;}assert(rejected&&fcntl(channel.descriptors[1],F_GETFD)>=0);++groups;}
 assert(groups==8);std::cout<<"Native Engine control watcher: "<<groups<<" real socket/eventfd/post-capture/kernel shutdown controls passed; no CEF idle assertion\n";
}
