// SPDX-License-Identifier: MIT
#include "mac_encoder.h"
#include "mac_capture.h"
#import <Foundation/Foundation.h>
#import <VideoToolbox/VideoToolbox.h>
#import <CoreVideo/CoreVideo.h>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unistd.h>
namespace suitecut {
struct MacNativeEncoder::Impl {
  struct Pending {std::shared_ptr<MacCaptureFrame> surface;bool preparation=false;};
  struct Packet {std::vector<uint8_t> data;};
  VTCompressionSessionRef session=nullptr;CVPixelBufferPoolRef pool=nullptr;
  int output,width,height;double fps;uint64_t max_bytes,bytes=0,sequence=0,next_packet=0;
  std::function<void()> check;std::mutex mutex;std::condition_variable changed;
  std::map<uint64_t,Pending> pending;std::map<uint64_t,Packet> packets;
  std::string error;
  static void CheckStatus(OSStatus status,const char* operation){if(status)throw std::runtime_error(std::string(operation)+": "+std::to_string(status));}
  static void Append(std::vector<uint8_t>& out,const uint8_t* data,size_t size){
    const uint8_t start[4]{0,0,0,1};out.insert(out.end(),start,start+4);out.insert(out.end(),data,data+size);
  }
  static void Callback(void* context,void* frame_context,OSStatus status,VTEncodeInfoFlags flags,CMSampleBufferRef sample){
    auto& self=*static_cast<Impl*>(context);const uint64_t index=uintptr_t(frame_context)-1;
    std::unique_lock lock(self.mutex);
    auto found=self.pending.find(index);if(found==self.pending.end())return;
    const bool preparation=found->second.preparation;
    auto owner=std::move(found->second.surface);self.pending.erase(found);
    try {
      CheckStatus(status,"Native VideoToolbox frame failed");
      if((flags&kVTEncodeInfo_FrameDropped)||!sample||!CMSampleBufferDataIsReady(sample))throw std::runtime_error("Native VideoToolbox dropped an admitted frame");
      Packet packet;
      if(!preparation){
        auto format=CMSampleBufferGetFormatDescription(sample);size_t parameter_count=0;int length_size=0;
        CheckStatus(CMVideoFormatDescriptionGetH264ParameterSetAtIndex(format,0,nullptr,nullptr,&parameter_count,&length_size),"Native H.264 format failed");
        for(size_t p=0;p<parameter_count;++p){const uint8_t* data=nullptr;size_t size=0;
          CheckStatus(CMVideoFormatDescriptionGetH264ParameterSetAtIndex(format,p,&data,&size,nullptr,nullptr),"Native H.264 parameter failed");Append(packet.data,data,size);}
        auto block=CMSampleBufferGetDataBuffer(sample);const size_t size=CMBlockBufferGetDataLength(block);
        if(size>self.max_bytes||length_size<1||length_size>4)throw std::runtime_error("Invalid native H.264 packet");
        std::vector<uint8_t> raw(size);CheckStatus(CMBlockBufferCopyDataBytes(block,0,size,raw.data()),"Native H.264 copy failed");
        size_t at=0;while(at<raw.size()){
          if(size_t(length_size)>raw.size()-at)throw std::runtime_error("Truncated native H.264 length");
          size_t count=0;for(int i=0;i<length_size;++i)count=(count<<8)|raw[at++];
          if(!count||count>raw.size()-at)throw std::runtime_error("Truncated native H.264 payload");Append(packet.data,raw.data()+at,count);at+=count;
        }
      }
      self.packets.emplace(index,std::move(packet));
      for(;;){auto ready=self.packets.find(self.next_packet);if(ready==self.packets.end())break;
        auto& data=ready->second.data;
        if(data.size()>self.max_bytes-self.bytes)throw std::runtime_error("Native recording video quota exceeded");
        size_t at=0;while(at<data.size()){
          const auto wrote=write(self.output,data.data()+at,data.size()-at);
          if(wrote>0)at+=size_t(wrote);else if(wrote<0&&errno==EINTR)continue;else throw std::runtime_error("Native H.264 spool write failed");
        }
        self.bytes+=data.size();self.packets.erase(ready);++self.next_packet;
      }
    }catch(const std::exception& failure){self.error=failure.what();}
    lock.unlock();self.changed.notify_all();
  }
  Impl(int fd,int w,int h,double rate,uint64_t limit,std::function<void()> guard):output(fd),width(w),height(h),fps(rate),max_bytes(limit),check(std::move(guard)){
    @autoreleasepool {
      NSDictionary* spec=@{(id)kVTVideoEncoderSpecification_RequireHardwareAcceleratedVideoEncoder:@YES};
      NSDictionary* attributes=@{(id)kCVPixelBufferWidthKey:@(w),(id)kCVPixelBufferHeightKey:@(h),(id)kCVPixelBufferPixelFormatTypeKey:@(kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange),(id)kCVPixelBufferIOSurfacePropertiesKey:@{}};
      CheckStatus(CVPixelBufferPoolCreate(kCFAllocatorDefault,nullptr,(__bridge CFDictionaryRef)attributes,&pool),"Native pixel pool failed");
      try {
        CheckStatus(VTCompressionSessionCreate(kCFAllocatorDefault,w,h,kCMVideoCodecType_H264,(__bridge CFDictionaryRef)spec,nullptr,nullptr,Callback,this,&session),"Native hardware encoder creation failed");
        NSDictionary* properties=@{(id)kVTCompressionPropertyKey_RealTime:@YES,(id)kVTCompressionPropertyKey_AllowFrameReordering:@NO,(id)kVTCompressionPropertyKey_MaxFrameDelayCount:@1,(id)kVTCompressionPropertyKey_ExpectedFrameRate:@(rate),(id)kVTCompressionPropertyKey_AverageBitRate:@24000000,(id)kVTCompressionPropertyKey_MaxKeyFrameInterval:@240,(id)kVTCompressionPropertyKey_ProfileLevel:(id)kVTProfileLevel_H264_High_AutoLevel,(id)kVTCompressionPropertyKey_ColorPrimaries:(id)kCVImageBufferColorPrimaries_ITU_R_709_2,(id)kVTCompressionPropertyKey_TransferFunction:(id)kCVImageBufferTransferFunction_sRGB,(id)kVTCompressionPropertyKey_YCbCrMatrix:(id)kCVImageBufferYCbCrMatrix_ITU_R_709_2};
        CheckStatus(VTSessionSetProperties(session,(__bridge CFDictionaryRef)properties),"Native hardware encoder configuration failed");
        CFTypeRef hardware=nullptr;CheckStatus(VTSessionCopyProperty(session,kVTCompressionPropertyKey_UsingHardwareAcceleratedVideoEncoder,kCFAllocatorDefault,&hardware),"Native hardware verification failed");
        bool actual=hardware&&CFEqual(hardware,kCFBooleanTrue);if(hardware)CFRelease(hardware);if(!actual)throw std::runtime_error("Native H.264 hardware is unavailable");
        CheckStatus(VTCompressionSessionPrepareToEncodeFrames(session),"Native hardware preparation failed");
      }catch(...){if(session){VTCompressionSessionInvalidate(session);CFRelease(session);session=nullptr;}CFRelease(pool);pool=nullptr;throw;}
    }
  }
  ~Impl(){if(session){VTCompressionSessionInvalidate(session);CFRelease(session);}if(pool)CFRelease(pool);}
  void Health(){check();std::lock_guard lock(mutex);if(!error.empty())throw std::runtime_error(error);}
  void Write(const std::shared_ptr<MacCaptureFrame>& surface,const std::vector<uint8_t>& data,bool preparation){
    Health();
    {std::unique_lock lock(mutex);while(pending.size()+packets.size()>=4){lock.unlock();Health();lock.lock();changed.wait_for(lock,std::chrono::milliseconds(10));}}
    CVPixelBufferRef image=surface?static_cast<CVPixelBufferRef>(surface->PixelBuffer()):nullptr;
    CVPixelBufferRef copied=nullptr;
    if(!image){
      if(data.size()!=size_t(width)*height*3/2)throw std::runtime_error("Invalid native NV12 canvas");
      CheckStatus(CVPixelBufferPoolCreatePixelBuffer(kCFAllocatorDefault,pool,&copied),"Native canvas allocation failed");
      CheckStatus(CVPixelBufferLockBaseAddress(copied,0),"Native canvas lock failed");size_t offset=0;
      for(int plane=0;plane<2;++plane){auto* dest=static_cast<uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(copied,plane));const auto stride=CVPixelBufferGetBytesPerRowOfPlane(copied,plane);for(int row=0;row<(plane?height/2:height);++row){std::memcpy(dest+row*stride,data.data()+offset,width);offset+=width;}}
      CVPixelBufferUnlockBaseAddress(copied,0);image=copied;
    }
    const auto index=sequence++;
    {std::lock_guard lock(mutex);pending.emplace(index,Pending{surface,preparation});}
    NSDictionary* frame_options=(preparation||index==1)?@{(id)kVTEncodeFrameOptionKey_ForceKeyFrame:@YES}:nil;
    VTEncodeInfoFlags info=0;
    auto status=VTCompressionSessionEncodeFrame(session,image,CMTimeMake(std::llround(index*1000000.0/fps),1000000),CMTimeMake(std::llround(1000000.0/fps),1000000),(__bridge CFDictionaryRef)frame_options,reinterpret_cast<void*>(uintptr_t(index+1)),&info);
    if(copied)CVPixelBufferRelease(copied);
    if(status){std::lock_guard lock(mutex);pending.erase(index);CheckStatus(status,"Native H.264 submit failed");}
    if(info&kVTEncodeInfo_FrameDropped)throw std::runtime_error("Native H.264 rejected an admitted frame");
    Health();
  }
  void Drain(){Health();CheckStatus(VTCompressionSessionCompleteFrames(session,kCMTimeInvalid),"Native hardware drain failed");for(;;){{std::unique_lock lock(mutex);if(pending.empty()&&packets.empty())break;changed.wait_for(lock,std::chrono::milliseconds(10));}Health();}Health();}
};
MacNativeEncoder::MacNativeEncoder(int fd,int width,int height,double fps,uint64_t max_bytes,std::function<void()> check):impl_(std::make_unique<Impl>(fd,width,height,fps,max_bytes,std::move(check))){}
MacNativeEncoder::~MacNativeEncoder()=default;
void MacNativeEncoder::Write(const std::shared_ptr<MacCaptureFrame>& surface,const std::vector<uint8_t>& nv12,bool preparation){impl_->Write(surface,nv12,preparation);}
void MacNativeEncoder::Drain(){impl_->Drain();}
}
