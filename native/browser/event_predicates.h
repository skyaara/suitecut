// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// CEF-free ownership and buffered candidate state. Timers and the actual event
// journal remain with Source's native scheduler.
class SuiteCutEventPredicateController {
 public:
  static constexpr std::size_t kCapacity=64, kMaxPayload=1048576, kPendingBytes=4*1048576;
  // A genuine 2MiB WebSocket frame expands to base64 in its predicate snapshot.
  // Other event callers keep the original1MiB bound; total ownership stays4MiB.
  static constexpr std::size_t kWebSocketPayload=3*1048576;
  static constexpr std::uint64_t kMaxWireInteger=9007199254740991ULL;
  struct Scope { int owner; std::uint64_t generation; };
  struct Candidate { std::uint64_t token,sequence; std::string client_token,event,json; };
  enum class Status { Missing, Waiting, CandidateReady, PayloadCapacity };
  enum class Decision { Stale, Retry, Matched };
  bool Start(int owner,std::uint64_t generation,std::string client_token,std::string event,std::uint64_t cursor,std::size_t max_payload=kMaxPayload) {
    if(owner<1||generation<1||generation>kMaxWireInteger||cursor>kMaxWireInteger||client_token.empty()||client_token.size()>128||event.empty()||event.size()>64||!max_payload||max_payload>kWebSocketPayload||entries_.size()>=kCapacity||entries_.contains(owner))return false;
    entries_.emplace(owner,Entry{generation,cursor,std::move(client_token),std::move(event),max_payload,std::nullopt});return true;
  }
  Status Offer(int owner,std::uint64_t generation,std::uint64_t sequence,const std::string& event,const std::string& json) {
    auto found=entries_.find(owner);
    if(found==entries_.end()||found->second.generation!=generation)return Status::Missing;
    auto& entry=found->second;
    if(entry.pending||sequence<=entry.cursor)return Status::Waiting;
    if(sequence>kMaxWireInteger)return Status::PayloadCapacity;
    if(event!=entry.event){entry.cursor=sequence;return Status::Waiting;}
    if(json.empty()||json.size()>entry.max_payload||pending_bytes_+json.size()>kPendingBytes||next_token_>=kMaxWireInteger)return Status::PayloadCapacity;
    entry.cursor=sequence;entry.pending=Candidate{++next_token_,sequence,entry.client_token,event,json};pending_bytes_+=json.size();return Status::CandidateReady;
  }
  Decision Answer(int owner,std::uint64_t generation,std::uint64_t token,const std::string& client_token,bool matches,std::string& matched_json) {
    auto found=entries_.find(owner);
    if(found==entries_.end()||found->second.generation!=generation||found->second.client_token!=client_token||!found->second.pending||found->second.pending->token!=token)return Decision::Stale;
    auto& pending=found->second.pending;
    pending_bytes_-=pending->json.size();
    if(matches){matched_json=std::move(pending->json);entries_.erase(found);return Decision::Matched;}
    pending.reset();return Decision::Retry;
  }
  const Candidate* Pending(int owner,std::uint64_t generation) const {
    auto found=entries_.find(owner);if(found==entries_.end()||found->second.generation!=generation||!found->second.pending)return nullptr;return &*found->second.pending;
  }
  std::optional<std::uint64_t> Cursor(int owner,std::uint64_t generation) const {
    auto found=entries_.find(owner);if(found==entries_.end()||found->second.generation!=generation)return std::nullopt;return found->second.cursor;
  }
  std::vector<Scope> Active() const {std::vector<Scope> out;out.reserve(entries_.size());for(const auto& [id,entry]:entries_)out.push_back({id,entry.generation});return out;}
  void Cancel(int owner){auto found=entries_.find(owner);if(found==entries_.end())return;if(found->second.pending)pending_bytes_-=found->second.pending->json.size();entries_.erase(found);}
  void Clear(){entries_.clear();pending_bytes_=0;}
  std::size_t Size() const{return entries_.size();}
  std::size_t PendingBytes() const{return pending_bytes_;}
 private:
  struct Entry {std::uint64_t generation,cursor;std::string client_token,event;std::size_t max_payload;std::optional<Candidate> pending;};
  std::map<int,Entry> entries_;
  std::uint64_t next_token_=0;
  std::size_t pending_bytes_=0;
};
