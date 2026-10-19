// SPDX-License-Identifier: MIT
#include "screenshot_png_codec.h"
namespace suitecut {
std::shared_ptr<ScreenshotRaster> DecodeScreenshotPNG(const std::vector<uint8_t>& bytes,const std::shared_ptr<ScreenshotByteBudget>& budget,ScreenshotNativeImages& images,std::string& error){
  int width=0,height=0;
  // Reserve native decoded pixels, GetAsBitmap bytes and retained RGBA before
  // AddPNG. The temporary bitmap retires here; native images retire on UI.
  auto reservation=ReserveScreenshotPNG(bytes,budget,width,height,error);if(!reservation)return {};
  auto image=CefImage::CreateImage();if(image)images.owners.push_back({image,reservation,size_t(width)*height*4});if(!image||!image->AddPNG(1,bytes.data(),bytes.size())||image->GetWidth()!=size_t(width)||image->GetHeight()!=size_t(height)){
    error="Native screenshot comparison PNG decoding failed";return {};
  }
  int actual_width=0,actual_height=0;auto bitmap=image->GetAsBitmap(1,CEF_COLOR_TYPE_RGBA_8888,CEF_ALPHA_TYPE_POSTMULTIPLIED,actual_width,actual_height);
  if(!bitmap||actual_width!=width||actual_height!=height||bitmap->GetSize()!=size_t(width)*height*4){error="Native screenshot comparison decoded bitmap dimensions are invalid";return {};}
  auto raster=std::make_shared<ScreenshotRaster>();raster->width=width;raster->height=height;raster->reservation=std::move(reservation);raster->rgba.resize(bitmap->GetSize());
  if(bitmap->GetData(raster->rgba.data(),raster->rgba.size(),0)!=raster->rgba.size()){error="Native screenshot comparison bitmap bytes are unavailable";return {};}
  bitmap=nullptr;reservation.reset();raster->reservation->Shrink(size_t(width)*height*8);
  return raster;
}
std::vector<uint8_t> EncodeScreenshotPNG(const ScreenshotRaster& raster,const std::shared_ptr<ScreenshotByteBudget>& budget,ScreenshotNativeImages& images,std::shared_ptr<ScreenshotByteBudget::Lease>& output,std::string& error){
  if(!ScreenshotRasterValid(raster)){error="Native screenshot comparison cannot encode an invalid raster";return {};}
  auto native=budget->Reserve(size_t(raster.width)*raster.height*4);if(!native){error="Native screenshot comparison64MiB encoding budget exceeded";return {};}
  auto image=CefImage::CreateImage();if(image)images.owners.push_back({image,native,0});if(!image||!image->AddBitmap(1,raster.width,raster.height,CEF_COLOR_TYPE_RGBA_8888,CEF_ALPHA_TYPE_POSTMULTIPLIED,raster.rgba.data(),raster.rgba.size())){error="Native screenshot comparison diff bitmap encoding failed";return {};}
  // PNG stores at most one filter byte per row plus deflate overhead. Reserve
  // a conservative uncompressed bound for native and copied encoded buffers
  // before GetAsPNG allocates either. This may reject a large diff at capacity.
  const size_t encoded_bound=size_t(raster.width)*raster.height*4+raster.height+65536;
  output=budget->Reserve(encoded_bound*2);if(!output){error="Native screenshot comparison64MiB PNG encoding budget exceeded";return {};}
  int width=0,height=0;auto png=image->GetAsPNG(1,false,width,height);if(!png||width!=raster.width||height!=raster.height||png->GetSize()>kScreenshotComparisonPNGBytes){error="Native screenshot comparison diff PNG exceeds2MiB or is unavailable";return {};}
  std::vector<uint8_t> bytes(png->GetSize());if(png->GetData(bytes.data(),bytes.size(),0)!=bytes.size()){error="Native screenshot comparison encoded bytes are unavailable";return {};}
  png=nullptr;output->Shrink(bytes.size());
  return bytes;
}
} // namespace suitecut
