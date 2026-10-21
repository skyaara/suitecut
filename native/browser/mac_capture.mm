// SPDX-License-Identifier: MIT
#include "mac_capture.h"
#include <mach/mach_time.h>
#include <IOSurface/IOSurface.h>
#include <cstring>
#include <functional>

namespace suitecut {
double MacNativeTimeTicksMs() {
  static const auto scale=[] {mach_timebase_info_data_t info;mach_timebase_info(&info);return double(info.numer)/info.denom/1000000.0;}();
  return mach_absolute_time()*scale;
}
bool CopyMacSurface(void* handle, int x, int y, int width, int height,
                    std::vector<uint8_t>& bgra) {
  auto surface = static_cast<IOSurfaceRef>(handle);
  if (!surface || x < 0 || y < 0 || width < 1 || height < 1 || width > 3840 || height > 2160 ||
      static_cast<size_t>(x) + width > IOSurfaceGetWidth(surface) ||
      static_cast<size_t>(y) + height > IOSurfaceGetHeight(surface) ||
      IOSurfaceGetPlaneCount(surface) > 1 || IOSurfaceGetBytesPerElement(surface) != 4)
    return false;
  const auto format = IOSurfaceGetPixelFormat(surface);
  constexpr uint32_t bgra_format = 0x42475241, rgba_format = 0x52474241;
  if (format != bgra_format && format != rgba_format) return false;
  const auto stride = IOSurfaceGetBytesPerRow(surface);
  if (stride < (static_cast<size_t>(x) + width) * 4 ||
      IOSurfaceLock(surface, kIOSurfaceLockReadOnly, nullptr) != kIOReturnSuccess) return false;
  struct Unlock {
    IOSurfaceRef surface;
    ~Unlock() { IOSurfaceUnlock(surface, kIOSurfaceLockReadOnly, nullptr); }
  } unlock{surface};
  const auto* pixels = static_cast<const uint8_t*>(IOSurfaceGetBaseAddress(surface));
  if (pixels) {
    bgra.resize(static_cast<size_t>(width) * height * 4);
    for (int row = 0; row < height; ++row) {
      const auto* source = pixels + (static_cast<size_t>(y) + row) * stride + static_cast<size_t>(x) * 4;
      auto* target = bgra.data() + static_cast<size_t>(row) * width * 4;
      if (format == bgra_format) std::memcpy(target, source, static_cast<size_t>(width) * 4);
      else for (int column = 0; column < width; ++column) {
        target[column * 4] = source[column * 4 + 2]; target[column * 4 + 1] = source[column * 4 + 1];
        target[column * 4 + 2] = source[column * 4]; target[column * 4 + 3] = source[column * 4 + 3];
      }
    }
  }
  return pixels != nullptr;
}
}

#import <Metal/Metal.h>
#import <CoreVideo/CoreVideo.h>
#include <mutex>
namespace suitecut {
struct MacCapturePool::Impl {
  id<MTLDevice> device=MTLCreateSystemDefaultDevice();
  id<MTLCommandQueue> queue=device ? [device newCommandQueue] : nil;
  id<MTLComputePipelineState> converter=nil;
  Impl(){
    if(!device)return;
    static std::once_flag once;
    static id<MTLComputePipelineState> shared_converter=nil;
    std::call_once(once,[this]{
    NSString* code=[NSString stringWithUTF8String:R"metal(
#include <metal_stdlib>
using namespace metal;
kernel void nv12_capture(texture2d<float,access::read> source[[texture(0)]],texture2d<float,access::write> yplane[[texture(1)]],texture2d<float,access::write> uvplane[[texture(2)]],constant uint2& origin[[buffer(0)]],uint2 p[[thread_position_in_grid]]){
 if(p.x>=yplane.get_width()||p.y>=yplane.get_height())return;
 float3 rgb=source.read(p+origin).rgb;
 float yy=16.0/255.0+dot(rgb,float3(.182586,.614231,.062007));
 yplane.write(float4(yy,0,0,1),p);
 if(((p.x|p.y)&1)==0){
   float3 average=(rgb+source.read(p+origin+uint2(1,0)).rgb+source.read(p+origin+uint2(0,1)).rgb+source.read(p+origin+uint2(1,1)).rgb)*.25;
   float u=128.0/255.0+dot(average,float3(-.100644,-.338572,.439216));
   float v=128.0/255.0+dot(average,float3(.439216,-.398942,-.040274));
   uvplane.write(float4(u,v,0,1),p/2);
 }
}
)metal"];
    NSError* error=nil;auto library=[device newLibraryWithSource:code options:nil error:&error];
    if(library)shared_converter=[device newComputePipelineStateWithFunction:[library newFunctionWithName:@"nv12_capture"] error:&error];
    });
    converter=shared_converter;
  }
  std::mutex mutex;
  std::vector<CVPixelBufferRef> free;
  size_t bytes=0;
  ~Impl(){for(auto buffer:free)CVPixelBufferRelease(buffer);}
  static size_t Bytes(CVPixelBufferRef buffer){return IOSurfaceGetAllocSize(CVPixelBufferGetIOSurface(buffer));}
  void Release(CVPixelBufferRef buffer){std::lock_guard lock(mutex);if(free.size()<32)free.push_back(buffer);else{bytes-=Bytes(buffer);CVPixelBufferRelease(buffer);}}
  CVPixelBufferRef Acquire(int width,int height,bool nv12){
    const OSType format=nv12?kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange:kCVPixelFormatType_32BGRA;
    std::lock_guard lock(mutex);
    for(auto it=free.begin();it!=free.end();++it)
      if(CVPixelBufferGetWidth(*it)==size_t(width)&&CVPixelBufferGetHeight(*it)==size_t(height)&&CVPixelBufferGetPixelFormatType(*it)==format){
        auto buffer=*it;free.erase(it);return buffer;
      }
    const size_t needed=size_t(width)*height*(nv12?3:8)/2;
    while(bytes+needed>64ULL*1024*1024&&!free.empty()){
      auto buffer=free.back();free.pop_back();
      bytes-=Bytes(buffer);CVPixelBufferRelease(buffer);
    }
    if(bytes+needed>64ULL*1024*1024)return nullptr;
    CVPixelBufferRef buffer=nullptr;
    NSDictionary* attributes=@{(id)kCVPixelBufferIOSurfacePropertiesKey:@{},(id)kCVPixelBufferMetalCompatibilityKey:@YES};
    if(CVPixelBufferCreate(kCFAllocatorDefault,width,height,format,(__bridge CFDictionaryRef)attributes,&buffer)!=kCVReturnSuccess)return nullptr;
    const size_t actual=Bytes(buffer);
    while(bytes+actual>64ULL*1024*1024&&!free.empty()){
      auto old=free.back();free.pop_back();bytes-=Bytes(old);CVPixelBufferRelease(old);
    }
    if(bytes+actual>64ULL*1024*1024){CVPixelBufferRelease(buffer);return nullptr;}
    bytes+=actual;return buffer;
  }
};
struct MacCaptureFrame::Impl {
  CVPixelBufferRef buffer=nullptr;
  std::function<void(CVPixelBufferRef)> recycle;
  ~Impl(){if(buffer)recycle(buffer);}
};
MacCaptureFrame::MacCaptureFrame():impl_(std::make_unique<Impl>()){}
MacCaptureFrame::~MacCaptureFrame()=default;
void* MacCaptureFrame::PixelBuffer()const{return impl_->buffer;}
bool MacCaptureFrame::Read(std::vector<uint8_t>& bgra)const{
  if(!nv12)return CopyMacSurface(CVPixelBufferGetIOSurface(impl_->buffer),0,0,width,height,bgra);
  if(CVPixelBufferLockBaseAddress(impl_->buffer,kCVPixelBufferLock_ReadOnly)!=kCVReturnSuccess)return false;
  bgra.resize(size_t(width)*height*3/2);size_t offset=0;
  for(int plane=0;plane<2;++plane){
    const auto* source=static_cast<const uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(impl_->buffer,plane));
    const auto stride=CVPixelBufferGetBytesPerRowOfPlane(impl_->buffer,plane);
    const int rows=plane?height/2:height;
    if(!source||stride<size_t(width)){CVPixelBufferUnlockBaseAddress(impl_->buffer,kCVPixelBufferLock_ReadOnly);return false;}
    for(int row=0;row<rows;++row){std::memcpy(bgra.data()+offset,source+row*stride,width);offset+=width;}
  }
  CVPixelBufferUnlockBaseAddress(impl_->buffer,kCVPixelBufferLock_ReadOnly);return true;
}
MacCapturePool::MacCapturePool():impl_(std::make_shared<Impl>()){}
MacCapturePool::~MacCapturePool()=default;
void PrepareMacCapture(){MacCapturePool prepared;}
std::shared_ptr<MacCaptureFrame> MacCapturePool::Copy(void* handle,int x,int y,int width,int height,bool nv12){
 @autoreleasepool {
  auto surface=static_cast<IOSurfaceRef>(handle);
  if(!impl_->queue||!surface||width<1||height<1||width>3840||height>2160||x<0||y<0||
     size_t(x+width)>IOSurfaceGetWidth(surface)||size_t(y+height)>IOSurfaceGetHeight(surface)||
     IOSurfaceGetPixelFormat(surface)!=0x42475241)return {};
  if(nv12&&(!impl_->converter||width%2||height%2))return {};
  auto buffer=impl_->Acquire(width,height,nv12);if(!buffer)return {};
  auto frame=std::shared_ptr<MacCaptureFrame>(new MacCaptureFrame());
  frame->width=width;frame->height=height;frame->nv12=nv12;frame->impl_->buffer=buffer;
  frame->impl_->recycle=[pool=impl_](CVPixelBufferRef value){pool->Release(value);};
  auto descriptor=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:IOSurfaceGetWidth(surface) height:IOSurfaceGetHeight(surface) mipmapped:NO];
  descriptor.usage=MTLTextureUsageShaderRead;descriptor.storageMode=MTLStorageModeShared;
  id<MTLTexture> source=[impl_->device newTextureWithDescriptor:descriptor iosurface:surface plane:0];
  if(!source)return {};
  id<MTLCommandBuffer> command=[impl_->queue commandBuffer];
  if(nv12){
    descriptor.width=width;descriptor.height=height;descriptor.pixelFormat=MTLPixelFormatR8Unorm;descriptor.usage=MTLTextureUsageShaderWrite;
    auto yplane=[impl_->device newTextureWithDescriptor:descriptor iosurface:CVPixelBufferGetIOSurface(buffer) plane:0];
    descriptor.width=width/2;descriptor.height=height/2;descriptor.pixelFormat=MTLPixelFormatRG8Unorm;
    auto uvplane=[impl_->device newTextureWithDescriptor:descriptor iosurface:CVPixelBufferGetIOSurface(buffer) plane:1];
    if(!yplane||!uvplane)return {};
    auto compute=[command computeCommandEncoder];if(!compute)return {};
    [compute setComputePipelineState:impl_->converter];[compute setTexture:source atIndex:0];[compute setTexture:yplane atIndex:1];[compute setTexture:uvplane atIndex:2];
    uint32_t origin[2]{uint32_t(x),uint32_t(y)};[compute setBytes:origin length:sizeof(origin) atIndex:0];
    [compute dispatchThreads:MTLSizeMake(width,height,1) threadsPerThreadgroup:MTLSizeMake(16,8,1)];[compute endEncoding];
  }else{
    descriptor.width=width;descriptor.height=height;
    auto target=[impl_->device newTextureWithDescriptor:descriptor iosurface:CVPixelBufferGetIOSurface(buffer) plane:0];if(!target)return {};
    auto blit=[command blitCommandEncoder];if(!blit)return {};
    [blit copyFromTexture:source sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(x,y,0) sourceSize:MTLSizeMake(width,height,1) toTexture:target destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0,0,0)];[blit endEncoding];
  }
  [command commit];[command waitUntilCompleted];
  if(command.status!=MTLCommandBufferStatusCompleted)return {};
  return frame;
 }
}
}
