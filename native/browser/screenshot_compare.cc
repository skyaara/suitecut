// SPDX-License-Identifier: MIT
#include "screenshot_compare.h"
#include "screenshot_png_codec.h"
#include <atomic>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>
#include "include/cef_parser.h"
namespace suitecut {
struct ScreenshotComparisons::Encoded{std::string text;std::shared_ptr<ScreenshotByteBudget::Lease> lease;};
struct ScreenshotComparisons::Record{
  Key key;int context=0;uint64_t generation=0,attempts=0,sample=0;double deadline=0,threshold=0.2;size_t max_pixels=0;double max_ratio=0;bool has_pixels=false,has_ratio=false,is_not=false;
  enum class Phase{Capture,Decode,Ready,Baseline};Phase phase=Phase::Ready;
  Dict screenshot;Capture capture;Reply reply;std::shared_ptr<AssertionObserver>assertion;
  std::shared_ptr<Encoded> expected_input,captured,actual,diff;
  std::shared_ptr<ScreenshotInputs::Data> prepared;
  std::shared_ptr<ScreenshotByteBudget::Lease> config_lease;
  std::shared_ptr<ScreenshotRaster> expected,previous;
  ScreenshotPixelDelta delta;bool stable=false;
};
struct ScreenshotComparisons::Finalization{
  uint64_t id=0,sample=0;Key key;int context=0;double deadline=0;
  AssertionWork kind=AssertionWork::Validation;
  std::string root;std::shared_ptr<Encoded>actual,diff;
  std::shared_ptr<AssertionObserver>assertion;ArtifactReply reply;std::shared_ptr<ScreenshotArtifactPublication>publication=std::make_shared<ScreenshotArtifactPublication>();std::shared_ptr<ScreenshotComparisons>physical_owner;bool canceled=false;
};
struct ScreenshotComparisons::Pending{
  uint64_t id=0,generation=0;Key key;int context=0;double threshold=0;
  std::shared_ptr<Encoded> expected_input,actual;
  std::shared_ptr<ScreenshotInputs::Data> prepared;
  std::shared_ptr<ScreenshotRaster> expected,previous,image;
  std::shared_ptr<ScreenshotByteBudget> budget;
  ScreenshotPixelDelta delta;bool stable=false;std::string error;
  std::vector<uint8_t> diff_png;std::shared_ptr<ScreenshotByteBudget::Lease> diff_lease;ScreenshotNativeImages native_images;
  std::shared_ptr<ScreenshotComparisons> physical_owner;
  std::shared_ptr<AssertionObserver>assertion;std::shared_ptr<Finalization>finalization;
  uint64_t sample=0;bool baseline_only=false;double original_deadline=0;
  std::shared_ptr<ScreenshotArtifactPublication>publication;
  std::atomic_bool done=false,canceled=false;
};
namespace {
bool Number(CefRefPtr<CefDictionaryValue> value,const char* key,double& number){const auto type=value->GetType(key);if(type!=VTYPE_DOUBLE&&type!=VTYPE_INT)return false;number=type==VTYPE_INT?value->GetInt(key):value->GetDouble(key);return std::isfinite(number);}
bool Base64(const std::string& text){
  if(text.empty()||text.size()%4||text.size()>((kScreenshotComparisonPNGBytes+2)/3)*4)return false;
  size_t padding=0;for(size_t i=0;i<text.size();++i){const unsigned char c=text[i];if(c=='='){if(i+2<text.size()||++padding>2)return false;}else if(padding||!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='+'||c=='/'))return false;}return true;
}
struct DecodedPNG{std::vector<uint8_t> bytes;std::shared_ptr<ScreenshotByteBudget::Lease> lease;};
DecodedPNG Decode(const std::string& text,const std::shared_ptr<ScreenshotByteBudget>& budget,std::string& error){
  // Canonical padding bits are checked directly, without allocating another
  // encoded copy merely to round-trip the base64 spelling.
  const auto sextet=[](char c){return c>='A'&&c<='Z'?c-'A':c>='a'&&c<='z'?c-'a'+26:c>='0'&&c<='9'?c-'0'+52:c=='+'?62:63;};
  const size_t padding=text.ends_with("==")?2:text.ends_with("=")?1:0;
  if((padding==2&&(sextet(text[text.size()-3])&15))||(padding==1&&(sextet(text[text.size()-2])&3))){error="Native screenshot comparison PNG base64 is not canonical";return {};}
  const size_t length=text.size()/4*3-padding;DecodedPNG decoded;decoded.lease=budget->Reserve(length*2);
  if(!decoded.lease){error="Native screenshot comparison64MiB binary input budget exceeded";return {};}
  auto binary=CefBase64Decode(text);if(!binary||binary->GetSize()!=length||length>kScreenshotComparisonPNGBytes){error="Native screenshot comparison PNG input exceeds2MiB or is invalid";return {};}
  decoded.bytes.resize(length);if(binary->GetData(decoded.bytes.data(),length,0)!=length){error="Native screenshot comparison PNG base64 is invalid";return {};}
  binary=nullptr;decoded.lease->Shrink(length);return decoded;
}
}
ScreenshotComparisons::ScreenshotComparisons(Hooks hooks):hooks_(std::move(hooks)),budget_(std::make_shared<ScreenshotByteBudget>()){}
ScreenshotComparisons::~ScreenshotComparisons(){for(auto& [key,record]:records_){record->capture={};record->reply={};}records_.clear();}
std::shared_ptr<ScreenshotComparisons::Encoded> ScreenshotComparisons::Input(const std::string& value,std::string& error){
  if(!Base64(value)){error="Native screenshot comparison expects bounded canonical PNG base64";return {};}
  auto lease=budget_->Reserve(value.size());if(!lease){error="Native screenshot comparison64MiB byte budget exceeded";return {};}
  auto input=std::make_shared<Encoded>();input->text=value;input->lease=std::move(lease);return input;
}
bool ScreenshotComparisons::Contains(int page,int owner)const{return records_.contains({page,owner});}
bool ScreenshotComparisons::Current(const std::shared_ptr<Record>& record)const{const auto found=records_.find(record->key);return found!=records_.end()&&found->second==record;}
void ScreenshotComparisons::Handle(int context,int page,int owner,Dict config,double deadline,Capture capture,Reply reply,std::shared_ptr<AssertionObserver> assertion){Begin(context,page,owner,config,nullptr,deadline,std::move(capture),std::move(reply),std::move(assertion));}
void ScreenshotComparisons::HandlePrepared(int context,int page,int owner,Dict config,std::shared_ptr<ScreenshotInputs::Data> input,double deadline,Capture capture,Reply reply,std::shared_ptr<AssertionObserver> assertion){
  Begin(context,page,owner,config,std::move(input),deadline,std::move(capture),std::move(reply),std::move(assertion));
}
void ScreenshotComparisons::Begin(int context,int page,int owner,Dict config,std::shared_ptr<ScreenshotInputs::Data> input,double deadline,Capture capture,Reply reply,std::shared_ptr<AssertionObserver> assertion){
  if(!reply)return;
  if(context<=0||page<=0||owner<=0||!config||!capture||std::isnan(deadline)||deadline<=0||records_.size()+finalizations_.size()>=16||(hooks_.available&&!hooks_.available())||Contains(page,owner)||generation_==std::numeric_limits<uint64_t>::max()){
    reply(false,nullptr,"Invalid native screenshot comparison owner, deadline, or16-owner capacity");return;
  }
  auto record=std::make_shared<Record>();record->key={page,owner};record->context=context;record->deadline=deadline;record->capture=std::move(capture);record->reply=std::move(reply);record->generation=++generation_;record->assertion=std::move(assertion);
  double threshold=0,max_pixels=0,max_ratio=0;
  if((!input&&config->GetType("expected")!=VTYPE_STRING)||config->GetType("screenshot")!=VTYPE_DICTIONARY||!Number(config,"threshold",threshold)||threshold<0||threshold>1||config->GetType("isNot")!=VTYPE_BOOL||
     (config->HasKey("maxDiffPixels")&&(!Number(config,"maxDiffPixels",max_pixels)||max_pixels<0||max_pixels>kScreenshotComparisonPixels||std::floor(max_pixels)!=max_pixels))||
     (config->HasKey("maxDiffPixelRatio")&&(!Number(config,"maxDiffPixelRatio",max_ratio)||max_ratio<0||max_ratio>1))){record->reply(false,nullptr,"Invalid native screenshot comparison options");return;}
  record->threshold=threshold;record->max_pixels=size_t(max_pixels);record->max_ratio=max_ratio;record->has_pixels=config->HasKey("maxDiffPixels");record->has_ratio=config->HasKey("maxDiffPixelRatio");record->is_not=config->GetBool("isNot");
  std::string error;
  if(input){int width=0,height=0;if(!input->reservation||input->reservation->owner!=budget_||input->reservation->bytes.load(std::memory_order_acquire)<input->png.capacity()+input->config.size()||!ScreenshotPNGDimensions(input->png,width,height,error)){record->reply(false,nullptr,error.empty()?"Native screenshot prepared PNG has no byte reservation":error);return;}record->prepared=std::move(input);}
  else{record->expected_input=Input(config->GetString("expected").ToString(),error);if(!record->expected_input){record->reply(false,nullptr,error);return;}}
  const auto screenshot=config->GetDictionary("screenshot");auto serial=CefValue::Create();serial->SetDictionary(screenshot->Copy(false));const auto text=CefWriteJSON(serial,JSON_WRITER_DEFAULT).ToString();
  if(text.empty()||text.size()>1048576||(record->config_lease=budget_->Reserve(text.size()))==nullptr){record->reply(false,nullptr,"Native screenshot assertion options exceed1MiB or shared byte budget");return;}
  if(record->prepared){std::string().swap(record->prepared->config);record->prepared->reservation->Shrink(record->prepared->png.capacity());}
  record->screenshot=screenshot->Copy(false);record->screenshot->SetString("action","screenshot");record->screenshot->SetString("format","png");records_[record->key]=record;
  if(record->assertion){record->phase=Record::Phase::Baseline;Compute(record);}else CaptureNext(record);
  if(hooks_.wake)hooks_.wake();
}
void ScreenshotComparisons::CaptureNext(const std::shared_ptr<Record>& record){
  if(!Current(record))return;
  if(hooks_.now()>=record->deadline){Finish(record,false,true);return;}
  if(record->attempts==std::numeric_limits<uint64_t>::max()||sample_generation_==std::numeric_limits<uint64_t>::max()){Fail(record,"Native screenshot comparison sample counter exhausted");return;}
  record->phase=Record::Phase::Capture;++record->attempts;record->sample=++sample_generation_;
  try{const auto capture=record->capture;capture(record->sample,record->screenshot->Copy(false));}catch(...){Fail(record,"Native screenshot presentation/capture dispatch failed");}
}
bool ScreenshotComparisons::Captured(int page,int owner,uint64_t sample,bool ok,const std::string& error,Dict result){
  const auto found=records_.find({page,owner});if(found==records_.end())return false;const auto record=found->second;if(record->sample!=sample)return false;
  if(record->phase!=Record::Phase::Capture){Fail(record,"Native screenshot comparison received an unexpected capture receipt");return true;}
  if(hooks_.now()>=record->deadline){Finish(record,false,true);return true;}
  if(!ok||!result||result->GetType("data")!=VTYPE_STRING){Fail(record,error.empty()?"Native screenshot capture is unavailable":error);return true;}
  std::string input_error;record->captured=Input(result->GetString("data").ToString(),input_error);if(!record->captured){Fail(record,input_error);return true;}
  record->phase=Record::Phase::Decode;Compute(record);return true;
}
void ScreenshotComparisons::Compute(const std::shared_ptr<Record>& record){
  if(!Current(record)||(record->phase!=Record::Phase::Decode&&record->phase!=Record::Phase::Baseline)||pending_.size()>=2||(hooks_.physical_available&&!hooks_.physical_available()))return;
  if(hooks_.now()>=record->deadline){Finish(record,false,true);return;}
  if(job_generation_==std::numeric_limits<uint64_t>::max()){Fail(record,"Native screenshot comparison job counter exhausted");return;}
  auto work=std::make_shared<Pending>();work->id=++job_generation_;work->baseline_only=record->phase==Record::Phase::Baseline;work->sample=work->baseline_only?0:record->sample;work->assertion=record->assertion;work->key=record->key;work->context=record->context;work->generation=record->generation;work->threshold=record->threshold;work->original_deadline=record->deadline;work->expected_input=record->expected_input;work->prepared=record->prepared;work->expected=record->expected;work->previous=record->previous;work->actual=record->captured;record->captured.reset();work->budget=budget_;work->native_images.owners.reserve(3);work->physical_owner=shared_from_this();pending_[work->id]=work;
  if(work->assertion&&(!work->assertion->admit||!work->assertion->submitting||!work->assertion->settled||!work->assertion->observed||!work->assertion->admit(work->id,work->sample,AssertionWork::Pixels))){pending_.erase(work->id);work->physical_owner.reset();Fail(record,"Native screenshot codec owner admission rejected");return;}
  // One record can own only one decode job; a queued record remains Decode.
  record->phase=Record::Phase::Ready;
  const auto weak=weak_from_this();const auto post=hooks_.post;const auto clock=hooks_.now;bool accepted=false;
  try{if(work->assertion&&!work->assertion->submitting(work->id))throw std::runtime_error("Native screenshot codec submission admission rejected");accepted=hooks_.background([weak,post,clock,work]{
    try{
      if(work->canceled.load(std::memory_order_acquire)){work->done.store(true,std::memory_order_release);try{post([weak,id=work->id]{if(const auto self=weak.lock())self->FinishPhysical(id);});}catch(...){}return;}
      if(!work->expected){
        if(work->prepared)work->expected=DecodeScreenshotPNG(work->prepared->png,work->budget,work->native_images,work->error);
        else{const auto png=Decode(work->expected_input->text,work->budget,work->error);if(work->error.empty())work->expected=DecodeScreenshotPNG(png.bytes,work->budget,work->native_images,work->error);}
      }
      if(!work->baseline_only&&work->error.empty()){const auto png=Decode(work->actual->text,work->budget,work->error);if(work->error.empty())work->image=DecodeScreenshotPNG(png.bytes,work->budget,work->native_images,work->error);}
      if(!work->baseline_only&&work->error.empty()&&!work->canceled.load(std::memory_order_acquire)&&clock()<work->original_deadline){
        work->stable=work->previous&&ScreenshotPixelsEqual(*work->previous,*work->image);work->delta=CompareScreenshotPixels(*work->expected,*work->image,work->threshold);
        // A changing page can reach its original deadline without two equal
        // captures. Keep a diagnostic for this exact admitted actual/baseline
        // pair as part of the same physical job; stability still gates success.
        if((!work->delta.dimensions_match||work->delta.different)&&!work->canceled.load(std::memory_order_acquire)&&clock()<work->original_deadline){
          auto diff=ScreenshotDifferenceImage(*work->expected,*work->image,work->threshold,work->budget,work->error);
          // Opposite aspect ratios can have a bounded input but oversized diff
          // canvas. Keep their truthful dimension mismatch without a diff PNG.
          if(!diff&&work->error.find("canvas exceeds")!=std::string::npos)work->error.clear();
          else if(diff)work->diff_png=EncodeScreenshotPNG(*diff,work->budget,work->native_images,work->diff_lease,work->error);
        }
      }
    }catch(...){work->error="Native screenshot comparison decoder or pixel job failed";}
    work->done.store(true,std::memory_order_release);
    try{post([weak,id=work->id]{if(const auto self=weak.lock())self->FinishPhysical(id);});}catch(...){/* Native owner Tick drains rejected completion posts. */}
  });}catch(...){accepted=false;}
  if(!accepted){
    pending_.erase(work->id);work->physical_owner.reset();
    if(work->assertion)work->assertion->settled(work->id,false,false,"Native screenshot comparison background capacity is unavailable");
    Fail(record,"Native screenshot comparison background capacity is unavailable");
  }
}
void ScreenshotComparisons::FinishPhysical(uint64_t id){
  const auto pending=pending_.find(id);if(pending==pending_.end()||!pending->second->done.load(std::memory_order_acquire))return;
  const auto work=pending->second;work->native_images.ReleaseOnUI();
  const auto retire=[this,work]{
    work->expected_input.reset();work->prepared.reset();work->actual.reset();work->expected.reset();work->previous.reset();work->image.reset();std::vector<uint8_t>().swap(work->diff_png);work->diff_lease.reset();work->budget.reset();
    pending_.erase(work->id);
    if(work->finalization){work->finalization->actual.reset();work->finalization->diff.reset();work->finalization->physical_owner.reset();}
    if(work->finalization&&work->finalization->reply){
      try{work->finalization->reply(work->publication);}catch(...){if(work->error.empty())work->error="Native screenshot artifact result consumer failed";}
    }
    if(work->assertion)work->assertion->settled(work->id,true,work->error.empty()&&!work->canceled.load(std::memory_order_acquire),work->error.empty()&&work->canceled.load(std::memory_order_acquire)?"Native screenshot physical job canceled":work->error);
    if(hooks_.settled_context)hooks_.settled_context(work->context);
    if(hooks_.wake)hooks_.wake();
    work->physical_owner.reset();
  };
  const auto found=records_.find(work->key);if(found==records_.end()||found->second->generation!=work->generation){retire();return;}const auto record=found->second;
  if(hooks_.now()>=record->deadline){Finish(record,false,true);retire();return;}
  if(!work->error.empty()){Fail(record,work->error);retire();return;}
  if(work->baseline_only){
    record->expected=work->expected;record->expected_input.reset();record->prepared.reset();record->phase=Record::Phase::Ready;
    retire();CaptureNext(record);return;
  }
  if(work->assertion&&!work->assertion->observed(work->id)){
    if(hooks_.now()>=record->deadline)Finish(record,false,true);else Fail(record,"Native screenshot observation owner or document changed");
    retire();return;
  }
  record->expected=work->expected;record->expected_input.reset();record->prepared.reset();record->actual=work->actual;record->previous=work->image;record->delta=work->delta;record->stable=work->stable;record->diff.reset();
  if(!work->diff_png.empty()){
    const auto base64=CefBase64Encode(work->diff_png.data(),work->diff_png.size()).ToString();std::string error;record->diff=Input(base64,error);if(!record->diff){Fail(record,error);retire();return;}
  }
  const size_t allowed=record->has_pixels&&record->has_ratio?std::min(record->max_pixels,size_t(std::floor(record->max_ratio*work->delta.total))):record->has_pixels?record->max_pixels:record->has_ratio?size_t(std::floor(record->max_ratio*work->delta.total)):0;
  const bool matches=work->delta.dimensions_match&&work->delta.different<=allowed;
  if(work->stable&&(matches!=record->is_not)){Finish(record,true,false);retire();return;}
  record->phase=Record::Phase::Ready;retire();CaptureNext(record);
}
void ScreenshotComparisons::Finish(std::shared_ptr<Record> record,bool passed,bool timed_out,const std::string& error){
  if(!Current(record))return;
  auto result=CefDictionaryValue::Create();result->SetBool("passed",passed);result->SetBool("stable",record->stable);result->SetBool("timedOut",timed_out);if(record->previous){result->SetBool("dimensionsMatch",record->delta.dimensions_match);result->SetDouble("differentPixels",double(record->delta.different));result->SetDouble("totalPixels",double(record->delta.total));}result->SetDouble("attempts",double(record->attempts));result->SetDouble("threshold",record->threshold);
  if(record->expected){result->SetInt("expectedWidth",record->expected->width);result->SetInt("expectedHeight",record->expected->height);}if(record->previous){result->SetInt("actualWidth",record->previous->width);result->SetInt("actualHeight",record->previous->height);}
  if(record->actual)result->SetString("actual",record->actual->text);
  if(record->diff)result->SetString("diff",record->diff->text);
  auto unobserved=record->assertion?record->assertion->unobserved:std::function<void(uint64_t,Dict)>{};
  auto retained=record->captured;const auto sample=record->sample;
  auto reply=std::move(record->reply);records_.erase(record->key);record->capture={};record->screenshot=nullptr;record->expected_input.reset();record->prepared.reset();record->captured.reset();record->actual.reset();record->diff.reset();record->expected.reset();record->previous.reset();record->config_lease.reset();reply(error.empty(),result,error);
  // This exact received PNG was queued behind the shared native physical cap.
  // The Source freezes first and admits validation only; it is not Received.
  if(retained&&unobserved){auto actual=CefDictionaryValue::Create();actual->SetString("data",retained->text);unobserved(sample,actual);}
}
void ScreenshotComparisons::Fail(const std::shared_ptr<Record>& record,const std::string& error){Finish(record,false,false,error);}
void ScreenshotComparisons::Cancel(int page,int owner){
  const Key key{page,owner};
  const auto found=records_.find(key);
  if(found!=records_.end()){found->second->capture={};found->second->reply={};records_.erase(found);}
  for(const auto&[id,work]:pending_){(void)id;if(work->key==key)work->canceled.store(true,std::memory_order_release);}
  for(const auto&[id,work]:finalizations_){(void)id;if(work->key==key)work->canceled=true;}
  if(hooks_.wake)hooks_.wake();
}
void ScreenshotComparisons::PageClosed(int page){std::vector<Key> closed;for(const auto& [key,record]:records_)if(key.first==page)closed.push_back(key);for(const auto& key:closed)Cancel(key.first,key.second);for(const auto&[id,work]:pending_){(void)id;if(work->key.first==page)work->canceled.store(true,std::memory_order_release);}for(const auto&[id,work]:finalizations_){(void)id;if(work->key.first==page)work->canceled=true;}}
bool ScreenshotComparisons::ContextReady(int context)const{for(const auto& [key,record]:records_)if(record->context==context)return false;for(const auto& [id,work]:pending_)if(work->context==context)return false;for(const auto&[id,work]:finalizations_){(void)id;if(work->context==context)return false;}return true;}
void ScreenshotComparisons::Deadline(int page,int owner){const auto found=records_.find({page,owner});if(found!=records_.end())Finish(found->second,false,true);}
bool ScreenshotComparisons::QueueFinalization(const std::shared_ptr<Finalization>&work,std::string&error){
  if(work->context<1||work->key.first<1||work->key.second<1||!std::isfinite(work->deadline)||work->deadline<=hooks_.now()||records_.size()+finalizations_.size()>=16||
     !work->assertion||!work->assertion->admit||!work->assertion->submitting||!work->assertion->settled||job_generation_==UINT64_MAX){error="Invalid native screenshot finalization owner/deadline/capacity";return false;}
  for(const auto&[id,held]:finalizations_){(void)id;if(held->key==work->key&&held->sample==work->sample&&held->kind==work->kind){error="Duplicate native screenshot finalization receipt";return false;}}
  work->publication->error.reserve(4096);
  work->id=++job_generation_;work->physical_owner=shared_from_this();
  try{finalizations_.emplace(work->id,work);}catch(...){work->physical_owner.reset();error="Native screenshot finalization allocation failed";return false;}
  bool admitted=false;
  try{admitted=work->assertion->admit(work->id,work->sample,work->kind);}catch(...){admitted=false;}
  if(!admitted){finalizations_.erase(work->id);work->physical_owner.reset();error="Native screenshot finalization original owner admission rejected";return false;}
  FinalizeNext(work);if(hooks_.wake)hooks_.wake();return true;
}
bool ScreenshotComparisons::ValidateCaptured(int context,int page,int owner,uint64_t sample,Dict result,double deadline,std::shared_ptr<AssertionObserver>assertion,std::string&error){
  try{
  if(!sample||!result||result->GetType("data")!=VTYPE_STRING){error="Invalid native late capture validation receipt";return false;}
  auto work=std::make_shared<Finalization>();work->context=context;work->key={page,owner};work->sample=sample;work->deadline=deadline;work->assertion=std::move(assertion);work->kind=AssertionWork::Validation;
  work->actual=Input(result->GetString("data").ToString(),error);if(!work->actual)return false;
  return QueueFinalization(work,error);
  }catch(const std::bad_alloc&){error="Native screenshot finalization allocation failed before admission";return false;}
}
bool ScreenshotComparisons::WriteArtifacts(int context,int page,int owner,const std::string&root,const std::string&actual,const std::string&diff,double deadline,std::shared_ptr<AssertionObserver>assertion,ArtifactReply reply,std::string&error){
  try{
  if(root.empty()||root.size()>32768||!reply){error="Invalid native screenshot artifact output admission";return false;}
  auto work=std::make_shared<Finalization>();work->context=context;work->key={page,owner};work->deadline=deadline;work->assertion=std::move(assertion);work->kind=AssertionWork::Files;work->root=root;work->reply=std::move(reply);
  work->actual=Input(actual,error);if(!work->actual)return false;
  if(!diff.empty()){work->diff=Input(diff,error);if(!work->diff)return false;}
  return QueueFinalization(work,error);
  }catch(const std::bad_alloc&){error="Native screenshot finalization allocation failed before admission";return false;}
}
void ScreenshotComparisons::RetireFinalization(const std::shared_ptr<Finalization>&work,const std::string&error){
  const auto found=finalizations_.find(work->id);if(found==finalizations_.end()||found->second!=work)return;
  const auto assertion=work->assertion;auto reply=std::move(work->reply);
  work->actual.reset();work->diff.reset();finalizations_.erase(found);work->physical_owner.reset();
  const auto publication=work->publication;publication->error=error;
  try{if(reply)reply(publication);}catch(...){/* Actual no-submit retirement must still reach its native owner. */}
  assertion->settled(work->id,false,false,error);
  if(hooks_.settled_context)hooks_.settled_context(work->context);
  if(hooks_.wake)hooks_.wake();
}
void ScreenshotComparisons::FinalizeNext(const std::shared_ptr<Finalization>&final){
  const auto found=finalizations_.find(final->id);if(found==finalizations_.end()||found->second!=final)return;
  if(final->canceled){RetireFinalization(final,"Native screenshot finalization owner canceled");return;}
  if(hooks_.now()>=final->deadline){RetireFinalization(final,"Native screenshot finalization original cleanup deadline exceeded");return;}
  if(pending_.size()>=2||(hooks_.physical_available&&!hooks_.physical_available()))return;
  std::shared_ptr<Pending>work;
  try{
    work=std::make_shared<Pending>();work->id=final->id;work->key=final->key;work->context=final->context;work->sample=final->sample;work->assertion=final->assertion;work->finalization=final;work->actual=final->actual;work->budget=budget_;work->native_images.owners.reserve(1);work->publication=final->publication;work->physical_owner=shared_from_this();
    pending_.emplace(work->id,work);
  }catch(const std::bad_alloc&){if(work)work->physical_owner.reset();RetireFinalization(final,"Native screenshot finalization allocation failed before submission");return;}
  finalizations_.erase(found);
  const auto weak=weak_from_this();const auto post=hooks_.post;const auto clock=hooks_.now;bool accepted=false;
  try{
    if(!work->assertion->submitting(work->id))throw std::runtime_error("Native screenshot finalization submission rejected");
    accepted=hooks_.background([weak,post,clock,work]{
      try{
        if(work->canceled.load(std::memory_order_acquire))work->error="Native screenshot finalization owner canceled";
        else if(clock()>=work->finalization->deadline)work->error="Native screenshot finalization original cleanup deadline exceeded";
        else if(work->finalization->kind==AssertionWork::Validation){
          const auto bytes=Decode(work->actual->text,work->budget,work->error);
          if(work->error.empty())work->image=DecodeScreenshotPNG(bytes.bytes,work->budget,work->native_images,work->error);
          // Deliberately no CompareScreenshotPixels/ScreenshotPixelsEqual,
          // diff generation, capture, or observation callback in this branch.
        }else{
          const auto actual=Decode(work->actual->text,work->budget,work->error);
          DecodedPNG diff;
          if(work->error.empty()&&work->finalization->diff)diff=Decode(work->finalization->diff->text,work->budget,work->error);
          if(work->error.empty()){
            *work->publication=WriteScreenshotArtifacts(work->finalization->root,actual.bytes,diff.bytes,work->finalization->deadline,work->canceled,clock);
            if(!work->publication->committed)work->error=work->publication->error.empty()?"Native screenshot artifact publication failed":work->publication->error;
            if(work->publication->cleanup_failed){if(!work->error.empty())work->error+="; ";work->error+=work->publication->cleanup_error;}
          }
        }
      }catch(...){work->error="Native screenshot finalization codec/file job failed";}
      work->done.store(true,std::memory_order_release);
      try{post([weak,id=work->id]{if(const auto self=weak.lock())self->FinishPhysical(id);});}catch(...){/* Tick retains and drains the real completion. */}
    });
  }catch(...){accepted=false;}
  if(!accepted){
    pending_.erase(work->id);work->physical_owner.reset();final->actual.reset();final->diff.reset();final->physical_owner.reset();
    const auto publication=final->publication;publication->error="Native screenshot finalization background submission rejected";
    try{if(final->reply)final->reply(publication);}catch(...){/* Preserve actual rejected submission retirement. */}
    work->assertion->settled(work->id,false,false,publication->error);
    if(hooks_.settled_context)hooks_.settled_context(work->context);
    if(hooks_.wake)hooks_.wake();
  }
}
void ScreenshotComparisons::Tick(){
  std::vector<uint64_t> completed;for(const auto& [id,work]:pending_)if(work->done.load(std::memory_order_acquire))completed.push_back(id);for(const auto id:completed)FinishPhysical(id);
  std::vector<std::shared_ptr<Record>> live;for(const auto& [key,record]:records_)live.push_back(record);for(const auto& record:live){if(!Current(record))continue;if(hooks_.now()>=record->deadline)Finish(record,false,true);else if(record->phase==Record::Phase::Decode||record->phase==Record::Phase::Baseline)Compute(record);}
  std::vector<std::shared_ptr<Finalization>>final;for(const auto&[id,work]:finalizations_){(void)id;final.push_back(work);}for(const auto&work:final)FinalizeNext(work);
}
} // namespace suitecut
