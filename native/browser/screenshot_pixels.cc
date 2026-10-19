// SPDX-License-Identifier: MIT
#include "screenshot_pixels.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
namespace suitecut {
bool ScreenshotRasterValid(const ScreenshotRaster& raster){return raster.width>0&&raster.height>0&&raster.width<=16384&&raster.height<=16384&&size_t(raster.width)*raster.height<=kScreenshotComparisonPixels&&raster.rgba.size()==size_t(raster.width)*raster.height*4;}
namespace {
uint32_t BigEndian(const uint8_t* value){return uint32_t(value[0])<<24|uint32_t(value[1])<<16|uint32_t(value[2])<<8|value[3];}
std::array<double,3> Lab(const uint8_t* rgba){
  const double alpha=rgba[3]/255.0;
  auto linear=[alpha](uint8_t byte){const double channel=(byte/255.0)*alpha+1-alpha;return channel<=0.04045?channel/12.92:std::pow((channel+0.055)/1.055,2.4);};
  const double r=linear(rgba[0]),g=linear(rgba[1]),b=linear(rgba[2]);
  // Standard Oklab color-space transform (CSS Color 4), not a vendor
  // screenshot comparator. Public threshold semantics are documented explicitly.
  const double l=std::cbrt(0.4122214708*r+0.5363325363*g+0.0514459929*b);
  const double m=std::cbrt(0.2119034982*r+0.6806995451*g+0.1073969566*b);
  const double s=std::cbrt(0.0883024619*r+0.2817188376*g+0.6299787005*b);
  return {0.2104542553*l+0.7936177850*m-0.0040720468*s,1.9779984951*l-2.4285922050*m+0.4505937099*s,0.0259040371*l+0.7827717662*m-0.8086757660*s};
}
bool Different(const uint8_t* left,const uint8_t* right,double threshold){
  if(std::memcmp(left,right,4)==0)return false;
  const auto a=Lab(left),b=Lab(right);double distance=0;for(size_t i=0;i<3;++i)distance+=(a[i]-b[i])*(a[i]-b[i]);
  return std::min(1.0,std::sqrt(distance))>threshold;
}
const uint8_t* Pixel(const ScreenshotRaster& image,int x,int y){return x<image.width&&y<image.height?image.rgba.data()+(size_t(y)*image.width+x)*4:nullptr;}
}
bool ScreenshotPNGDimensions(const std::vector<uint8_t>& bytes,int& width,int& height,std::string& error){
  static const uint8_t signature[]={137,80,78,71,13,10,26,10};
  if(bytes.size()<33||bytes.size()>kScreenshotComparisonPNGBytes||std::memcmp(bytes.data(),signature,8)||BigEndian(bytes.data()+8)!=13||std::memcmp(bytes.data()+12,"IHDR",4)){
    error="Screenshot comparison requires a genuine PNG of at most2MiB with a valid IHDR";return false;
  }
  const auto w=BigEndian(bytes.data()+16),h=BigEndian(bytes.data()+20);
  if(!w||!h||w>16384||h>16384||uint64_t(w)*h>kScreenshotComparisonPixels){error="Screenshot comparison PNG dimensions exceed16384 or2M pixels";return false;}
  // Reject invalid IHDR methods before any decoder can allocate.
  // The native decoder independently validates the encoded image data.
  if(bytes[26]!=0||bytes[27]!=0||bytes[28]>1){error="Screenshot comparison PNG IHDR encoding is invalid";return false;}
  width=int(w);height=int(h);return true;
}
std::shared_ptr<ScreenshotByteBudget::Lease> ReserveScreenshotPNG(const std::vector<uint8_t>& bytes,const std::shared_ptr<ScreenshotByteBudget>& budget,int& width,int& height,std::string& error){
  if(!ScreenshotPNGDimensions(bytes,width,height,error))return {};
  auto lease=budget->Reserve(size_t(width)*height*12);if(!lease)error="Native screenshot comparison64MiB byte budget exceeded";return lease;
}
bool ScreenshotPixelsEqual(const ScreenshotRaster& left,const ScreenshotRaster& right){return ScreenshotRasterValid(left)&&ScreenshotRasterValid(right)&&left.width==right.width&&left.height==right.height&&left.rgba==right.rgba;}
ScreenshotPixelDelta CompareScreenshotPixels(const ScreenshotRaster& expected,const ScreenshotRaster& actual,double threshold){
  ScreenshotPixelDelta result;if(!ScreenshotRasterValid(expected)||!ScreenshotRasterValid(actual)||!std::isfinite(threshold)||threshold<0||threshold>1)return result;
  const int width=std::min(expected.width,actual.width),height=std::min(expected.height,actual.height);const size_t overlap=size_t(width)*height;
  const size_t left=size_t(expected.width)*expected.height,right=size_t(actual.width)*actual.height;
  result.dimensions_match=expected.width==actual.width&&expected.height==actual.height;result.total=left+right-overlap;result.different=left+right-2*overlap;
  // Walk only the true overlap, bounded by2M pixels. Extreme opposite aspect
  // ratios never expand comparison work to the much larger bounding canvas.
  for(int y=0;y<height;++y)for(int x=0;x<width;++x)if(Different(Pixel(expected,x,y),Pixel(actual,x,y),threshold))++result.different;
  return result;
}
std::shared_ptr<ScreenshotRaster> ScreenshotDifferenceImage(const ScreenshotRaster& expected,const ScreenshotRaster& actual,double threshold,const std::shared_ptr<ScreenshotByteBudget>& budget,std::string& error){
  if(!ScreenshotRasterValid(expected)||!ScreenshotRasterValid(actual)||!std::isfinite(threshold)||threshold<0||threshold>1){error="Native screenshot difference requires valid bounded rasters and threshold";return {};}
  const int width=std::max(expected.width,actual.width),height=std::max(expected.height,actual.height);
  if(size_t(width)*height>kScreenshotComparisonPixels){error="Native screenshot difference canvas exceeds2M pixels";return {};}
  auto lease=budget->Reserve(size_t(width)*height*4);if(!lease){error="Native screenshot comparison64MiB byte budget exceeded";return {};}
  auto raster=std::make_shared<ScreenshotRaster>();raster->width=width;raster->height=height;raster->reservation=std::move(lease);raster->rgba.resize(size_t(width)*height*4);
  for(int y=0;y<height;++y)for(int x=0;x<width;++x){const auto a=Pixel(expected,x,y),b=Pixel(actual,x,y);auto* pixel=raster->rgba.data()+(size_t(y)*width+x)*4;
    if((a&&!b)||(!a&&b)||(a&&b&&Different(a,b,threshold))){pixel[0]=a?255:0;pixel[1]=0;pixel[2]=b?0:255;}else{const int gray=b?(int(b[0])*2126+int(b[1])*7152+int(b[2])*722)/10000:255;pixel[0]=pixel[1]=pixel[2]=uint8_t(128+gray/2);}pixel[3]=255;}
  return raster;
}
} // namespace suitecut
