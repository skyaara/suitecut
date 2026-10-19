// SPDX-License-Identifier: MIT
#pragma once
#include "screenshot_pixels.h"
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
namespace suitecut {
// Native input admission owns the original assertion deadline, before path I/O
// or chunk uploads. No accepted file task is retired by logical cancellation.
class ScreenshotInputs:public std::enable_shared_from_this<ScreenshotInputs>{
 public:
  enum class Purpose {Comparison, Screenshot, PDF};
  struct Owner{int context=0,page=0;uint64_t source=0;bool operator==(const Owner&)const=default;};
  struct Data{std::shared_ptr<void> admission;std::shared_ptr<ScreenshotByteBudget::Lease> reservation;std::vector<uint8_t> png;std::string config;};
  struct Ticket{std::string token;double deadline=0;};
  using Reply=std::function<void(std::shared_ptr<Data>,double,const std::string&)>;
  struct Hooks{
    std::function<double()> now;
    std::function<bool(std::function<void()>)> background,post;
    std::function<void()> wake;
    std::function<void(int)> settled;
    std::function<bool()> available,physical_available;
  };
  ScreenshotInputs(std::shared_ptr<ScreenshotByteBudget> budget,Hooks hooks);
  ~ScreenshotInputs();
  Ticket Prepare(Owner owner,const std::string&client,bool file,size_t png_bytes,size_t config_bytes,double deadline,std::string& error,Purpose purpose=Purpose::Comparison);
  bool Append(Owner owner,const std::string&token,bool config,size_t offset,const std::vector<uint8_t>& bytes,std::string&error);
  bool Inspect(Owner owner,const std::string&token,std::string&json,std::string&error,Purpose purpose=Purpose::Comparison,std::shared_ptr<Data>* retention=nullptr);
  bool Claim(Owner owner,const std::string&token,const std::string&path,int command,Reply reply,std::string&error,Purpose purpose=Purpose::Comparison);
  bool Watch(Owner owner,const std::string&token,int command,std::function<void(bool,const std::string&)> reply,std::string&error);
  void CancelWait(int command);
  void DiscardClient(int page,const std::string&client);
  void Discard(int context,int page,const std::string&token);
  void Clear();
  void Cancel(Owner owner,const std::string&token);
  void CancelCommand(int page,int command);
  void PageClosed(int page);
  void Tick();
  bool ContextReady(int context)const;
  size_t LogicalCount()const{return records_.size();}
  size_t RetainedCount()const;
  size_t AdmissionCount()const{return LogicalCount()+RetainedCount();}
  size_t PhysicalCount()const{return pending_.size();}
  bool NeedsTick()const{return AdmissionCount()!=0||!pending_.empty();}
 private:
  struct Record;struct Pending;struct Retentions;struct Admission;
  std::shared_ptr<Record> Find(Owner owner,const std::string&token,std::string&error);
  void Finish(std::shared_ptr<Record> record,const std::string&error);
  void Cancel(std::shared_ptr<Record> record);
  void FinishPhysical(uint64_t id);
  void StartFile(const std::shared_ptr<Record>& record);
  Hooks hooks_;std::shared_ptr<ScreenshotByteBudget>budget_;
  std::map<std::string,std::shared_ptr<Record>> records_;
  std::map<uint64_t,std::shared_ptr<Pending>>pending_;
  std::shared_ptr<Retentions>retentions_;
  uint64_t generation_=0;
};
} // namespace suitecut
