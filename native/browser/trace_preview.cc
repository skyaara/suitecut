// SPDX-License-Identifier: MIT
#include "trace_preview.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <string>
#include <utility>
#include "include/cef_image.h"
#include "libyuv/scale_argb.h"

namespace suitecut {
struct TracePreview::Pending {
  TraceJournal::Lease lease;int page,css_width,css_height,width,height;double time;
  uint64_t id;std::vector<uint8_t> bgra,jpeg;std::string error;
  std::atomic_bool complete=false;bool canceled=false;
};
bool TracePreview::Paint(TraceJournal::Lease lease,int page,int css_width,int css_height,double now,const void* bgra,int width,int height){
  if(!lease.context||!page||!bgra||css_width<1||css_height<1||width<1||height<1||width>16384||height>16384||uint64_t(width)*height>67108864||!std::isfinite(now)||now<0||generation_>=9007199254740991ULL)return false;
  // A maximum two native encoders bounds copied paint bytes and backpressure.
  // Sampling never changes engine capture FPS or the real author viewport.
  if(pending_.size()>=2)return false;
  const auto last=last_.find(page);if(last!=last_.end()&&last->second.session==lease.session&&last->second.chunk==lease.chunk&&now-last->second.time<100)return false;
  auto pending=std::make_shared<Pending>();pending->lease=lease;pending->page=page;pending->css_width=css_width;pending->css_height=css_height;pending->width=width;pending->height=height;pending->time=now;pending->id=++generation_;
  pending->bgra.resize(size_t(width)*height*4);std::memcpy(pending->bgra.data(),bgra,pending->bgra.size());pending_[pending->id]=pending;last_[page]={lease.session,lease.chunk,now};
  const auto weak=weak_from_this();const auto post=hooks_.owner;
  bool submitted=false;try{submitted=hooks_.background([weak,pending,post]{
    try{
      const double scale=std::min({800.0/pending->css_width,800.0/pending->css_height,1.0});const int width=std::max(1,int(std::lround(pending->css_width*scale))),height=std::max(1,int(std::lround(pending->css_height*scale)));
      std::vector<uint8_t> pixels(size_t(width)*height*4);
      const bool scaled=libyuv::ARGBScale(pending->bgra.data(),pending->width*4,pending->width,pending->height,pixels.data(),width*4,width,height,libyuv::kFilterBox)==0;
      auto image=CefImage::CreateImage();
      if(!scaled||!image||!image->AddBitmap(1,width,height,CEF_COLOR_TYPE_BGRA_8888,CEF_ALPHA_TYPE_PREMULTIPLIED,pixels.data(),pixels.size()))pending->error="Native trace preview bitmap encoding failed";
      else{int actual_width=0,actual_height=0;auto jpeg=image->GetAsJPEG(1,90,actual_width,actual_height);if(!jpeg||actual_width!=width||actual_height!=height||jpeg->GetSize()>8*1024*1024)pending->error="Native trace preview JPEG encoding failed";else{pending->jpeg.resize(jpeg->GetSize());if(jpeg->GetData(pending->jpeg.data(),pending->jpeg.size(),0)!=pending->jpeg.size())pending->error="Native trace preview bytes unavailable";}}
    }catch(...){pending->error="Native trace preview encoding failed";}
    pending->bgra.clear();pending->complete.store(true,std::memory_order_release);
    try{post([weak,id=pending->id]{if(const auto self=weak.lock())self->Finish(id);});}catch(...){/* Native owner Tick drains a failed completion post. */}
  });}catch(...){submitted=false;}
  if(!submitted){pending_.erase(pending->id);last_.erase(page);return false;}if(hooks_.wake)hooks_.wake();return true;
}
void TracePreview::Finish(uint64_t id){const auto found=pending_.find(id);if(found==pending_.end()||!found->second->complete.load(std::memory_order_acquire))return;const auto pending=found->second;pending_.erase(found);if(hooks_.wake)hooks_.wake();if(pending->canceled)return;if(!pending->error.empty()){hooks_.error(pending->lease,pending->error);return;}
  auto event=CefDictionaryValue::Create();event->SetString("type","screencast-frame");event->SetString("pageId","page@"+std::to_string(pending->page));event->SetDouble("timestamp",pending->time);event->SetInt("width",pending->css_width);event->SetInt("height",pending->css_height);const auto file="resources/preview-"+std::to_string(pending->lease.session)+"-"+std::to_string(pending->lease.chunk)+"-"+std::to_string(pending->id)+".jpeg";event->SetString("file",file);hooks_.captured(pending->lease,event,file,std::move(pending->jpeg));
}
bool TracePreview::HasPendingForContext(int context) const{for(const auto& [id,pending]:pending_)if(pending->lease.context==context)return true;return false;}
void TracePreview::Tick(){std::vector<uint64_t> finished;for(const auto& [id,pending]:pending_)if(pending->complete.load(std::memory_order_acquire))finished.push_back(id);for(const auto id:finished)Finish(id);}
void TracePreview::PageClosed(int page){last_.erase(page);for(const auto& [id,pending]:pending_)if(pending->page==page)pending->canceled=true;}
void TracePreview::Clear(){last_.clear();for(const auto& [id,pending]:pending_)pending->canceled=true;}
} // namespace suitecut
