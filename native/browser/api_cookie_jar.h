// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace suitecut {
// Authoritative cookies for independent native API contexts. This is not a
// metadata projection of Chromium's jar. Associated browser APIs keep that jar.
class NativeAPICookieJar {
 public:
  struct Cookie {
    std::string name,value,domain,path;
    double expires=-1;
    bool secure=false,http_only=false;
    int same_site=2; // None=1, Lax=2, Strict=3.
    std::optional<std::string> partition_key;
    bool partition_cross_site=true;
    uint64_t identity=0; // Native-only exact snapshot identity, never public.
  };
  struct Input {
    std::string name,value;
    std::optional<std::string> url,domain,path;
    std::optional<double> expires;
    std::optional<bool> secure;
    bool http_only=false;
    int same_site=2;
    std::optional<std::string> partition_key;
    bool partition_cross_site=true;
  };
  struct Clock {
    std::function<double()> monotonic;
    double wall,deadline;
    bool canceled=false;
  };
  struct Limits {
    size_t entries=4096,allocated_bytes=1048576,header_bytes=65536;
  };
  class Budget {
   public:
    explicit Budget(size_t limit=16*1048576,
                    std::pmr::memory_resource* upstream=std::pmr::get_default_resource());
    ~Budget();
    size_t Bytes() const;
    size_t Peak() const;
   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    friend class NativeAPICookieJar;
  };
  explicit NativeAPICookieJar(std::shared_ptr<Budget> budget);
  NativeAPICookieJar(std::shared_ptr<Budget> budget,Limits limits);
  ~NativeAPICookieJar();
  NativeAPICookieJar(const NativeAPICookieJar&)=delete;
  NativeAPICookieJar& operator=(const NativeAPICookieJar&)=delete;
  // Mutations are transactional, including input validation and native
  // allocator admission. Expired entries may be reclaimed during any access.
  bool Import(std::span<const Input> input,const Clock& clock,std::string& error);
  bool Reset(std::span<const Input> input,const Clock& clock,std::string& error);
  bool ValidateReplacement(std::span<const Input> input,const Clock& clock,std::string& error);
  bool Receive(const std::string& url,std::span<const std::string> headers,
               const Clock& clock,std::string& error);
  bool Cookies(std::span<const std::string> urls,const Clock& clock,
               std::vector<Cookie>& output,std::string& error);
  bool Header(const std::string& url,const Clock& clock,std::string& output,
              std::string& error);
  bool Remove(std::span<const uint64_t> identities,const Clock& clock,std::string& error);
  bool Clear(const Clock& clock,std::string& error);
  size_t Size() const;
  size_t AllocatedBytes() const;
  size_t PeakAllocatedBytes() const;
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace suitecut
