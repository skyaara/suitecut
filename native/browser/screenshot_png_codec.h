// SPDX-License-Identifier: MIT
#pragma once
#include <cassert>
#include "screenshot_pixels.h"
#include "include/cef_image.h"
#include "include/cef_task.h"
namespace suitecut {
// CPU-created native image wrappers are held until owner UI explicitly destroys
// the references. Their decoded budget remains reserved through this real release.
struct ScreenshotNativeImages{
  struct Owner{CefRefPtr<CefImage> image;std::shared_ptr<ScreenshotByteBudget::Lease> reservation;size_t raster_bytes=0;};
  std::vector<Owner> owners;
  void ReleaseOnUI(){
    if(owners.empty())return;
    assert(CefCurrentlyOn(TID_UI));
    for(auto& owner:owners){owner.image=nullptr;if(owner.reservation)owner.reservation->Shrink(owner.raster_bytes);}
    owners.clear();
  }
};
std::shared_ptr<ScreenshotRaster> DecodeScreenshotPNG(const std::vector<uint8_t>& bytes,const std::shared_ptr<ScreenshotByteBudget>& budget,ScreenshotNativeImages& images,std::string& error);
std::vector<uint8_t> EncodeScreenshotPNG(const ScreenshotRaster& raster,const std::shared_ptr<ScreenshotByteBudget>& budget,ScreenshotNativeImages& images,std::shared_ptr<ScreenshotByteBudget::Lease>& output,std::string& error);
} // namespace suitecut
