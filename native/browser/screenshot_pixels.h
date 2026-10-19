// SPDX-License-Identifier: MIT
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <utility>
namespace suitecut {
// One engine budget accounts for all retained comparison inputs and rasters.
class ScreenshotByteBudget:public std::enable_shared_from_this<ScreenshotByteBudget>{
 public:
  struct Lease{
    std::shared_ptr<ScreenshotByteBudget> owner;std::atomic_size_t bytes=0;
    ~Lease(){if(owner)owner->used_.fetch_sub(bytes.load(std::memory_order_acquire),std::memory_order_acq_rel);}
    void Shrink(size_t remaining){auto old=bytes.load(std::memory_order_acquire);while(old>remaining&&!bytes.compare_exchange_weak(old,remaining,std::memory_order_acq_rel)){}if(old>remaining)owner->used_.fetch_sub(old-remaining,std::memory_order_acq_rel);}
  };
  explicit ScreenshotByteBudget(size_t limit=64*1024*1024):limit_(limit){}
  std::shared_ptr<Lease> Reserve(size_t bytes){
    auto lease=std::make_shared<Lease>();auto self=shared_from_this();
    auto used=used_.load(std::memory_order_acquire);
    do{if(bytes>limit_||used>limit_-bytes)return {};}while(!used_.compare_exchange_weak(used,used+bytes,std::memory_order_acq_rel));
    lease->owner=std::move(self);lease->bytes=bytes;return lease;
  }
  size_t Used()const{return used_.load(std::memory_order_acquire);}
 private:const size_t limit_;std::atomic_size_t used_=0;
};
struct ScreenshotRaster{
  int width=0,height=0;
  std::vector<uint8_t> rgba;
  std::shared_ptr<ScreenshotByteBudget::Lease> reservation;
};
struct ScreenshotPixelDelta{bool dimensions_match=false;size_t different=0,total=0;};
constexpr size_t kScreenshotComparisonPixels=2*1024*1024;
constexpr size_t kScreenshotComparisonPNGBytes=2*1024*1024;
// Read IHDR and enforce integer dimensions/byte limits before native decoding.
bool ScreenshotPNGDimensions(const std::vector<uint8_t>& bytes,int& width,int& height,std::string& error);
std::shared_ptr<ScreenshotByteBudget::Lease> ReserveScreenshotPNG(const std::vector<uint8_t>& bytes,const std::shared_ptr<ScreenshotByteBudget>& budget,int& width,int& height,std::string& error);
bool ScreenshotRasterValid(const ScreenshotRaster& raster);
bool ScreenshotPixelsEqual(const ScreenshotRaster& left,const ScreenshotRaster& right);
// Alpha is composited over white; threshold is normalized Euclidean Oklab
// distance, clamped to[0,1]. Dimensions must match independently of tolerances.
ScreenshotPixelDelta CompareScreenshotPixels(const ScreenshotRaster& expected,const ScreenshotRaster& actual,double threshold);
std::shared_ptr<ScreenshotRaster> ScreenshotDifferenceImage(const ScreenshotRaster& expected,const ScreenshotRaster& actual,double threshold,const std::shared_ptr<ScreenshotByteBudget>& budget,std::string& error);
} // namespace suitecut
