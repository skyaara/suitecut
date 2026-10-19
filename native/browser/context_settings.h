// SPDX-License-Identifier: MIT
#pragma once
#include "context_permissions.h"
#include "context_authentication_controls.h"
#include <cmath>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "include/cef_parser.h"
#include "include/cef_values.h"

namespace suitecut {
// Browser-thread configuration, never renderer-authored data. Settings are
// validated before any public CefBrowser is reserved and retained per profile.
struct ContextSettings {
  using Dict=CefRefPtr<CefDictionaryValue>;
  struct Size { int width=0,height=0; };
  using Auth=NativeContextCredential;
  struct Proxy{std::string server,bypass,host,username,password;int port=0;bool authentication=false;};
  std::optional<Proxy> proxy;
  std::optional<Auth> http_credentials;
  std::vector<Auth> http_credentials_all;
  std::vector<std::string> permissions;
  struct Geo { double latitude=0,longitude=0,accuracy=0; };
  std::optional<Size> viewport,screen;
  std::optional<Geo> geolocation;
  bool no_fixed_viewport=false;
  std::optional<double> device_scale;
  std::optional<bool> has_touch,is_mobile,ignore_https_errors;
  std::optional<std::string> locale,timezone,user_agent,color_scheme,reduced_motion,forced_colors,contrast;
  struct Step { std::string method; Dict params; };
  static std::string CanonicalOrigin(const std::string&url){
    CefURLParts parts;if(!CefParseURL(url,parts))return "";const auto scheme=CefString(&parts.scheme).ToString(),host=CefString(&parts.host).ToString(),port=CefString(&parts.port).ToString();
    if((scheme!="http"&&scheme!="https")||host.empty())return "";
    return scheme+"://"+host+((port.empty()||(scheme=="http"&&port=="80")||(scheme=="https"&&port=="443"))?"":":"+port);
  }
  const Auth* SelectHTTPCredential(const std::string&url)const{
    const auto origin=CanonicalOrigin(url);
    for(const auto&auth:http_credentials_all)if(!auth.origin.empty()&&auth.origin==origin)return &auth;
    for(const auto&auth:http_credentials_all)if(auth.origin.empty())return &auth;
    if(http_credentials_all.empty()&&http_credentials&&(http_credentials->origin.empty()||http_credentials->origin==origin))return &*http_credentials;
    return nullptr;
  }
  void WriteHTTPCredentials(Dict params)const{
    auto rows=CefListValue::Create();const auto add=[&](const Auth&auth){auto row=CefDictionaryValue::Create();row->SetString("username",auth.username);row->SetString("password",auth.password);if(!auth.origin.empty())row->SetString("origin",auth.origin);row->SetString("send",auth.always?"always":"unauthorized");rows->SetDictionary(rows->GetSize(),row);};
    if(http_credentials_all.empty()){if(http_credentials)add(*http_credentials);}else for(const auto&auth:http_credentials_all)add(auth);
    params->SetList("credentials",rows);
  }
  bool BootstrapRequired()const{return RendererOverrides()||http_credentials||(proxy&&proxy->authentication);}
  bool RendererOverrides() const {
    return ignore_https_errors.value_or(false)||viewport||screen||device_scale||has_touch||is_mobile||locale||timezone||user_agent||geolocation||color_scheme||reduced_motion||forced_colors||contrast;
  }
  bool CaptureDimensions(int width,int height,double scale,std::string&error) const {
    const auto size=viewport.value_or(Size{width,height});const double ratio=device_scale.value_or(scale);
    if(size.width<1||size.height<1||!std::isfinite(ratio)||ratio<=0||std::ceil(size.width*ratio)>3840||std::ceil(size.height*ratio)>2160){error="Native context physical capture dimensions exceed 3840x2160";return false;}return true;
  }
  Dict Media() const {
    if(!color_scheme&&!reduced_motion&&!forced_colors&&!contrast)return nullptr;
    auto output=CefDictionaryValue::Create();auto features=CefListValue::Create();size_t index=0;
    const auto add=[&](const char*name,const std::optional<std::string>&value){if(value){auto item=CefDictionaryValue::Create();item->SetString("name",name);item->SetString("value",*value);features->SetDictionary(index++,item);}};
    add("prefers-color-scheme",color_scheme);add("prefers-reduced-motion",reduced_motion);add("forced-colors",forced_colors);add("prefers-contrast",contrast);
    output->SetString("media","");output->SetList("features",features);return output;
  }
  // A page resize changes the viewport, not the context's display/mobile
  // policy. Use the same native metrics for bootstrap and subsequent resizes.
  Dict ViewportMetrics(Size view,double fallback_scale) const {
    const Size display=screen.value_or(view);
    auto params=CefDictionaryValue::Create();
    params->SetInt("width",view.width);params->SetInt("height",view.height);
    params->SetInt("screenWidth",display.width);params->SetInt("screenHeight",display.height);
    params->SetDouble("deviceScaleFactor",device_scale.value_or(fallback_scale));
    params->SetBool("mobile",is_mobile.value_or(false));
    return params;
  }
  std::vector<Step> Plan(bool main,int fallback_width,int fallback_height,double fallback_scale) const {
    std::vector<Step> steps;
    // CEF's certificate callback covers main navigation only. The genuine
    // Security handler also covers cross-site frame and subresource requests;
    // its acknowledgement precedes authored context initialization.
    if(main&&ignore_https_errors.value_or(false)){auto p=CefDictionaryValue::Create();p->SetBool("ignore",true);steps.push_back({"Security.setIgnoreCertificateErrors",p});}
    if(locale){auto p=CefDictionaryValue::Create();p->SetString("locale",*locale);steps.push_back({"Emulation.setLocaleOverride",p});}
    if(timezone){auto p=CefDictionaryValue::Create();p->SetString("timezoneId",*timezone);steps.push_back({"Emulation.setTimezoneOverride",p});}
    // acceptLanguage is an actual native UA override, affecting navigator and
    // outgoing HTTP. Locale-only configuration still retains the native UA.
    if(user_agent||locale){auto p=CefDictionaryValue::Create();p->SetString("userAgent",user_agent.value_or(""));if(locale)p->SetString("acceptLanguage",*locale);steps.push_back({"Emulation.setUserAgentOverride",p});}
    if(has_touch){auto p=CefDictionaryValue::Create();p->SetBool("enabled",*has_touch);if(*has_touch)p->SetInt("maxTouchPoints",1);steps.push_back({"Emulation.setTouchEmulationEnabled",p});}
    if(main&&(viewport||screen||device_scale||is_mobile)){
      steps.push_back({"Emulation.setDeviceMetricsOverride",ViewportMetrics(viewport.value_or(Size{fallback_width,fallback_height}),fallback_scale)});
    }
    if(geolocation){auto p=CefDictionaryValue::Create();p->SetDouble("latitude",geolocation->latitude);p->SetDouble("longitude",geolocation->longitude);p->SetDouble("accuracy",geolocation->accuracy);steps.push_back({"Emulation.setGeolocationOverride",p});}
    if(const auto media=Media())steps.push_back({"Emulation.setEmulatedMedia",media});
    return steps;
  }
  static bool Parse(Dict input,ContextSettings&output,std::string&error){
    if(!input){error="Native context settings must be a dictionary";return false;}
    auto wrapped=CefValue::Create();wrapped->SetDictionary(input);const auto encoded=CefWriteJSON(wrapped,JSON_WRITER_DEFAULT);if(encoded.size()>65536){error="Native context settings exceed 64 KiB";return false;}
    ContextSettings parsed;
    parsed.no_fixed_viewport=input->HasKey("viewport")&&input->GetType("viewport")==VTYPE_NULL;
    auto text=[&](const char*key,std::optional<std::string>&target,size_t limit,bool nullable=false){
      if(!input->HasKey(key))return true;
      if(nullable&&input->GetType(key)==VTYPE_NULL){target="";return true;}
      if(input->GetType(key)!=VTYPE_STRING){error=std::string("Native context ")+key+" must be a string";return false;}
      const auto value=input->GetString(key).ToString();if(value.empty()||value.size()>limit||value.find('\0')!=std::string::npos||value.find('\r')!=std::string::npos||value.find('\n')!=std::string::npos){error=std::string("Invalid native context ")+key;return false;}
      target=value;return true;
    };
    auto flag=[&](const char*key,std::optional<bool>&target){if(!input->HasKey(key))return true;if(input->GetType(key)!=VTYPE_BOOL){error=std::string("Native context ")+key+" must be boolean";return false;}target=input->GetBool(key);return true;};
    auto size=[&](const char*key,std::optional<Size>&target,bool nullable){
      if(!input->HasKey(key)||(nullable&&input->GetType(key)==VTYPE_NULL))return true;
      const auto value=input->GetDictionary(key);if(!value||value->GetType("width")!=VTYPE_INT||value->GetType("height")!=VTYPE_INT||value->GetSize()!=2){error=std::string("Invalid native context ")+key;return false;}
      const int width=value->GetInt("width"),height=value->GetInt("height");if(width<1||width>3840||height<1||height>2160){error=std::string("Native context ")+key+" exceeds capture dimensions 3840x2160";return false;}target=Size{width,height};return true;
    };
    if(!text("locale",parsed.locale,128)||!text("timezoneId",parsed.timezone,256)||!text("userAgent",parsed.user_agent,4096)||!text("colorScheme",parsed.color_scheme,32,true)||!text("reducedMotion",parsed.reduced_motion,32,true)||!text("forcedColors",parsed.forced_colors,32,true)||!text("contrast",parsed.contrast,32,true)||!flag("hasTouch",parsed.has_touch)||!flag("isMobile",parsed.is_mobile)||!flag("ignoreHTTPSErrors",parsed.ignore_https_errors)||!size("viewport",parsed.viewport,true)||!size("screen",parsed.screen,false))return false;
    const auto enum_value=[&](const char*key,const std::optional<std::string>&value,const std::vector<std::string>&allowed){if(!value)return true;for(const auto&item:allowed)if(item==*value)return true;error=std::string("Invalid native context ")+key;return false;};
    if(!enum_value("colorScheme",parsed.color_scheme,{"light","dark","no-preference",""})||!enum_value("reducedMotion",parsed.reduced_motion,{"reduce","no-preference",""})||!enum_value("forcedColors",parsed.forced_colors,{"active","none",""})||!enum_value("contrast",parsed.contrast,{"more","no-preference",""}))return false;
    if(input->HasKey("deviceScaleFactor")){const auto type=input->GetType("deviceScaleFactor");const double scale=type==VTYPE_INT?input->GetInt("deviceScaleFactor"):input->GetDouble("deviceScaleFactor");if((type!=VTYPE_INT&&type!=VTYPE_DOUBLE)||!std::isfinite(scale)||scale<0.25||scale>4){error="Native context deviceScaleFactor must be finite in [0.25,4]";return false;}parsed.device_scale=scale;}
    if(input->HasKey("geolocation")&&input->GetType("geolocation")!=VTYPE_NULL){
      const auto value=input->GetDictionary("geolocation");if(!value){error="Native context geolocation must be a dictionary or null";return false;}
      const auto number=[&](const char*key,double&target,bool required){
        if(!value->HasKey(key)){if(required)error=std::string("geolocation.")+key+": expected float, got undefined";return !required;}
        const auto type=value->GetType(key);if(type!=VTYPE_INT&&type!=VTYPE_DOUBLE){const char*received=type==VTYPE_NULL?"null":type==VTYPE_STRING?"string":type==VTYPE_BOOL?"boolean":"object";error=std::string("geolocation.")+key+": expected float, got "+received;return false;}
        target=type==VTYPE_INT?value->GetInt(key):value->GetDouble(key);if(!std::isfinite(target)){error=std::string("geolocation.")+key+": expected finite float";return false;}return true;
      };
      Geo geo;if(!number("longitude",geo.longitude,true)||!number("latitude",geo.latitude,true)||!number("accuracy",geo.accuracy,false))return false;
      if(geo.longitude < -180||geo.longitude>180){error="geolocation.longitude: precondition -180 <= LONGITUDE <= 180 failed.";return false;}
      if(geo.latitude < -90||geo.latitude>90){error="geolocation.latitude: precondition -90 <= LATITUDE <= 90 failed.";return false;}
      if(geo.accuracy<0){error="geolocation.accuracy: precondition 0 <= ACCURACY failed.";return false;}
      parsed.geolocation=geo;
    }
    if(parsed.no_fixed_viewport&&(parsed.device_scale||parsed.is_mobile.value_or(false))){error="Native deviceScaleFactor/isMobile requires a fixed viewport";return false;}
    if(input->HasKey("permissions")){
      const auto list=input->GetList("permissions");if(!list||list->GetSize()>32){error="Native context permissions require at most32 names";return false;}
      for(size_t i=0;i<list->GetSize();++i){if(list->GetType(i)!=VTYPE_STRING){error="Native context permissions must be strings";return false;}const auto name=list->GetString(i).ToString();if(auto invalid=NativePermissionError(name);!invalid.empty()){error=std::move(invalid);return false;}parsed.permissions.push_back(name);}
    }
    if(input->HasKey("httpCredentials")){
      const auto add=[&](Dict value){
        if(!value||value->GetType("username")!=VTYPE_STRING||value->GetType("password")!=VTYPE_STRING){error="Native context HTTP credentials require username and password";return false;}
        Auth auth;auth.username=value->GetString("username").ToString();auth.password=value->GetString("password").ToString();if(auth.username.size()>4096||auth.password.size()>4096||auth.username.find('\0')!=std::string::npos||auth.password.find('\0')!=std::string::npos){error="Native context HTTP credentials exceed capacity";return false;}
        if(value->HasKey("origin")){if(value->GetType("origin")!=VTYPE_STRING||(auth.origin=CanonicalOrigin(value->GetString("origin"))).empty()){error="Native context HTTP credential origin must be HTTP(S)";return false;}}
        if(value->HasKey("send")){const auto mode=value->GetString("send").ToString();if(value->GetType("send")!=VTYPE_STRING||(mode!="always"&&mode!="unauthorized")){error="Native context HTTP credential send must be always or unauthorized";return false;}auth.always=mode=="always";}
        parsed.http_credentials_all.push_back(std::move(auth));return true;
      };
      if(input->GetType("httpCredentials")==VTYPE_LIST){const auto rows=input->GetList("httpCredentials");if(rows->GetSize()>64){error="Native context HTTP credentials exceed 64 entries";return false;}for(size_t i=0;i<rows->GetSize();++i)if(!add(rows->GetDictionary(i)))return false;}
      else if(!add(input->GetDictionary("httpCredentials")))return false;
      if(!parsed.http_credentials_all.empty())parsed.http_credentials=parsed.http_credentials_all.front();
    }
    if(input->HasKey("proxy")){
      const auto value=input->GetDictionary("proxy");if(!value||value->GetType("server")!=VTYPE_STRING){error="Native context proxy requires a server";return false;}
      Proxy proxy;proxy.server=value->GetString("server").ToString();if(proxy.server.empty()||proxy.server.size()>4096||proxy.server.find_first_of("\r\n")!=std::string::npos||proxy.server.find('\0')!=std::string::npos){error="Invalid native context proxy server";return false;}
      const auto uri=proxy.server.find("://")==std::string::npos?"http://"+proxy.server:proxy.server;CefURLParts parts;
      const auto separator=uri.find("://");const auto requested_scheme=uri.substr(0,separator);const auto parsed_uri=(requested_scheme=="socks4"||requested_scheme=="socks5")?"http"+uri.substr(separator):uri;
      if(!CefParseURL(parsed_uri,parts)){error="Invalid native context proxy URL";return false;}
      const auto scheme=requested_scheme;proxy.host=CefString(&parts.host).ToString();const auto port=CefString(&parts.port).ToString();
      if((scheme!="http"&&scheme!="https"&&scheme!="socks4"&&scheme!="socks5")||proxy.host.empty()||!CefString(&parts.username).empty()||!CefString(&parts.password).empty()){error="Unsupported native context proxy URL";return false;}
      if(port.empty())proxy.port=scheme=="https"?443:(scheme=="http"?80:1080);else{int parsed_port=0;for(char digit:port){if(digit<'0'||digit>'9'||parsed_port>65535){error="Invalid native proxy port";return false;}parsed_port=parsed_port*10+(digit-'0');}if(parsed_port<1||parsed_port>65535){error="Invalid native proxy port";return false;}proxy.port=parsed_port;}
      proxy.server=scheme+"://"+proxy.host+":"+std::to_string(proxy.port);proxy.bypass="<-loopback>";
      if(value->HasKey("bypass")){if(value->GetType("bypass")!=VTYPE_STRING){error="Native proxy bypass must be a string";return false;}auto bypass=value->GetString("bypass").ToString();if(bypass.size()>4096||bypass.find_first_of("\r\n")!=std::string::npos||bypass.find('\0')!=std::string::npos){error="Native proxy bypass exceeds capacity";return false;}for(char&item:bypass)if(item==',')item=';';proxy.bypass+=";"+bypass;}
      for(const char*key:{"username","password"})if(value->HasKey(key)){if(value->GetType(key)!=VTYPE_STRING){error="Native proxy credentials must be strings";return false;}auto entry=value->GetString(key).ToString();if(entry.size()>4096||entry.find('\0')!=std::string::npos){error="Native proxy credentials exceed capacity";return false;}if(std::string(key)=="username")proxy.username=entry;else proxy.password=entry;proxy.authentication=true;}
      if((scheme=="socks4"||scheme=="socks5")&&proxy.authentication){error="Native Chromium SOCKS proxy authentication is unsupported";return false;}
      parsed.proxy=std::move(proxy);
    }
    output=std::move(parsed);return true;
  }
};

class ContextSettingsRegistry {
 public:
  bool Seed(int context,CefRefPtr<CefDictionaryValue>input,std::string&error){
    if(context<1||states_.contains(context)||states_.size()>=32){error="Native context settings capacity or identity conflict";return false;}
    ContextSettings parsed;
    parsed.no_fixed_viewport=input->HasKey("viewport")&&input->GetType("viewport")==VTYPE_NULL;if(!ContextSettings::Parse(input,parsed,error))return false;
    states_.emplace(context,std::make_shared<const ContextSettings>(std::move(parsed)));return true;
  }
  std::shared_ptr<const ContextSettings> Get(int context)const{const auto entry=states_.find(context);return entry==states_.end()?nullptr:entry->second;}
  void Drop(int context){states_.erase(context);}
  size_t size()const{return states_.size();}
 private:
  std::map<int,std::shared_ptr<const ContextSettings>>states_;
};

// One page's actual physical override acknowledgments. Callers invoke this on
// an empty root document or a debugger-paused child before author execution.
// The sender must retain its physical request until ACK/real target destruction
// and close the browser on its native deadline; logical cancellation is not ACK.
class ContextSettingsPage:public std::enable_shared_from_this<ContextSettingsPage>{
 public:
  using Result=std::function<void(bool,const std::string&)>;
  using Send=std::function<void(const std::string&,const std::string&,ContextSettings::Dict,Result)>;
  explicit ContextSettingsPage(Send send):send_(std::move(send)){}
  void Attach(const std::string&session,std::vector<ContextSettings::Step>steps,Result done){
    if(closed_||session.size()>128||!done||states_.contains(session)||states_.size()>=1024||queue_.size()>=64){if(done)done(false,"Native context settings target capacity or identity conflict");return;}
    auto work=std::make_shared<Work>();work->session=session;work->steps=std::move(steps);work->done=std::move(done);states_.emplace(session,work);queue_.push_back(work);Pump();
  }
  void Detach(const std::string&session){const auto found=states_.find(session);if(found==states_.end())return;auto work=found->second;states_.erase(found);work->detached=true;auto done=std::move(work->done);if(done)done(false,"Native context settings target detached");if(!physical_)Pump();}
  void Clear(){closed_=true;auto rows=std::move(states_);states_.clear();queue_.clear();for(auto&[session,work]:rows){work->detached=true;auto done=std::move(work->done);if(done)done(false,"Native context settings page closed");}active_.reset();physical_=false;}
  size_t targets()const{return states_.size();}
  size_t pending()const{return queue_.size();}
  bool physical()const{return physical_;}
 private:
  struct Work{std::string session;std::vector<ContextSettings::Step>steps;Result done;size_t next=0;bool detached=false;};
  void Pump(){
    if(closed_||physical_)return;
    if(!active_){while(!queue_.empty()&&queue_.front()->detached)queue_.pop_front();if(queue_.empty())return;active_=queue_.front();}
    const auto work=active_;if(work->detached){Finish(work,false,"Native context settings target detached");return;}
    if(work->next>=work->steps.size()){Finish(work,true,"");return;}
    auto step=work->steps[work->next++];physical_=true;
    send_(work->session,step.method,step.params,[weak=weak_from_this(),work](bool ok,const std::string&error){const auto self=weak.lock();if(!self||self->closed_||self->active_!=work)return;self->physical_=false;if(!ok||work->detached){self->Finish(work,false,error);return;}self->Pump();});
  }
  void Finish(const std::shared_ptr<Work>&work,bool ok,const std::string&error){
    if(active_!=work)return;
    auto done=std::move(work->done);if(!queue_.empty()&&queue_.front()==work)queue_.pop_front();active_.reset();if(!ok){const auto found=states_.find(work->session);if(found!=states_.end()&&found->second==work)states_.erase(found);}if(done)done(ok,error);Pump();
  }
  Send send_;std::map<std::string,std::shared_ptr<Work>>states_;std::deque<std::shared_ptr<Work>>queue_;std::shared_ptr<Work>active_;bool physical_=false,closed_=false;
};
}
