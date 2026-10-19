// SPDX-License-Identifier: MIT
#include "screenshot_pixels.h"
#include <cassert>
#include <cmath>
using namespace suitecut;
ScreenshotRaster Solid(int width,int height,uint8_t r,uint8_t g,uint8_t b,uint8_t a=255){ScreenshotRaster image;image.width=width;image.height=height;for(int i=0;i<width*height;++i)image.rgba.insert(image.rgba.end(),{r,g,b,a});return image;}
int main(){
  const auto red=Solid(2,2,255,0,0);auto actual=red;assert(ScreenshotPixelsEqual(red,actual));auto delta=CompareScreenshotPixels(red,actual,0);assert(delta.dimensions_match&&delta.different==0&&delta.total==4);
  actual.rgba[4]=0;actual.rgba[6]=255;assert(!ScreenshotPixelsEqual(red,actual));delta=CompareScreenshotPixels(red,actual,0.2);assert(delta.different==1&&delta.total==4&&delta.dimensions_match);assert(CompareScreenshotPixels(red,actual,1).different==0);
  const auto white=Solid(1,1,255,255,255);const auto transparent=Solid(1,1,13,71,222,0);assert(CompareScreenshotPixels(white,transparent,0).different==0);assert(!ScreenshotPixelsEqual(white,transparent));
  delta=CompareScreenshotPixels(Solid(2,1,255,0,0),Solid(1,2,255,0,0),1);assert(!delta.dimensions_match&&delta.different==2&&delta.total==3);
  // Crossing extreme valid aspect ratios must compare only the overlap,
  // without allocating or visiting a268M-pixel bounding canvas.
  delta=CompareScreenshotPixels(Solid(16384,128,255,0,0),Solid(128,16384,255,0,0),0);assert(delta.different==2*kScreenshotComparisonPixels-2*128*128&&delta.total==2*kScreenshotComparisonPixels-128*128);
  auto budget=std::make_shared<ScreenshotByteBudget>();std::string error;auto diff=ScreenshotDifferenceImage(red,actual,0.2,budget,error);assert(diff&&error.empty()&&budget->Used()==16);assert(diff->rgba[4]==255&&diff->rgba[5]==0&&diff->rgba[6]==0);diff.reset();assert(budget->Used()==0);
  const std::vector<uint8_t> png{137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,2,0,0,0,2,8,6,0,0,0,114,182,13,36,0,0,0,17,73,68,65,84,120,156,99,248,207,192,240,31,132,25,96,12,0,71,202,7,249,103,89,110,183,0,0,0,0,73,69,78,68,174,66,96,130};int width=0,height=0;assert(ScreenshotPNGDimensions(png,width,height,error)&&width==2&&height==2);
  auto lease=ReserveScreenshotPNG(png,budget,width,height,error);assert(lease&&budget->Used()==48);lease.reset();assert(budget->Used()==0);
  auto tiny=std::make_shared<ScreenshotByteBudget>(47);assert(!ReserveScreenshotPNG(png,tiny,width,height,error)&&error.find("byte budget")!=std::string::npos&&tiny->Used()==0);
  auto malicious=png;malicious[16]=127;malicious[17]=255;malicious[18]=255;malicious[19]=255;assert(!ReserveScreenshotPNG(malicious,budget,width,height,error)&&error.find("dimensions")!=std::string::npos&&budget->Used()==0);
  auto zero=png;zero[16]=zero[17]=zero[18]=zero[19]=0;assert(!ReserveScreenshotPNG(zero,budget,width,height,error)&&budget->Used()==0);
  error.clear();assert(!ScreenshotDifferenceImage(Solid(16384,128,255,0,0),Solid(128,16384,255,0,0),0,budget,error)&&error.find("canvas")!=std::string::npos&&budget->Used()==0);
  auto held=budget->Reserve(64*1024*1024);assert(held&&!budget->Reserve(1));held.reset();assert(budget->Used()==0);
}
