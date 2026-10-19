// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <map>
#include <numeric>
#include <optional>
#include <set>
#include <string_view>
#include <string>
#include <vector>
#include "include/cef_parser.h"
#include "network_post_data.h"
#include "har_multipart.h"

namespace suitecut {
// Native, immutable archive matching. Request bytes come from actual Fetch
// metadata; missing multipart file bytes never become an empty POST match.
class NativeHARReplay {
 public:
  using Dict=CefRefPtr<CefDictionaryValue>;
  struct Match {int status=0;std::string status_text;std::vector<std::pair<std::string,std::string>>headers;std::vector<uint8_t>body;};
  bool Load(Dict archive,std::string&error) {
    auto log=archive?archive->GetDictionary("log"):nullptr;auto entries=log?log->GetList("entries"):nullptr;
    if(!entries||entries->GetSize()>4096){error="Invalid or excessive native HAR entries";return false;}
    std::vector<Entry> parsed;size_t cost=0;
    for(size_t i=0;i<entries->GetSize();++i){
      auto object=entries->GetDictionary(i);auto request=object?object->GetDictionary("request"):nullptr;auto response=object?object->GetDictionary("response"):nullptr;
      if(!request||!response||request->GetString("url").length()>8192||request->GetString("method").empty()||request->GetString("method").length()>64){error="Invalid native HAR request or response";return false;}
      Entry entry;entry.url=request->GetString("url");entry.method=request->GetString("method");entry.match.status=response->GetInt("status");entry.match.status_text=response->GetString("statusText");
      if(entry.match.status<100||entry.match.status>599){continue;} // Failed network entries cannot fulfill a response.
      if(!Headers(request->GetList("headers"),entry.headers,error)||!Headers(response->GetList("headers"),entry.match.headers,error))return false;
      for(auto&[name,value]:entry.headers)name=Lower(std::move(name));
      if(request->GetBool("_postDataUnavailable")){entry.unavailable=true;}
      else if(auto post=request->GetDictionary("postData")){
        entry.post_present=true;if(!Bytes(post,"_encoding",entry.post,error))return false;
      }
      auto content=response->GetDictionary("content");
      if(!content||!content->HasKey("text")){const bool empty_response=entry.method=="HEAD"||entry.match.status==204||entry.match.status==205||entry.match.status==304||(entry.match.status>=300&&entry.match.status<400&&std::any_of(entry.match.headers.begin(),entry.match.headers.end(),[](const auto&header){return Lower(header.first)=="location";}));if(!empty_response)entry.unavailable=true;}
      else if(!Bytes(content,"encoding",entry.match.body,error))return false;
      // Captured bodies are decoded, so forwarding stale wire encodings or
      // lengths would describe a different payload than the actual archive.
      const bool representation_only=entry.method=="HEAD"||entry.match.status==304;
      std::erase_if(entry.match.headers,[representation_only](const auto&header){const auto name=Lower(header.first);return name=="transfer-encoding"||(!representation_only&&(name=="content-length"||name=="content-encoding"));});
      if(!representation_only)entry.match.headers.emplace_back("Content-Length",std::to_string(entry.match.body.size()));
      cost+=entry.url.size()+entry.method.size()+entry.post.size()+entry.match.body.size()+entry.match.status_text.size();
      for(const auto&[name,value]:entry.headers)cost+=name.size()+value.size();
      for(const auto&[name,value]:entry.match.headers)cost+=name.size()+value.size();
      if(cost>32*1048576){error="Native HAR replay retained-byte limit exceeded";return false;}parsed.push_back(std::move(entry));
    }
    // Index existing entries rather than duplicating URLs, methods or bodies.
    // Original entry order is the final key, preserving first-match score ties.
    std::vector<size_t> index(parsed.size());std::iota(index.begin(),index.end(),0);
    std::sort(index.begin(),index.end(),[&](size_t a,size_t b){
      if(parsed[a].url!=parsed[b].url)return parsed[a].url<parsed[b].url;
      if(parsed[a].method!=parsed[b].method)return parsed[a].method<parsed[b].method;
      return a<b;
    });
    entries_=std::move(parsed);index_=std::move(index);bytes_=cost;return true;
  }
  std::optional<Match> Lookup(Dict actual_request)const {
    if(!actual_request)return std::nullopt;
    const auto url=actual_request->GetString("url").ToString(),method=actual_request->GetString("method").ToString();
    const auto begin=std::lower_bound(index_.begin(),index_.end(),std::pair<std::string_view,std::string_view>{url,method},[&](size_t i,const auto&key){
      return entries_[i].url<key.first||(entries_[i].url==key.first&&entries_[i].method<key.second);
    });
    if(begin==index_.end()||entries_[*begin].url!=url||entries_[*begin].method!=method)return std::nullopt;
    auto actual=CaptureNativePostData(actual_request);if(actual.kind==NativePostData::Kind::Unavailable)return std::nullopt;
    std::vector<uint8_t>post=std::move(actual.bytes);
    if(actual.kind==NativePostData::Kind::Text){const auto text=actual_request->GetString("postData").ToString();post.assign(text.begin(),text.end());}
    const Entry*best=nullptr;size_t best_score=0;
    auto observed=actual_request->GetDictionary("headers");
    CefDictionaryValue::KeyList names;
    std::map<std::string,std::set<std::string>> observed_headers;
    if(observed){observed->GetKeys(names);for(const auto&name:names)observed_headers[Lower(name.ToString())].insert(observed->GetString(name).ToString());}
    bool type_checked=false,actual_multipart=false;std::optional<std::string>actual_type;
    const auto multipart=[&](const Entry&entry){
      if(!observed)return false;
      if(!type_checked){
        type_checked=true;
        for(const auto&name:names)if(har_multipart_detail::EqualASCII(name.ToString(),"content-type")){
          if(actual_type||observed->GetType(name)!=VTYPE_STRING){actual_type.reset();return false;}
          actual_type=observed->GetString(name).ToString();
        }
        actual_multipart=actual_type&&har_multipart_detail::ContentType(*actual_type).has_value();
      }
      if(!actual_multipart)return false;
      const std::string*recorded_type=nullptr;
      for(const auto&[name,value]:entry.headers)if(har_multipart_detail::EqualASCII(name,"content-type")){if(recorded_type)return false;recorded_type=&value;}
      if(!recorded_type)return false;
      return NativeHARMultipartEquivalent({reinterpret_cast<const char*>(entry.post.data()),entry.post.size()},*recorded_type,
          {reinterpret_cast<const char*>(post.data()),post.size()},*actual_type);
    };
    for(auto candidate=begin;candidate!=index_.end();++candidate){
      const auto&entry=entries_[*candidate];
      if(entry.url!=url||entry.method!=method)break;
      if(entry.unavailable||(entry.post!=post&&!multipart(entry)))continue;
      size_t score=0;
      for(const auto&[name,value]:entry.headers){const auto found=observed_headers.find(name);if(found!=observed_headers.end()&&found->second.contains(value))++score;}
      if(!best||score>best_score){best=&entry;best_score=score;}
    }
    return best?std::optional<Match>(best->match):std::nullopt;
  }
  size_t size()const{return entries_.size();}size_t bytes()const{return bytes_;}
  void Clear(){entries_.clear();index_.clear();bytes_=0;}
 private:
  struct Entry{std::string url,method;std::vector<std::pair<std::string,std::string>>headers;std::vector<uint8_t>post;Match match;bool post_present=false,unavailable=false;};
  static std::string Lower(std::string text){std::transform(text.begin(),text.end(),text.begin(),[](unsigned char c){return char(std::tolower(c));});return text;}
  static bool Bytes(Dict object,const char*encoding,std::vector<uint8_t>&bytes,std::string&error){
    if(object->GetType("text")!=VTYPE_STRING){error="Native HAR body text is absent";return false;}
    const auto text=object->GetString("text").ToString();const auto format=object->GetString(encoding).ToString();
    if(format.empty()){if(text.size()>2097152){error="Native HAR body exceeds 2 MiB";return false;}bytes.assign(text.begin(),text.end());return true;}
    if(format!="base64"||text.size()>2796204){error="Unsupported or excessive native HAR body encoding";return false;}
    if(text.empty())return true;
    const auto decoded=CefBase64Decode(text);if(!decoded||decoded->GetSize()>2097152){error="Invalid native HAR base64 body";return false;}
    bytes.resize(decoded->GetSize());decoded->GetData(bytes.data(),bytes.size(),0);
    if(CefBase64Encode(bytes.data(),bytes.size())!=text){error="Invalid native HAR base64 body";return false;}return true;
  }
  static bool Headers(CefRefPtr<CefListValue>input,std::vector<std::pair<std::string,std::string>>&headers,std::string&error){
    if(!input||input->GetSize()>512){error="Invalid or excessive native HAR headers";return false;}
    size_t cost=0;for(size_t i=0;i<input->GetSize();++i){auto pair=input->GetDictionary(i);if(!pair||pair->GetType("name")!=VTYPE_STRING||pair->GetType("value")!=VTYPE_STRING){error="Invalid native HAR header";return false;}
      const auto name=pair->GetString("name").ToString(),value=pair->GetString("value").ToString();cost+=name.size()+value.size();
      if(name.empty()||name.find_first_of("\r\n")!=std::string::npos||value.find_first_of("\r\n")!=std::string::npos||name.find('\0')!=std::string::npos||value.find('\0')!=std::string::npos||cost>65536){error="Invalid or excessive native HAR header";return false;}
      headers.emplace_back(name,value);
    }return true;
  }
  std::vector<Entry>entries_;std::vector<size_t>index_;size_t bytes_=0;
};
} // namespace suitecut
