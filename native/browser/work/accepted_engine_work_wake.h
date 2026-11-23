// SPDX-License-Identifier: MIT
#pragma once
#if defined(__linux__)
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <functional>
#include <memory>
#include <poll.h>
#include <stdexcept>
#include <sys/eventfd.h>
#include <thread>
#include <unistd.h>
namespace suitecut {
// The native process control socket is a real descriptor. A blocking kernel
// watcher posts only on actual readability, never runs an author callback or
// interprets work/idle on another thread. All messages and readiness remain UI.
class AcceptedEngineWorkWake {
 private:
  struct State {
    std::atomic<bool> stop{false},failed{false};std::atomic_size_t posted{0};
    int event=-1;
    State(){event=eventfd(0,EFD_CLOEXEC|EFD_NONBLOCK);if(event<0)throw std::runtime_error("Native accepted-work eventfd creation failed");}
    ~State(){if(event>=0)close(event);}
    void Wake()noexcept{uint64_t one=1;ssize_t result;do{result=write(event,&one,sizeof(one));}while(result<0&&errno==EINTR);if(result!=static_cast<ssize_t>(sizeof(one))&&!(result<0&&errno==EAGAIN))failed=true;}
  };
 public:
  class Post {
   public:
    explicit Post(std::shared_ptr<State> state):state_(std::move(state)){state_->posted.fetch_add(1);}
    ~Post(){state_->posted.fetch_sub(1);state_->Wake();}
    Post(const Post&)=delete;Post& operator=(const Post&)=delete;
   private:std::shared_ptr<State> state_;
  };
  using Poster=std::function<bool(std::shared_ptr<Post>)>;
  AcceptedEngineWorkWake(int actual_descriptor,Poster actual_post):state_(std::make_shared<State>()){
    if(actual_descriptor<3||!actual_post)throw std::invalid_argument("Native accepted-work watcher requires actual endpoint/poster");
    const auto state=state_;
    thread_=std::thread([state,actual_descriptor,post=std::move(actual_post)]{
      try{
        while(!state->stop.load()){
          pollfd descriptors[2]{{state->posted.load()?-1:actual_descriptor,POLLIN,0},{state->event,POLLIN,0}};
          int result;do{result=poll(descriptors,2,-1);}while(result<0&&errno==EINTR);
          if(result<0||descriptors[0].revents&(POLLNVAL|POLLERR)||descriptors[1].revents&(POLLNVAL|POLLERR)){state->failed=true;break;}
          if(descriptors[1].revents&POLLIN){uint64_t value;while(read(state->event,&value,sizeof(value))<0&&errno==EINTR){}}
          if(state->stop.load())break;
          if(descriptors[0].revents&(POLLIN|POLLHUP)){
            auto token=std::make_shared<Post>(state);
            if(!post(std::move(token))){state->failed=true;break;}
          }
        }
      }catch(...){state->failed=true;} // Observed by UI; never grants readiness.
    });
  }
  ~AcceptedEngineWorkWake(){Stop();}
  AcceptedEngineWorkWake(const AcceptedEngineWorkWake&)=delete;AcceptedEngineWorkWake& operator=(const AcceptedEngineWorkWake&)=delete;
  bool Failed()const noexcept{return state_->failed.load();}
  size_t PendingPosts()const noexcept{return state_->posted.load();}
  std::shared_ptr<Post> RetainPost(){return std::make_shared<Post>(state_);}
  void Stop()noexcept{state_->stop=true;state_->Wake();if(thread_.joinable())thread_.join();}
 private:
  std::shared_ptr<State> state_;std::thread thread_;
};
} // namespace suitecut
#endif
