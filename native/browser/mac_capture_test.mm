// SPDX-License-Identifier: MIT
#include "mac_capture.h"
#import <Foundation/Foundation.h>
#import <IOSurface/IOSurface.h>
#include <cassert>
#include <cstring>
int main(){@autoreleasepool {
  NSDictionary* attributes=@{(id)kIOSurfaceWidth:@1920,(id)kIOSurfaceHeight:@1080,(id)kIOSurfaceBytesPerElement:@4,(id)kIOSurfacePixelFormat:@(0x42475241)};
  IOSurfaceRef source=IOSurfaceCreate((__bridge CFDictionaryRef)attributes);assert(source);
  assert(IOSurfaceLock(source,0,nullptr)==kIOReturnSuccess);
  auto* pixels=static_cast<uint8_t*>(IOSurfaceGetBaseAddress(source));
  for(int y=0;y<1080;++y)for(int x=0;x<1920;++x){auto* p=pixels+y*IOSurfaceGetBytesPerRow(source)+x*4;p[0]=3;p[1]=17;p[2]=231;p[3]=255;}
  assert(IOSurfaceUnlock(source,0,nullptr)==kIOReturnSuccess);
  suitecut::MacCapturePool pool;
  std::vector<std::shared_ptr<suitecut::MacCaptureFrame>> owned;
  for(int i=0;i<8;++i){auto frame=pool.Copy(source,0,0,1920,1080);assert(frame);owned.push_back(std::move(frame));}
  assert(!pool.Copy(source,0,0,1920,1080));
  owned.erase(owned.begin());
  auto reused=pool.Copy(source,0,0,1920,1080);assert(reused);
  std::vector<uint8_t> copied;assert(reused->Read(copied));
  assert(copied.size()==1920*1080*4);
  for(size_t i=0;i<copied.size();i+=4)assert(copied[i]==3&&copied[i+1]==17&&copied[i+2]==231&&copied[i+3]==255);
  assert(!pool.Copy(source,-1,0,1920,1080));assert(!pool.Copy(source,0,0,3840,2160));
  suitecut::MacCapturePool yuv_pool;
  auto nv12=yuv_pool.Copy(source,0,0,1920,1080,true);assert(nv12&&nv12->nv12);
  std::vector<uint8_t> planes;assert(nv12->Read(planes));assert(planes.size()==1920*1080*3/2);
  for(size_t i=0;i<1920*1080;++i)assert(planes[i]>=68&&planes[i]<=70);
  for(size_t i=1920*1080;i<planes.size();i+=2)assert(planes[i]>=99&&planes[i]<=101&&planes[i+1]>=222&&planes[i+1]<=224);
  CFRelease(source);
}}
