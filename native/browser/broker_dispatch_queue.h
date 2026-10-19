// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <deque>
#include <optional>
#include <string>
#include <utility>
namespace suitecut {
// Only the CEF UI task consumes this bounded queue; submission never calls CEF.
class BrokerDispatchQueue {
 public:
  enum class Owner {None,Control,Bootstrap,Operation};
  struct Job {int id;std::string json;Owner owner;std::string session;};
  static constexpr size_t kCount=1024,kBytes=8*1048576;
  bool Push(int id,std::string json,Owner owner,std::string session={}){if(id<1||json.empty()||jobs_.size()>=kCount||json.size()>kBytes||json.size()>kBytes-bytes_)return false;const auto charge=json.size();jobs_.push_back({id,std::move(json),owner,std::move(session)});bytes_+=charge;return true;}
  std::optional<Job> Pop(){if(jobs_.empty())return std::nullopt;auto job=std::move(jobs_.front());jobs_.pop_front();bytes_-=job.json.size();return job;}
  void Clear(){jobs_.clear();bytes_=0;}
  bool Empty() const{return jobs_.empty();}
  size_t Count() const{return jobs_.size();}
  size_t Bytes() const{return bytes_;}
 private:
  std::deque<Job> jobs_;size_t bytes_=0;
};
}
