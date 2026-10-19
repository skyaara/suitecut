// SPDX-License-Identifier: MIT
#include "api_cookie_jar.h"
#include <algorithm>
#include <arpa/inet.h>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <list>
#include <new>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <curl/curl.h>
#include <libpsl.h>
#include "http_cookie.h"

namespace suitecut {
namespace {
constexpr double kMaximumExpiry=253402300799.0;
struct CookieCapacity final : std::bad_alloc {};
struct Address {std::string host,path,scheme;};
bool IP(const std::string& host){
  auto literal=host;
  if(literal.size()>1&&literal.front()=='['&&literal.back()==']')literal=literal.substr(1,literal.size()-2);
  in_addr v4{};in6_addr v6{};
  return inet_pton(AF_INET,literal.c_str(),&v4)==1||inet_pton(AF_INET6,literal.c_str(),&v6)==1;
}
bool Loopback(const std::string& host){
  if(host=="localhost"||host.ends_with(".localhost"))return true;
  in_addr v4{};if(inet_pton(AF_INET,host.c_str(),&v4)==1)return (ntohl(v4.s_addr)>>24)==127;
  auto literal=host;if(literal.size()>1&&literal.front()=='['&&literal.back()==']')literal=literal.substr(1,literal.size()-2);
  in6_addr v6{};return inet_pton(AF_INET6,literal.c_str(),&v6)==1&&IN6_IS_ADDR_LOOPBACK(&v6);
}
bool URL(const std::string& text,Address& result){
  if(text.empty()||text.size()>8192||text.find('\0')!=std::string::npos)return false;
  struct Cleanup {CURLU* value;~Cleanup(){curl_url_cleanup(value);}} url{curl_url()};
  if(!url.value||curl_url_set(url.value,CURLUPART_URL,text.c_str(),0)!=CURLUE_OK)return false;
  auto part=[&](CURLUPart field,unsigned flags=0){char* value=nullptr;std::string output;if(curl_url_get(url.value,field,&value,flags)==CURLUE_OK){output=value;curl_free(value);}return output;};
  result.scheme=CookieLower(part(CURLUPART_SCHEME));
  result.host=CookieLower(part(CURLUPART_HOST,CURLU_PUNYCODE));
  result.path=part(CURLUPART_PATH);if(result.path.empty())result.path="/";
  return (result.scheme=="http"||result.scheme=="https")&&!result.host.empty();
}
std::string Domain(std::string value){value=CookieLower(std::move(value));while(!value.empty()&&value.front()=='.')value.erase(0,1);return value;}
bool DomainSyntax(const std::string& value){
  if(value.empty()||value.size()>253||value.back()=='.'||value.find('\0')!=std::string::npos)return false;
  if(IP(value))return true;
  size_t label=0;for(const unsigned char c:value){if(c=='.'){if(!label||label>63)return false;label=0;}else if((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'||c=='_')++label;else return false;}
  return label>0&&label<=63;
}
bool DomainMatches(std::string_view host,std::string_view domain,bool host_only){
  return host==domain||(!host_only&&host.size()>domain.size()&&host.ends_with(domain)&&host[host.size()-domain.size()-1]=='.');
}
bool PathMatches(std::string_view request,std::string_view cookie){return request.starts_with(cookie)&&(request.size()==cookie.size()||cookie.ends_with('/')||request[cookie.size()]=='/');}
std::string DefaultPath(const std::string& path){const auto slash=path.rfind('/');return slash==std::string::npos||slash==0?"/":path.substr(0,slash);}
bool NameValue(const std::string& name,const std::string& value){
  if(name.size()>4096||value.size()>4096-name.size())return false;
  for(const unsigned char c:name)if(c<=32||c>=127||std::string_view("()<>@,;:\\\"/[]?={}").find(static_cast<char>(c))!=std::string_view::npos)return false;
  for(const unsigned char c:value)if(c<32||c==127||c==';')return false;
  return true;
}
bool ClockValid(const NativeAPICookieJar::Clock& clock,std::string& error){
  if(!clock.monotonic){error="Native API cookie monotonic clock is required";return false;}
  const double now=clock.monotonic();
  if(!std::isfinite(now)||now<0||!std::isfinite(clock.wall)||!std::isfinite(clock.deadline)||clock.deadline<0){error="Native API cookie clocks must be finite; monotonic clock and deadline must be nonnegative";return false;}
  if(clock.canceled){error="Native API cookie operation canceled";return false;}
  if(clock.deadline&&now>=clock.deadline){error="Native API cookie original deadline expired";return false;}
  return true;
}
bool PartitionSite(const std::string& text){
  Address site;if(!URL(text,site))return false;
  // A partition key is a schemeful site, never an arbitrary request URL.
  struct Cleanup {CURLU* value;~Cleanup(){curl_url_cleanup(value);}} url{curl_url()};
  if(!url.value||curl_url_set(url.value,CURLUPART_URL,text.c_str(),0)!=CURLUE_OK)return false;
  for(const auto field:{CURLUPART_USER,CURLUPART_PASSWORD,CURLUPART_QUERY,CURLUPART_FRAGMENT}){char* value=nullptr;const auto code=curl_url_get(url.value,field,&value,0);const bool present=code==CURLUE_OK&&value&&*value;curl_free(value);if(present)return false;}
  return site.path=="/";
}
bool Partitioned(const std::string& raw){
  size_t at=raw.find(';');while(at!=std::string::npos){const auto end=raw.find(';',at+1);const auto item=CookieLower(CookieTrim(raw.substr(at+1,end==std::string::npos?std::string::npos:end-at-1)));if(item=="partitioned"||item.starts_with("partitioned="))return true;at=end;}return false;
}
} // namespace

struct NativeAPICookieJar::Budget::Impl {
  size_t limit;std::pmr::memory_resource* upstream;
  std::atomic_size_t current{0},peak{0};
  bool Reserve(size_t bytes){size_t before=current.load();do{if(bytes>limit||before>limit-bytes)return false;}while(!current.compare_exchange_weak(before,before+bytes));size_t old=peak.load();while(old<before+bytes&&!peak.compare_exchange_weak(old,before+bytes)){}return true;}
};
NativeAPICookieJar::Budget::Budget(size_t limit,std::pmr::memory_resource* upstream):impl_(std::make_unique<Impl>()) {impl_->limit=limit;impl_->upstream=upstream?upstream:std::pmr::get_default_resource();}
NativeAPICookieJar::Budget::~Budget()=default;
size_t NativeAPICookieJar::Budget::Bytes()const{return impl_->current.load();}
size_t NativeAPICookieJar::Budget::Peak()const{return impl_->peak.load();}

struct NativeAPICookieJar::Impl {
  struct Resource final : std::pmr::memory_resource {
    std::shared_ptr<Budget> shared;size_t limit,current=0,peak=0;
    Resource(std::shared_ptr<Budget> budget,size_t maximum):shared(std::move(budget)),limit(maximum){}
    void* do_allocate(size_t bytes,size_t alignment)override{
      if(bytes>limit||current>limit-bytes||!shared->impl_->Reserve(bytes))throw CookieCapacity{};
      void* result=nullptr;try{result=shared->impl_->upstream->allocate(bytes,alignment);}catch(...){shared->impl_->current.fetch_sub(bytes);throw;}
      current+=bytes;peak=std::max(peak,current);return result;
    }
    void do_deallocate(void* value,size_t bytes,size_t alignment)override{shared->impl_->upstream->deallocate(value,bytes,alignment);current-=bytes;shared->impl_->current.fetch_sub(bytes);}
    bool do_is_equal(const std::pmr::memory_resource& other)const noexcept override{return this==&other;}
  };
  struct Entry {
    std::pmr::string name,value,domain,scope,path,partition;
    double expires=-1;bool secure=false,http_only=false,host_only=false,has_expiry=false,has_partition=false,cross_site=true;
    int same_site=2;uint64_t identity=0;
    explicit Entry(std::pmr::memory_resource* resource):name(resource),value(resource),domain(resource),scope(resource),path(resource),partition(resource){}
    Entry(const Entry& from,std::pmr::memory_resource* resource):name(from.name,resource),value(from.value,resource),domain(from.domain,resource),scope(from.scope,resource),path(from.path,resource),partition(from.partition,resource),expires(from.expires),secure(from.secure),http_only(from.http_only),host_only(from.host_only),has_expiry(from.has_expiry),has_partition(from.has_partition),cross_site(from.cross_site),same_site(from.same_site),identity(from.identity){}
    void Swap(Entry& other) noexcept {name.swap(other.name);value.swap(other.value);domain.swap(other.domain);scope.swap(other.scope);path.swap(other.path);partition.swap(other.partition);std::swap(expires,other.expires);std::swap(secure,other.secure);std::swap(http_only,other.http_only);std::swap(host_only,other.host_only);std::swap(has_expiry,other.has_expiry);std::swap(has_partition,other.has_partition);std::swap(cross_site,other.cross_site);std::swap(same_site,other.same_site);std::swap(identity,other.identity);}
  };
  Limits limits;Resource resource;std::pmr::list<Entry> entries;uint64_t next_identity=0;
  Impl(std::shared_ptr<Budget> budget,Limits maximum):limits(maximum),resource(std::move(budget),maximum.allocated_bytes),entries(&resource){}
  void Sweep(double now){std::erase_if(entries,[now](const Entry& item){return item.has_expiry&&item.expires<=now;});}
  bool Match(const Entry& entry,const Address& url,bool selection=false)const{return DomainMatches(url.host,entry.scope,selection?false:entry.host_only)&&PathMatches(url.path,entry.path)&&(!entry.secure||url.scheme=="https"||Loopback(url.host));}
  bool Apply(std::pmr::list<Entry>& target,Entry& entry,double now,uint64_t& identity,std::string& error){
    const auto existing=std::find_if(target.begin(),target.end(),[&](const Entry& old){return old.name==entry.name&&old.scope==entry.scope&&old.path==entry.path&&old.has_partition==entry.has_partition&&(!old.has_partition||(old.partition==entry.partition&&old.cross_site==entry.cross_site));});
    if(entry.has_expiry&&entry.expires<=now){if(existing!=target.end())target.erase(existing);return true;}
    if(identity==std::numeric_limits<uint64_t>::max()){error="Native API cookie identity space exhausted";return false;}entry.identity=++identity;
    if(existing!=target.end()){existing->Swap(entry);return true;}
    if(target.size()>=limits.entries){error="Native API cookie entry capacity exceeded";return false;}
    target.emplace_back(&resource);target.back().Swap(entry);return true;
  }
  std::pmr::list<Entry> Clone(){std::pmr::list<Entry> result(&resource);for(const auto& entry:entries)result.emplace_back(entry,&resource);return result;}
  bool ImportEntry(const Input& input,Entry& item,std::string& error){
    if((input.url&&input.url->size()>8192)||(input.domain&&input.domain->size()>254)||(input.path&&input.path->size()>8192)||(input.partition_key&&input.partition_key->size()>8192)){error="Native API cookie field capacity exceeded";return false;}
    if(!NameValue(input.name,input.value)){error="Invalid native API cookie name or value";return false;}
    const bool has_url=input.url.has_value(),has_domain=input.domain.has_value();
    if(has_url==has_domain||(has_domain&&!input.path)||(has_url&&input.path)){error="Cookie should have a url or a domain/path pair";return false;}
    Address url;std::string scope;
    if(has_url){if(!URL(*input.url,url)){error="Invalid native API cookie URL";return false;}scope=url.host;item.host_only=true;item.domain=url.host;item.path=input.path?*input.path:DefaultPath(url.path);}
    else{scope=Domain(*input.domain);if(!DomainSyntax(scope)||(!IP(scope)&&scope!="localhost"&&!scope.ends_with(".localhost")&&psl_is_public_suffix(psl_builtin(),scope.c_str()))){error="Invalid native API cookie domain or public suffix";return false;}item.domain=CookieLower(*input.domain);item.path=*input.path;item.host_only=IP(scope)||!input.domain->starts_with('.');}
    if(item.path.empty()||item.path.front()!='/'||item.path.size()>8192||std::any_of(item.path.begin(),item.path.end(),[](unsigned char c){return c<32||c==127;})){error="Invalid native API cookie path";return false;}
    const double expiry=input.expires.value_or(-1);if(!std::isfinite(expiry)||(expiry!=-1&&expiry<0)||expiry>kMaximumExpiry){error="Invalid native API cookie expiration";return false;}
    if(input.same_site<1||input.same_site>3){error="Invalid native API cookie sameSite";return false;}
    item.scope=scope;item.name=input.name;item.value=input.value;item.expires=expiry;item.has_expiry=input.expires.has_value()&&expiry!=-1;item.secure=input.secure.value_or(has_url&&url.scheme=="https");item.http_only=input.http_only;item.same_site=input.same_site;
    if(input.name.starts_with("__Secure-")&&!item.secure){error="Invalid secure native API cookie prefix";return false;}
    if(input.name.starts_with("__Host-")&&(!item.secure||has_domain||!item.host_only||item.path!="/")){error="Invalid host native API cookie prefix";return false;}
    if(input.partition_key){if(!item.secure||!PartitionSite(*input.partition_key)){error="Invalid native API cookie partition key";return false;}item.has_partition=true;item.partition=*input.partition_key;item.cross_site=input.partition_cross_site;}
    return true;
  }
  bool ResponseEntry(const std::string& raw,const Address& url,double now,Entry& item){
    // No API top-level site exists for an unsolicited Partitioned response.
    // Never silently turn a partitioned response into an unpartitioned cookie.
    if(Partitioned(raw))return false;
    const auto parsed=ParseNativeHTTPCookie(raw,url.host,url.path,now);if(!parsed)return false;
    const auto scope=parsed->domain.empty()?url.host:parsed->domain;
    if(!DomainSyntax(scope))return false;
    const bool host_only=parsed->domain.empty()||IP(url.host)||psl_is_public_suffix(psl_builtin(),scope.c_str());
    if(IP(url.host)&&scope!=url.host)return false;
    if(!psl_is_cookie_domain_acceptable(psl_builtin(),url.host.c_str(),scope.c_str()))return false;
    if(parsed->name.starts_with("__Secure-")&&(url.scheme!="https"&&!Loopback(url.host)))return false;
    if(parsed->name.starts_with("__Host-")&&(url.scheme!="https"&&!Loopback(url.host)))return false;
    item.name=parsed->name;item.value=parsed->value;item.domain=host_only?scope:"."+scope;item.scope=scope;item.path=parsed->path;item.expires=parsed->expires.value_or(-1);item.has_expiry=parsed->expires.has_value();item.secure=parsed->secure;item.http_only=parsed->http_only;item.same_site=parsed->same_site?parsed->same_site:2;item.host_only=host_only;return true;
  }
  template<class Run> bool Guard(std::string& error,Run run){
    error.clear();try{return run();}catch(const CookieCapacity&){error="Native API cookie physical allocation budget exceeded";}catch(const std::bad_alloc&){error="Native API cookie allocation failed";}return false;
  }
};
NativeAPICookieJar::NativeAPICookieJar(std::shared_ptr<Budget> budget):NativeAPICookieJar(std::move(budget),Limits{}){}
NativeAPICookieJar::NativeAPICookieJar(std::shared_ptr<Budget> budget,Limits limits):impl_(std::make_unique<Impl>(budget?std::move(budget):std::make_shared<Budget>(),limits)){}
NativeAPICookieJar::~NativeAPICookieJar()=default;
bool NativeAPICookieJar::Import(std::span<const Input> input,const Clock& clock,std::string& error){return impl_->Guard(error,[&]{if(!ClockValid(clock,error))return false;if(input.size()>4096){error="Native API cookie import entry capacity exceeded";return false;}impl_->Sweep(clock.wall);auto next=impl_->Clone();auto identity=impl_->next_identity;for(const auto& value:input){Impl::Entry entry(&impl_->resource);if(!impl_->ImportEntry(value,entry,error)||!impl_->Apply(next,entry,clock.wall,identity,error))return false;}if(!ClockValid(clock,error))return false;impl_->entries.swap(next);impl_->next_identity=identity;return true;});}
bool NativeAPICookieJar::Reset(std::span<const Input> input,const Clock& clock,std::string& error){return impl_->Guard(error,[&]{if(!ClockValid(clock,error))return false;if(input.size()>4096){error="Native API cookie import entry capacity exceeded";return false;}impl_->Sweep(clock.wall);std::pmr::list<Impl::Entry> next(&impl_->resource);auto identity=impl_->next_identity;for(const auto& value:input){Impl::Entry entry(&impl_->resource);if(!impl_->ImportEntry(value,entry,error)||!impl_->Apply(next,entry,clock.wall,identity,error))return false;}if(!ClockValid(clock,error))return false;impl_->entries.swap(next);impl_->next_identity=identity;return true;});}
bool NativeAPICookieJar::ValidateReplacement(std::span<const Input> input,const Clock& clock,std::string& error){return impl_->Guard(error,[&]{if(!ClockValid(clock,error))return false;if(input.size()>4096){error="Native API cookie import entry capacity exceeded";return false;}impl_->Sweep(clock.wall);std::pmr::list<Impl::Entry> next(&impl_->resource);auto identity=impl_->next_identity;for(const auto& value:input){Impl::Entry entry(&impl_->resource);if(!impl_->ImportEntry(value,entry,error)||!impl_->Apply(next,entry,clock.wall,identity,error))return false;}return ClockValid(clock,error);});}
bool NativeAPICookieJar::Receive(const std::string& url,std::span<const std::string> headers,const Clock& clock,std::string& error){return impl_->Guard(error,[&]{if(!ClockValid(clock,error))return false;Address address;if(!URL(url,address)){error="Invalid native API cookie response URL";return false;}if(headers.size()>4096){error="Native API cookie response entry capacity exceeded";return false;}if(headers.empty()){impl_->Sweep(clock.wall);return ClockValid(clock,error);}size_t bytes=0;for(const auto& header:headers){if(header.size()>1048576-bytes){error="Native API cookie response byte capacity exceeded";return false;}bytes+=header.size();}impl_->Sweep(clock.wall);std::optional<std::pmr::list<Impl::Entry>> next;auto identity=impl_->next_identity;for(const auto& raw:headers){Impl::Entry entry(&impl_->resource);if(!impl_->ResponseEntry(raw,address,clock.wall,entry))continue;if(!next)next.emplace(impl_->Clone());if(!impl_->Apply(*next,entry,clock.wall,identity,error))return false;}if(!ClockValid(clock,error))return false;if(next){impl_->entries.swap(*next);impl_->next_identity=identity;}return true;});}
bool NativeAPICookieJar::Cookies(std::span<const std::string> urls,const Clock& clock,std::vector<Cookie>& output,std::string& error){output.clear();return impl_->Guard(error,[&]{if(!ClockValid(clock,error))return false;if(urls.size()>1024){error="Native API cookie URL filter capacity exceeded";return false;}std::vector<Address> addresses;for(const auto& url:urls){Address address;if(!URL(url,address)){error="Invalid native API cookie URL filter";return false;}addresses.push_back(std::move(address));}impl_->Sweep(clock.wall);std::vector<Cookie> result;for(const auto& entry:impl_->entries){if(!addresses.empty()&&std::none_of(addresses.begin(),addresses.end(),[&](const auto& url){return impl_->Match(entry,url,true);}))continue;Cookie cookie;cookie.name=entry.name;cookie.value=entry.value;cookie.domain=entry.domain;cookie.path=entry.path;cookie.expires=entry.expires;cookie.secure=entry.secure;cookie.http_only=entry.http_only;cookie.same_site=entry.same_site;cookie.identity=entry.identity;if(entry.has_partition){cookie.partition_key=entry.partition;cookie.partition_cross_site=entry.cross_site;}result.push_back(std::move(cookie));}if(!ClockValid(clock,error))return false;output=std::move(result);return true;});}
bool NativeAPICookieJar::Header(const std::string& url,const Clock& clock,std::string& output,std::string& error){output.clear();return impl_->Guard(error,[&]{if(!ClockValid(clock,error))return false;Address address;if(!URL(url,address)){error="Invalid native API cookie header URL";return false;}impl_->Sweep(clock.wall);std::string result;for(const auto& entry:impl_->entries){if(entry.has_partition||!impl_->Match(entry,address))continue;const auto bytes=entry.name.size()+entry.value.size()+1+(result.empty()?0:2);if(bytes>impl_->limits.header_bytes||result.size()>impl_->limits.header_bytes-bytes){error="Native API cookie wire header capacity exceeded";return false;}if(!result.empty())result+="; ";result.append(entry.name);result+='=';result.append(entry.value);}if(!ClockValid(clock,error))return false;output=std::move(result);return true;});}
bool NativeAPICookieJar::Remove(std::span<const uint64_t> identities,const Clock& clock,std::string& error){return impl_->Guard(error,[&]{if(!ClockValid(clock,error))return false;if(identities.size()>4096){error="Native API cookie removal capacity exceeded";return false;}impl_->Sweep(clock.wall);std::vector<uint64_t> exact(identities.begin(),identities.end());std::sort(exact.begin(),exact.end());if(!ClockValid(clock,error))return false;std::erase_if(impl_->entries,[&](const auto& entry){return std::binary_search(exact.begin(),exact.end(),entry.identity);});return true;});}
bool NativeAPICookieJar::Clear(const Clock& clock,std::string& error){return impl_->Guard(error,[&]{if(!ClockValid(clock,error))return false;impl_->entries.clear();return true;});}
size_t NativeAPICookieJar::Size()const{return impl_->entries.size();}
size_t NativeAPICookieJar::AllocatedBytes()const{return impl_->resource.current;}
size_t NativeAPICookieJar::PeakAllocatedBytes()const{return impl_->resource.peak;}
} // namespace suitecut
