// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include "include/cef_api_hash.h"
#include "har_pages.h"
int main(){
 assert(cef_api_hash(CEF_API_VERSION,0));suitecut::NativeHARPages pages;
 assert(pages.Start(1));assert(pages.Created(1,"page:123","page@123",1700000000000));
 assert(pages.Title(1,"page:123","actual title"));assert(pages.Lifecycle(1,"page:123","request",100,1700000000100,"actual-loader-1"));assert(pages.Lifecycle(1,"page:123","init",105,1700000000105,"actual-loader-1"));assert(pages.Lifecycle(1,"page:123","DOMContentLoaded",130,1700000000130,"actual-loader-1"));assert(pages.Lifecycle(1,"page:123","load",140,1700000000140,"actual-loader-1"));
 assert(pages.Reference(1,"page:123")=="page@123"&&pages.Reference(1,"broker:123").empty()&&pages.Reference(2,"page:123").empty());
 auto date=[](double ms){return std::to_string(ms);};auto exported=pages.Export(1,date);auto page=exported->GetDictionary(0);assert(page->GetString("id")=="page@123"&&page->GetString("title")=="actual title"&&page->GetDictionary("pageTimings")->GetDouble("onContentLoad")==30&&page->GetDictionary("pageTimings")->GetDouble("onLoad")==40);assert(page->GetString("startedDateTime")=="1700000000100.000000");
 // Later documents update the actual live page while first recorded loader
 // timings never mix timestamps with a different actual navigation.
 assert(pages.Lifecycle(1,"page:123","init",200,1700000000200,"actual-loader-2"));assert(pages.Lifecycle(1,"page:123","load",220,1700000000220,"actual-loader-2"));assert(pages.Title(1,"page:123","latest actual title"));auto frozen=pages.Export(1,date)->GetDictionary(0)->Copy(false);assert(frozen->GetDictionary("pageTimings")->GetDouble("onLoad")==40&&frozen->GetString("title")=="latest actual title");
 const auto capacity_before=pages.title_capacity();assert(pages.Title(1,"page:123",std::string(8192,'x')));const auto capacity_large=pages.title_capacity();assert(capacity_large>=16384);assert(pages.Title(1,"page:123","latest actual title"));assert(pages.title_capacity()<capacity_large&&pages.title_capacity()<=capacity_before+128);
 pages.Seal(1);assert(pages.Title(1,"page:123","after seal"));assert(pages.Export(1,date)->GetDictionary(0)->GetString("title")=="latest actual title");pages.Stop(1);assert(pages.Start(1));auto current=pages.Export(1,date)->GetDictionary(0)->Copy(false);assert(current->GetDictionary("pageTimings")->GetDouble("onLoad")==20&&current->GetString("title")=="after seal");
 const auto before=pages.bytes();assert(!pages.Title(1,"page:123",std::string(8193,'x'))&&pages.bytes()==before);pages.Detach("page:123");assert(pages.Reference(1,"page:123")=="page@123");pages.Stop(1);assert(pages.bytes()==0);assert(pages.Start(2));assert(pages.Created(2,"page:456","page@456",1700000000300));assert(pages.Reference(1,"page:456").empty());pages.Clear();assert(pages.bytes()==0);
}
