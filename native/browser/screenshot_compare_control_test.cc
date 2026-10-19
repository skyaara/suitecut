// SPDX-License-Identifier: MIT
#include "screenshot_compare_control_test.h"
#include "screenshot_compare.h"
#include "screenshot_png_codec.h"
#include <cassert>
#include <deque>
#include <thread>
#include <limits>
#include <filesystem>
#include <iostream>
#include <map>
#include <unistd.h>
#include <fcntl.h>
#include "include/cef_parser.h"
namespace {
using Controls=suitecut::ScreenshotComparisons;using Dict=Controls::Dict;
suitecut::ScreenshotRaster Solid(int width,int height,uint8_t r,uint8_t g,uint8_t b){suitecut::ScreenshotRaster image;image.width=width;image.height=height;for(int i=0;i<width*height;++i)image.rgba.insert(image.rgba.end(),{r,g,b,255});return image;}
std::string PNG(const suitecut::ScreenshotRaster& raster){suitecut::ScreenshotNativeImages images;std::string error;std::shared_ptr<suitecut::ScreenshotByteBudget::Lease> output;const auto bytes=suitecut::EncodeScreenshotPNG(raster,std::make_shared<suitecut::ScreenshotByteBudget>(),images,output,error);assert(error.empty()&&!bytes.empty());images.ReleaseOnUI();return CefBase64Encode(bytes.data(),bytes.size());}
suitecut::ScreenshotRaster ReadPNG(const std::string& encoded){
 auto binary=CefBase64Decode(encoded);assert(binary);std::vector<uint8_t> bytes(binary->GetSize());assert(binary->GetData(bytes.data(),bytes.size(),0)==bytes.size());
 suitecut::ScreenshotNativeImages images;std::string error;auto raster=suitecut::DecodeScreenshotPNG(bytes,std::make_shared<suitecut::ScreenshotByteBudget>(),images,error);assert(raster&&error.empty());images.ReleaseOnUI();return *raster;
}
struct Fixture{
 double now=1;bool reject_background=false,reject_post=false;
 std::deque<std::function<void()>> background,owner;
 std::deque<uint64_t> captures;
 struct Result{bool ok;Dict value;std::string error;};std::vector<Result> replies;
 std::shared_ptr<Controls> controls;std::shared_ptr<suitecut::ScreenshotInputs> inputs;
 Fixture(){Controls::Hooks hooks;hooks.available=[this]{return !inputs||inputs->LogicalCount()+controls->LogicalCount()<16;};hooks.physical_available=[this]{return !inputs||inputs->PhysicalCount()+controls->PhysicalCount()<2;};hooks.now=[this]{return now;};hooks.background=[this](auto task){if(reject_background)return false;background.push_back(std::move(task));return true;};hooks.post=[this](auto task){if(reject_post)return false;owner.push_back(std::move(task));return true;};controls=std::make_shared<Controls>(hooks);}
 struct Proof{Controls::AssertionWork kind;uint64_t sample;bool entered=false;};
 std::map<uint64_t,Proof>proofs;unsigned observed=0,unobserved=0,validations=0,settlements=0;bool last_valid=false;std::string last_error;
 std::shared_ptr<Controls::AssertionObserver>observer;
 void Observe(){
  observer=std::make_shared<Controls::AssertionObserver>();
  observer->admit=[this](uint64_t job,uint64_t sample,Controls::AssertionWork kind){assert(!proofs.contains(job));proofs.emplace(job,Proof{kind,sample,false});if(kind==Controls::AssertionWork::Validation)++validations;return true;};
  observer->submitting=[this](uint64_t job){auto&proof=proofs.at(job);assert(!proof.entered);proof.entered=true;return true;};
  observer->observed=[this](uint64_t job){const auto&proof=proofs.at(job);assert(proof.entered&&proof.kind==Controls::AssertionWork::Pixels&&proof.sample&&now<100);++observed;return true;};
  observer->settled=[this](uint64_t job,bool submitted,bool valid,const std::string&error){const auto found=proofs.find(job);assert(found!=proofs.end()&&(!submitted||found->second.entered));proofs.erase(found);++settlements;last_valid=valid;last_error=error;};
  observer->unobserved=[this](uint64_t sample,Dict actual){++unobserved;std::string error;assert(controls->ValidateCaptured(1,1,1,sample,actual,1000,observer,error)&&error.empty());};
 }
 void StartObserved(Dict config){Observe();controls->Handle(1,1,1,config,100,[this](uint64_t sample,Dict){captures.push_back(sample);},[this](bool ok,Dict value,const std::string&error){replies.push_back({ok,value,error});},observer);}
 void AttachInputs(){suitecut::ScreenshotInputs::Hooks hooks;hooks.available=[this]{return inputs->LogicalCount()+controls->LogicalCount()<16;};hooks.now=[this]{return now;};hooks.physical_available=[this]{return inputs->PhysicalCount()+controls->PhysicalCount()<2;};hooks.background=[this](auto task){background.push_back(std::move(task));return true;};hooks.post=[this](auto task){owner.push_back(std::move(task));return true;};inputs=std::make_shared<suitecut::ScreenshotInputs>(controls->Budget(),hooks);}
 std::shared_ptr<suitecut::ScreenshotInputs::Data> Raw(const std::string& text){auto binary=CefBase64Decode(text);assert(binary);auto raw=std::make_shared<suitecut::ScreenshotInputs::Data>();raw->png.resize(binary->GetSize());assert(binary->GetData(raw->png.data(),raw->png.size(),0)==raw->png.size());raw->reservation=controls->Budget()->Reserve(raw->png.capacity());assert(raw->reservation);return raw;}
 void StartPrepared(Dict config,std::shared_ptr<suitecut::ScreenshotInputs::Data> data,int page=1,int id=1){config->Remove("expected");controls->HandlePrepared(1,page,id,config,std::move(data),100,[this](uint64_t sample,Dict){captures.push_back(sample);},[this](bool ok,Dict value,const std::string&error){replies.push_back({ok,value,error});});}
 Dict Config(const std::string& expected){auto config=CefDictionaryValue::Create();config->SetString("expected",expected);config->SetDouble("threshold",0.2);config->SetBool("isNot",false);config->SetDictionary("screenshot",CefDictionaryValue::Create());return config;}
 void Start(Dict config,int page=1,int id=1,double deadline=100){controls->Handle(1,page,id,config,deadline,[this](uint64_t sample,Dict){captures.push_back(sample);},[this](bool ok,Dict value,const std::string& error){replies.push_back({ok,value,error});});}
 uint64_t Capture(const std::string& actual,int page=1,int id=1){assert(!captures.empty());const auto sample=captures.front();captures.pop_front();auto result=CefDictionaryValue::Create();result->SetString("data",actual);assert(controls->Captured(page,id,sample,true,"",result));return sample;}
 void CPU(){assert(!background.empty());auto work=std::move(background.front());background.pop_front();std::thread thread([&]{work();});thread.join();work={};}
 void Owner(){assert(!owner.empty());auto work=std::move(owner.front());owner.pop_front();work();work={};}
 void Sample(const std::string& actual){Capture(actual);CPU();Owner();}
 void Empty(){if(inputs)assert(!inputs->PhysicalCount()&&!inputs->LogicalCount());assert(!controls->LogicalCount()&&!controls->PhysicalCount()&&!controls->Bytes());assert(background.empty()&&owner.empty());}
};
}
void RunScreenshotComparisonControlTests(){
 assert(CefCurrentlyOn(TID_UI));const auto red=PNG(Solid(2,2,255,0,0)),blue=PNG(Solid(2,2,0,0,255));
 {
  Fixture test;test.Start(test.Config(red));test.Sample(red);assert(test.replies.empty()&&test.captures.size()==1);test.Sample(red);assert(test.replies.size()==1&&test.replies[0].ok&&test.replies[0].value->GetBool("passed")&&test.replies[0].value->GetBool("stable"));assert(test.replies[0].value->GetDouble("attempts")==2&&test.replies[0].value->GetDouble("differentPixels")==0);test.Empty();
 }
 {
  Fixture test;test.Start(test.Config(blue));test.Sample(red);test.Sample(red);assert(test.replies.empty()&&test.captures.size()==1);test.now=100;test.controls->Deadline(1,1);assert(test.replies.size()==1&&test.replies[0].ok&&!test.replies[0].value->GetBool("passed")&&test.replies[0].value->GetBool("timedOut"));assert(test.replies[0].value->GetDouble("differentPixels")==4&&test.replies[0].value->GetType("diff")==VTYPE_STRING);test.captures.clear();test.Empty();
 }
 {
  // No stable pair: timeout diagnostics describe the last actually admitted
  // frame, not the prior all-blue mismatch and not a fabricated stable sample.
  auto changed=Solid(2,2,255,0,0);changed.rgba[0]=0;changed.rgba[2]=255;const auto last=PNG(changed);
  Fixture test;test.Start(test.Config(red));test.Sample(blue);test.Sample(last);assert(test.replies.empty());test.now=100;test.controls->Deadline(1,1);
  assert(test.replies.size()==1&&test.replies[0].ok);const auto result=test.replies[0].value;
  assert(!result->GetBool("passed")&&!result->GetBool("stable")&&result->GetBool("timedOut")&&result->GetDouble("differentPixels")==1);
  assert(result->GetString("actual")==last&&result->GetDouble("attempts")==3);
  const auto diff=ReadPNG(result->GetString("diff"));assert(diff.width==2&&diff.height==2);
  assert((diff.rgba==std::vector<uint8_t>{255,0,0,255,155,155,155,255,155,155,155,255,155,155,155,255}));
  assert(test.captures.size()==1&&test.background.empty());test.captures.clear();test.Empty();
 }
 {
  // An actual later decode is held across the original deadline. The logical
  // timeout retains the earlier admitted pair while its real CPU/UI job stays
  // owned; late completion cannot publish the newer actual/diff or a success.
  Fixture test;test.Start(test.Config(red));test.Sample(blue);test.Capture(red);assert(test.controls->PhysicalCount()==1);
  test.now=100;test.controls->Deadline(1,1);assert(test.replies.size()==1&&test.replies[0].ok);
  const auto result=test.replies[0].value;const auto retained_diff=result->GetString("diff").ToString();
  assert(!result->GetBool("stable")&&result->GetBool("timedOut")&&result->GetString("actual")==blue&&result->GetDouble("differentPixels")==4&&!retained_diff.empty());
  assert(test.controls->LogicalCount()==0&&test.controls->PhysicalCount()==1&&test.controls->Bytes()>0);
  test.CPU();assert(test.controls->PhysicalCount()==1);test.Owner();
  assert(test.replies.size()==1&&result->GetString("actual")==blue&&result->GetString("diff")==retained_diff);test.Empty();
 }
 {
  // The newly produced unstable diff has actual initialized native image
  // ownership. Logical cancellation cannot retire its held UI release.
  Fixture test;test.Start(test.Config(red));test.Capture(blue);test.CPU();test.controls->Cancel(1,1);
  assert(test.replies.empty()&&test.controls->PhysicalCount()==1&&test.controls->Bytes()>0);test.Owner();test.Empty();
 }
 {
  Fixture test;auto config=test.Config(red);config->SetBool("isNot",true);test.Start(config);test.Sample(blue);test.Sample(blue);assert(test.replies.size()==1&&test.replies[0].ok&&test.replies[0].value->GetBool("passed"));test.Empty();
 }
 {
  auto changed=Solid(2,2,255,0,0);changed.rgba[0]=0;changed.rgba[2]=255;const auto png=PNG(changed);
  Fixture test;auto config=test.Config(red);config->SetInt("maxDiffPixels",1);config->SetDouble("maxDiffPixelRatio",0.25);test.Start(config);test.Sample(png);test.Sample(png);assert(test.replies.size()==1&&test.replies[0].value->GetBool("passed")&&test.replies[0].value->GetDouble("differentPixels")==1);test.Empty();
  Fixture strict;config=strict.Config(red);config->SetInt("maxDiffPixels",4);config->SetDouble("maxDiffPixelRatio",0.1);strict.Start(config);strict.Sample(png);strict.Sample(png);assert(strict.replies.empty());strict.now=100;strict.controls->Tick();assert(strict.replies.size()==1&&!strict.replies[0].value->GetBool("passed"));strict.captures.clear();strict.Empty();
 }
 {
  Fixture test;auto config=test.Config(red);config->SetInt("maxDiffPixels",100);config->SetDouble("maxDiffPixelRatio",1);test.Start(config);const auto narrow=PNG(Solid(1,2,255,0,0));test.Sample(narrow);test.Sample(narrow);assert(test.replies.empty());test.now=100;test.controls->Tick();assert(!test.replies[0].value->GetBool("dimensionsMatch")&&!test.replies[0].value->GetBool("passed"));test.captures.clear();test.Empty();
 }
 {
  Fixture test;test.Start(test.Config(red));test.Capture(red);assert(test.controls->PhysicalCount()==1);test.controls->Cancel(1,1);assert(test.controls->PhysicalCount()==1&&test.replies.empty());test.CPU();assert(test.controls->PhysicalCount()==1);test.Owner();assert(test.replies.empty());test.Empty();
 }
 {
  // Decode completes physically, but the actual UI image release remains held.
  Fixture test;test.Start(test.Config(red));test.Capture(red);test.CPU();assert(test.controls->PhysicalCount()==1&&test.controls->Bytes()>0&&test.replies.empty());test.controls->PageClosed(1);assert(test.controls->PhysicalCount()==1);test.Owner();test.Empty();assert(test.replies.empty());
 }
 {
  // A real CPU completion at/after the original deadline cannot win before Tick.
  Fixture test;test.Start(test.Config(red));test.Capture(red);test.CPU();test.now=101;test.Owner();assert(test.replies.size()==1&&test.replies[0].value->GetBool("timedOut")&&!test.replies[0].value->GetBool("passed"));test.Empty();
 }
 {
  Fixture test;test.reject_post=true;test.Start(test.Config(red));test.Capture(red);test.CPU();assert(test.controls->PhysicalCount()==1&&test.controls->Bytes()>0);test.controls->Cancel(1,1);test.controls->Tick();assert(test.replies.empty());test.Empty();
 }
 {
  Fixture test;test.Start(test.Config(red));const auto stale=test.captures.front();test.captures.clear();test.controls->Cancel(1,1);test.Start(test.Config(red));auto result=CefDictionaryValue::Create();result->SetString("data",red);assert(!test.controls->Captured(1,1,stale,true,"",result));test.Sample(red);test.Sample(red);assert(test.replies.size()==1&&test.replies[0].value->GetBool("passed"));test.Empty();
 }
 {
  Fixture test;test.reject_background=true;test.Start(test.Config(red));test.Capture(red);assert(test.replies.size()==1&&!test.replies[0].ok&&test.replies[0].error.find("background capacity")!=std::string::npos);test.Empty();
 }
 {
  Fixture test;auto invalid=test.Config(red);invalid->SetDouble("threshold",1.1);test.Start(invalid);assert(test.replies.size()==1&&!test.replies[0].ok);test.Empty();
 }
 {
  // A registry-disabled timeout is normalized to infinity by Source. Native
  // sampling still requires real captures and cancellation retires real work.
  Fixture test;test.Start(test.Config(red),1,1,std::numeric_limits<double>::infinity());test.now=1e12;test.Sample(red);test.Sample(red);assert(test.replies.size()==1&&test.replies[0].value->GetBool("passed"));test.Empty();
 }
 {
  Fixture test;test.Start(test.Config(red),1,1,std::numeric_limits<double>::infinity());test.Capture(red);test.now=1e12;test.controls->Tick();assert(test.replies.empty()&&test.controls->PhysicalCount()==1);test.controls->Cancel(1,1);test.CPU();test.Owner();assert(test.replies.empty());test.Empty();
 }
 {
  // Actual initialized PNG decode above the former wire ceiling, without a
  // second base64 baseline copy. This proves native prepared admission only.
  auto noise=Solid(320,240,0,0,0);uint32_t state=1234567;
  for(size_t i=0;i<noise.rgba.size();++i){state^=state<<13;state^=state>>17;state^=state<<5;noise.rgba[i]=uint8_t(state);}
  const auto encoded=PNG(noise);assert(encoded.size()>65536);
  Fixture test;auto raw=test.Raw(encoded);assert(raw->png.size()>65536);test.StartPrepared(test.Config(""),std::move(raw));test.Sample(encoded);test.Sample(encoded);assert(test.replies.size()==1&&test.replies[0].ok&&test.replies[0].value->GetBool("passed"));test.Empty();
 }
 {
  // One actual file job and one actual image job exhaust their JOINT cap.
  // Neither CPU completion nor logical cancellation retires their UI receipt.
  Fixture test;test.AttachInputs();char path[]="/tmp/suitecut-screenshot-joint-XXXXXX";const int fd=mkstemp(path);assert(fd>=0);auto bytes=test.Raw(red);assert(write(fd,bytes->png.data(),bytes->png.size())==ssize_t(bytes->png.size()));assert(close(fd)==0);
  const suitecut::ScreenshotInputs::Owner owner{1,1,1};std::string error;int files=0;
  const auto file=[&](const char*client,int command){const auto ticket=test.inputs->Prepare(owner,client,true,0,2,100,error);assert(!ticket.token.empty());assert(test.inputs->Append(owner,ticket.token,true,0,{'{','}'},error));assert(test.inputs->Claim(owner,ticket.token,path,command,[&](auto data,double deadline,const std::string&failure){assert(failure.empty()&&deadline==100&&data->png==bytes->png);++files;},error));};
  test.Start(test.Config(red));test.Capture(red);file("first",7);assert(test.background.size()==2&&test.controls->PhysicalCount()==1&&test.inputs->PhysicalCount()==1);
  test.Start(test.Config(red),2,2);test.Capture(red,2,2);file("second",8);assert(test.background.size()==2&&test.controls->PhysicalCount()+test.inputs->PhysicalCount()==2);
  test.CPU();test.controls->Cancel(1,1);test.inputs->Tick();assert(test.background.size()==1&&test.controls->PhysicalCount()+test.inputs->PhysicalCount()==2);
  test.Owner();assert(test.controls->PhysicalCount()==0&&test.inputs->PhysicalCount()==1);test.inputs->Tick();assert(test.inputs->PhysicalCount()==2&&test.background.size()==2);
  test.CPU();test.Owner();assert(files==1&&test.inputs->PhysicalCount()==1);test.controls->Tick();assert(test.controls->PhysicalCount()==1&&test.inputs->PhysicalCount()==1&&test.background.size()==2);
  test.controls->Cancel(2,2);test.inputs->PageClosed(1);assert(test.controls->PhysicalCount()+test.inputs->PhysicalCount()==2);
  while(!test.background.empty()){test.CPU();test.Owner();}
  assert(files==1);assert(unlink(path)==0);assert(test.controls->Bytes()==bytes->reservation->bytes.load());
  bytes.reset();test.Empty();
 }

 {
  Fixture test;test.AttachInputs();std::string error;const suitecut::ScreenshotInputs::Owner owner{1,1,1};
  for(int i=0;i<16;++i)assert(!test.inputs->Prepare(owner,"quota"+std::to_string(i),false,33,2,100,error).token.empty());
  test.Start(test.Config(red));assert(test.replies.size()==1&&!test.replies[0].ok&&!test.controls->LogicalCount()&&test.inputs->LogicalCount()==16);
  test.inputs->Clear();test.Empty();
 }
 {
  Fixture test;test.AttachInputs();std::string error;
  for(int i=1;i<=16;++i)test.Start(test.Config(red),1,i);
  assert(test.controls->LogicalCount()==16&&test.inputs->Prepare({1,1,1},"quota",false,33,2,100,error).token.empty());
  test.controls->PageClosed(1);test.captures.clear();test.Empty();
 }

 {
  // Actual initialized baseline decode precedes every renderer request; the
  // validated raster is reused by subsequent physical sample jobs.
  Fixture test;test.StartObserved(test.Config(red));assert(test.captures.empty()&&test.controls->PhysicalCount()==1);test.CPU();test.Owner();assert(test.settlements==1&&test.observed==0&&test.captures.size()==1);test.Sample(red);test.Sample(red);assert(test.replies.size()==1&&test.replies[0].value->GetBool("passed")&&test.observed==2&&test.proofs.empty());test.Empty();
 }
 {
  Fixture test;test.StartObserved(test.Config("bm90IHBuZw=="));assert(test.captures.empty());test.CPU();test.Owner();assert(test.replies.size()==1&&!test.replies[0].ok&&!test.last_valid&&!test.last_error.empty()&&test.captures.empty()&&!test.observed&&test.proofs.empty());test.Empty();
 }
 {
  // A genuinely admitted decode held past the original clock validates PNGs
  // but cannot compute/adopt a comparison or queue another capture.
  Fixture test;test.StartObserved(test.Config(red));test.CPU();test.Owner();test.Capture(red);test.now=100;test.CPU();assert(test.controls->PhysicalCount()==1&&test.replies.empty());test.Owner();assert(test.replies.size()==1&&test.replies[0].value->GetBool("timedOut")&&!test.replies[0].value->HasKey("differentPixels")&&!test.observed&&test.last_valid&&test.proofs.empty());test.Empty();
 }
 {
  // Real received pixels can wait behind two OTHER native jobs. Freeze moves
  // only those retained bytes into a bounded validation queue, not Received.
  Fixture test;test.StartObserved(test.Config(red));test.CPU();test.Owner();const auto own_sample=test.captures.front();test.captures.pop_front();
  test.Start(test.Config(red),1,2);test.Capture(red,1,2);test.Start(test.Config(red),1,3);test.Capture(red,1,3);assert(test.controls->PhysicalCount()==2);
  auto actual=CefDictionaryValue::Create();actual->SetString("data",red);assert(test.controls->Captured(1,1,own_sample,true,"",actual));test.now=100;test.controls->Deadline(1,1);
  assert(test.unobserved==1&&test.validations==1&&test.controls->PhysicalCount()==2&&!test.observed&&test.proofs.size()==1);
  test.controls->Cancel(1,2);test.controls->Cancel(1,3);
  while(!test.background.empty()){test.CPU();test.Owner();test.controls->Tick();}
  assert(test.last_valid&&test.proofs.empty()&&!test.observed&&test.replies.size()==1&&!test.replies[0].value->HasKey("actual"));test.Empty();
 }
 {
  Fixture test;test.Observe();char path[]="/tmp/suitecut-native-assertion-files-XXXXXX";assert(mkdtemp(path));std::shared_ptr<const suitecut::ScreenshotArtifactPublication>publication;std::string error;
  assert(test.controls->WriteArtifacts(1,1,1,path,red,blue,1000,test.observer,[&](auto result){publication=result;},error));assert(test.controls->PhysicalCount()==1&&!test.controls->ContextReady(1));
  test.CPU();assert(!publication&&test.controls->PhysicalCount()==1);test.controls->Cancel(1,1);test.Owner();
  assert(publication&&publication->committed&&!publication->rollback&&!publication->cleanup_failed&&!test.last_valid&&test.last_error.find("canceled")!=std::string::npos&&test.proofs.empty());
  assert(std::filesystem::is_regular_file(publication->actual_path)&&std::filesystem::is_regular_file(publication->diff_path));test.Empty();assert(std::filesystem::remove_all(path)==4);
 }
 {
  Fixture test;test.Observe();test.reject_background=true;std::shared_ptr<const suitecut::ScreenshotArtifactPublication>publication;std::string error;
  assert(test.controls->WriteArtifacts(1,1,1,"/unused-native-output",red,"",1000,test.observer,[&](auto result){publication=result;},error));assert(publication&&!publication->committed&&!test.last_valid&&!test.last_error.empty()&&test.proofs.empty());test.Empty();
 }
 std::cout<<"PASS 6 initialized screenshot assertion baseline/queued validation/native IO retirement controls; no public Source proof claim\n";

}
