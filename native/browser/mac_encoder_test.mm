// SPDX-License-Identifier: MIT
#include "mac_capture.h"
#include "mac_encoder.h"
#import <Foundation/Foundation.h>
#import <IOSurface/IOSurface.h>
#include <cassert>
#include <filesystem>
#include <fcntl.h>
#include <unistd.h>
int main(int argc,char**argv){@autoreleasepool {
 assert(argc==2);auto path=std::filesystem::path(argv[1]);std::filesystem::create_directories(path.parent_path());
 int fd=open(path.c_str(),O_CREAT|O_TRUNC|O_WRONLY,0600);assert(fd>=0);
 auto source=IOSurfaceCreate((__bridge CFDictionaryRef)@{(id)kIOSurfaceWidth:@64,(id)kIOSurfaceHeight:@64,(id)kIOSurfaceBytesPerElement:@4,(id)kIOSurfacePixelFormat:@(0x42475241)});assert(source);
 suitecut::MacCapturePool pool;std::shared_ptr<suitecut::MacCaptureFrame> colors[2];
 for(int color=0;color<2;++color){assert(IOSurfaceLock(source,0,nullptr)==kIOReturnSuccess);auto* base=static_cast<uint8_t*>(IOSurfaceGetBaseAddress(source));
  for(int y=0;y<64;++y)for(int x=0;x<64;++x){auto* p=base+y*IOSurfaceGetBytesPerRow(source)+x*4;p[0]=color?255:0;p[1]=0;p[2]=color?0:255;p[3]=255;}
  IOSurfaceUnlock(source,0,nullptr);colors[color]=pool.Copy(source,0,0,64,64,true);assert(colors[color]);}
 {
  suitecut::MacNativeEncoder encoder(fd,64,64,120,8*1024*1024,[]{});
  encoder.Write(colors[0],{},true);encoder.Drain();assert(std::filesystem::file_size(path)==0);
  for(int i=0;i<12;++i)encoder.Write(colors[i%2],{});
  encoder.Drain();assert(std::filesystem::file_size(path)>100);
 }
 close(fd);CFRelease(source);
}}
