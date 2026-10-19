// SPDX-License-Identifier: MIT
#pragma once
#include <coroutine>
#include <exception>
#include <optional>
#include <stdexcept>
#include <utility>
namespace suitecut::runner {
// Single-threaded native continuations. Only an actual IO/deadline yield returns
// control to the coordinator; callback promises never schedule native work.
struct NativeContinuation {
  std::coroutine_handle<> ready{};double wake=0;
  struct Wait {
    NativeContinuation& context;double at;
    bool await_ready()const noexcept{return false;}
    void await_suspend(std::coroutine_handle<> handle)const noexcept{context.ready=handle;context.wake=at;}
    void await_resume()const noexcept{}
  };
  Wait Yield(double at){return {*this,at};}
};
namespace continuation_detail {
template<class T>struct Value {std::optional<T> value;void return_value(T result){value=std::move(result);}T Take(){return std::move(*value);}};
template<>struct Value<void> {void return_void(){}void Take(){}};
}
template<class T=void>class NativeTask {
 public:
  struct promise_type:continuation_detail::Value<T> {
    std::exception_ptr error;std::coroutine_handle<> continuation{};
    NativeTask get_return_object(){return NativeTask(std::coroutine_handle<promise_type>::from_promise(*this));}
    std::suspend_always initial_suspend()const noexcept{return {};}
    struct Final {
      bool await_ready()const noexcept{return false;}
      std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> handle)const noexcept{return handle.promise().continuation?handle.promise().continuation:std::noop_coroutine();}
      void await_resume()const noexcept{}
    };
    Final final_suspend()const noexcept{return {};}
    void unhandled_exception(){error=std::current_exception();}
  };
  using Handle=std::coroutine_handle<promise_type>;
  explicit NativeTask(Handle handle):handle_(handle){}
  NativeTask(NativeTask&& other)noexcept:handle_(std::exchange(other.handle_,{})){}
  NativeTask(const NativeTask&)=delete;NativeTask& operator=(const NativeTask&)=delete;
  ~NativeTask(){if(handle_)handle_.destroy();}
  bool Done()const{return !handle_||handle_.done();}
  T Result(){if(!handle_||!handle_.done())throw std::logic_error("Native continuation has no actual completion");if(handle_.promise().error)std::rethrow_exception(handle_.promise().error);return handle_.promise().Take();}
  void Resume(NativeContinuation& context){if(Done())throw std::logic_error("Native completed continuation cannot resume");const auto next=context.ready?std::exchange(context.ready,{}):std::coroutine_handle<>(handle_);next.resume();}
  struct Awaiter {
    Handle handle;
    bool await_ready()const noexcept{return false;}
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> parent)const noexcept{handle.promise().continuation=parent;return handle;}
    T await_resume(){if(handle.promise().error)std::rethrow_exception(handle.promise().error);return handle.promise().Take();}
  };
  Awaiter operator co_await()const{return {handle_};}
 private:Handle handle_;
};
}
