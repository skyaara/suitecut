// SPDX-License-Identifier: MIT
#include "curl_transport.h"
#include "http_extra_ca.h"
#include "http_content_decoder.h"
#include "http_surplus_prefix.h"
#include <charconv>
#include <algorithm>
#include <array>
#include <cctype>
#include <limits>

namespace suitecut {
namespace {
std::string Trim(std::string value) {
  while(!value.empty() && (value.back()=='\r' || value.back()=='\n' || value.back()==' ' || value.back()=='\t'))value.pop_back();
  const auto first=value.find_first_not_of(" \t");return first==std::string::npos?"":value.substr(first);
}
std::string ResponseError(CURLcode result,const NativeCurlTransport::Headers& headers,std::string diagnostic) {
  if(result!=CURLE_BAD_CONTENT_ENCODING)return diagnostic;
  std::string encoding;
  for(const auto& [name,value]:headers){
    auto lower=name;for(auto& c:lower)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if(lower!="content-encoding")continue;
    // Preserve the actual header chain without allowing an arbitrary response
    // header to turn a bounded transport diagnostic into an unbounded error.
    if(value.size()>256||encoding.size()+value.size()+2>256)return diagnostic;
    if(!encoding.empty())encoding+=", ";
    encoding+=value;
  }
  return encoding.empty()?diagnostic:"failed to decompress '"+encoding+"' encoding: "+diagnostic;
}
}
struct NativeCurlTransport::Transfer {
  int owner=0;CURL* handle=nullptr;curl_slist* outgoing=nullptr;curl_slist* resolved=nullptr;CURLSH* private_dns=nullptr;
  Request request;Response response;Data data;Done done;
  size_t cookie_receipts=0;bool discarded_auth_body=false;
  bool head_upload_complete=false;int header_status=0;double start_time=0;size_t header_bytes=0;std::array<char,CURL_ERROR_SIZE> diagnostic{};
  bool IntermediateAuthentication()const{
    if(!request.authoritative_cookies)return false;
    const bool server=header_status==401&&request.credentials&&!request.preemptive_credentials;
    if(!server)return false; // Proxy negotiation remains native libcurl ownership.
    for(const auto&[name,value]:response.headers){auto field=name,challenge=value;for(auto&c:field)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));for(auto&c:challenge)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));if(field=="www-authenticate"&&(challenge=="basic"||challenge.starts_with("basic ")))return true;}
    return false;
  }
  NativeHTTP1SurplusPrefix surplus;
  bool http1=false,framing_prepared=false,folded_headers=false,header_field=false;
  size_t committed_headers=0,header_lines=0;
  void CommitHeaderMetadata(){
    // Commit only complete, unfolded field values. Preserve real cookies from
    // earlier authentication responses without treating continuations as fields.
    for(size_t i=committed_headers;i<response.headers.size();++i){
      auto&[name,value]=response.headers[i];value=Trim(std::move(value));auto lower=name;
      for(auto&c:lower)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      if(lower=="set-cookie")response.set_cookies.push_back(value);
    }
    committed_headers=response.headers.size();header_field=false;
  }
  void PrepareFraming(){
    if(framing_prepared)return;
    framing_prepared=true;bool safe=http1&&!folded_headers&&request.method!="HEAD"&&header_status!=204&&header_status!=304;
    std::optional<size_t> length;
    for(const auto&[name,value]:response.headers){
      auto lower=name;for(auto&c:lower)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      if(lower=="transfer-encoding")safe=false;
      if(lower=="content-length"){
        size_t number=0;const auto parsed=std::from_chars(value.data(),value.data()+value.size(),number);
        if(parsed.ec!=std::errc()||parsed.ptr!=value.data()+value.size()||(length&&number!=*length))safe=false;
        else length=number;
      }
    }
    // Curl's own parsed framing length/version must agree. Our retained
    // header metadata alone cannot authorize interpretation of raw data.
    curl_off_t actual=-1;long version=0;
    if(curl_easy_getinfo(handle,CURLINFO_CONTENT_LENGTH_DOWNLOAD_T,&actual)!=CURLE_OK||actual<0||!length||static_cast<uint64_t>(actual)!=*length)safe=false;
    if(curl_easy_getinfo(handle,CURLINFO_HTTP_VERSION,&version)!=CURLE_OK||(version!=CURL_HTTP_VERSION_1_0&&version!=CURL_HTTP_VERSION_1_1))safe=false;
    surplus.Configure(length.value_or(0),safe&&length.has_value());
  }
  std::unique_ptr<NativeHTTPContentDecoder> decoder;
  bool decoder_prepared=false,body_forbidden=false;
  size_t encoded_bytes=0;std::optional<size_t> content_length;
  bool Current(){return !request.checkpoint||request.checkpoint();}
  bool DecodeFailure(){
    if(decoder&&!decoder->sink_rejected()&&response.error.empty()){
      response.error=ResponseError(CURLE_BAD_CONTENT_ENCODING,response.headers,decoder->error());
    }
    return false;
  }
  bool PrepareDecoder(){
    if(decoder_prepared)return true;
    decoder_prepared=true;
    body_forbidden=discarded_auth_body||request.method=="HEAD"||header_status==204||header_status==304||(header_status>=100&&header_status<200);
    if(body_forbidden)return true;
    std::string encoding;bool chunked=false;std::optional<size_t> length;
    for(const auto&[name,value]:response.headers){
      auto lower=name;for(auto&c:lower)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      if(lower=="content-encoding"){
        if(value.size()>256||encoding.size()+value.size()+2>256){response.error="Native HTTP content encoding header exceeds 256 bytes";return false;}
        if(!encoding.empty())encoding+=",";
        encoding+=value;
      }
      if(lower=="transfer-encoding")chunked=true;
      if(lower=="content-length"){
        size_t parsed=0;const auto result=std::from_chars(value.data(),value.data()+value.size(),parsed);
        if(result.ec==std::errc()&&result.ptr==value.data()+value.size())length=parsed;
      }
    }
    curl_off_t actual=-1;
    if(!chunked&&length&&curl_easy_getinfo(handle,CURLINFO_CONTENT_LENGTH_DOWNLOAD_T,&actual)==CURLE_OK&&actual>=0&&static_cast<uint64_t>(actual)==*length)content_length=length;
    decoder=std::make_unique<NativeHTTPContentDecoder>([this](auto bytes){return data(bytes.data(),bytes.size());},NativeHTTPContentDecoder::kPerCodecBytes,[this]{return Current();});
    if(!decoder->Configure(encoding))return DecodeFailure();
    // HTTP representation emptiness is established by actual transfer
    // completion, never solely by a declared Content-Length value.
    return true;
  }
  bool DecodedBody(const void*bytes,size_t length){
    if(!Current()||!PrepareDecoder())return false;
    if(body_forbidden)return data(bytes,length);
    if(length>std::numeric_limits<size_t>::max()-encoded_bytes){response.error="Native HTTP encoded body length overflow";return false;}
    encoded_bytes+=length;
    if(!decoder->Push({static_cast<const unsigned char*>(bytes),length}))return DecodeFailure();
    // Finish at the genuine declared end while still inside the write
    // callback, so invalid compressed content cannot enter curl's idle pool.
    if(content_length&&encoded_bytes==*content_length&&!decoder->Finish())return DecodeFailure();
    return true;
  }
  ~Transfer(){if(handle)curl_easy_cleanup(handle);if(outgoing)curl_slist_free_all(outgoing);if(resolved)curl_slist_free_all(resolved);if(private_dns)curl_share_cleanup(private_dns);}
};
NativeCurlTransport::NativeCurlTransport() {
  initialized_=curl_global_init(CURL_GLOBAL_DEFAULT)==CURLE_OK;if(initialized_)multi_=curl_multi_init();
  if(multi_){curl_multi_setopt(multi_,CURLMOPT_MAX_TOTAL_CONNECTIONS,64L);curl_multi_setopt(multi_,CURLMOPT_MAXCONNECTS,64L);}
}
NativeCurlTransport::~NativeCurlTransport(){Clear();if(multi_)curl_multi_cleanup(multi_);if(initialized_)curl_global_cleanup();}
bool NativeCurlTransport::Start(int owner,Request request,Data data,Done done,std::string& error) {
  if(!multi_ || active_.size()>=64 || active_.contains(owner)){error="Native HTTP transport is unavailable or at capacity";return false;}
  if(request.authoritative_cookies){if(!request.cookies.empty()||!request.refresh_cookie_header||!request.receive_cookies){error="Native authoritative cookie hooks are required without a curl cookie jar";return false;}if(!request.refresh_cookie_header(request.headers,error))return false;}
  auto transfer=std::make_shared<Transfer>();transfer->owner=owner;transfer->request=std::move(request);transfer->data=std::move(data);transfer->done=std::move(done);
  transfer->handle=curl_easy_init();if(!transfer->handle){error="Could not create native HTTP transport";return false;}
  auto handle=transfer->handle;
  if(const auto&lookup=transfer->request.lookup){
    if(lookup->hostname.empty()||lookup->hostname.size()>253||lookup->hostname.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")!=std::string::npos||lookup->port<1||lookup->port>65535||!HTTPLookupControls::Validate(lookup->addresses)){error="Invalid native HTTP lookup destination";return false;}
    std::string entry=lookup->hostname+":"+std::to_string(lookup->port)+":";
    for(const auto&address:lookup->addresses){if(entry.back()!=':')entry+=",";entry+=address.family==6?"["+address.address+"]":address.address;}
    transfer->resolved=curl_slist_append(nullptr,entry.c_str());transfer->private_dns=curl_share_init();
    if(!transfer->resolved||!transfer->private_dns||curl_share_setopt(transfer->private_dns,CURLSHOPT_SHARE,CURL_LOCK_DATA_DNS)!=CURLSHE_OK||curl_easy_setopt(handle,CURLOPT_SHARE,transfer->private_dns)!=CURLE_OK||curl_easy_setopt(handle,CURLOPT_RESOLVE,transfer->resolved)!=CURLE_OK||curl_easy_setopt(handle,CURLOPT_FRESH_CONNECT,1L)!=CURLE_OK||curl_easy_setopt(handle,CURLOPT_FORBID_REUSE,1L)!=CURLE_OK){error="Could not isolate native HTTP lookup cache and connection";return false;}
  }
  for(const auto& [name,value]:transfer->request.headers) {
    const auto line=name+(value.empty()?";":": "+value);
    auto list=curl_slist_append(transfer->outgoing,line.c_str());if(!list){error="Could not allocate native HTTP headers";return false;}transfer->outgoing=list;
  }
  curl_easy_setopt(handle,CURLOPT_URL,transfer->request.url.c_str());
  curl_easy_setopt(handle,CURLOPT_PROTOCOLS_STR,"http,https");curl_easy_setopt(handle,CURLOPT_REDIR_PROTOCOLS_STR,"http,https");
  curl_easy_setopt(handle,CURLOPT_FOLLOWLOCATION,0L);curl_easy_setopt(handle,CURLOPT_FAILONERROR,0L);
  curl_easy_setopt(handle,CURLOPT_NOSIGNAL,1L);curl_easy_setopt(handle,CURLOPT_PROXY,transfer->request.proxy_server.c_str());
  if(!transfer->request.proxy_server.empty()){
    curl_easy_setopt(handle,CURLOPT_NOPROXY,transfer->request.proxy_bypass.c_str());
    if(!transfer->request.proxy_username.empty()||!transfer->request.proxy_password.empty()){curl_easy_setopt(handle,CURLOPT_PROXYUSERNAME,transfer->request.proxy_username.c_str());curl_easy_setopt(handle,CURLOPT_PROXYPASSWORD,transfer->request.proxy_password.c_str());curl_easy_setopt(handle,CURLOPT_PROXYAUTH,transfer->request.preemptive_proxy_credentials?CURLAUTH_BASIC:CURLAUTH_ANY);}
  }
  curl_easy_setopt(handle,CURLOPT_SSL_VERIFYPEER,transfer->request.ignore_https_errors?0L:1L);curl_easy_setopt(handle,CURLOPT_SSL_VERIFYHOST,transfer->request.ignore_https_errors?0L:2L);
  curl_easy_setopt(handle,CURLOPT_PROXY_SSL_VERIFYPEER,transfer->request.ignore_https_errors?0L:1L);curl_easy_setopt(handle,CURLOPT_PROXY_SSL_VERIFYHOST,transfer->request.ignore_https_errors?0L:2L);
  if(transfer->request.extra_ca){
    if(!HTTPOpenSSLCurlCompatible()){
      error="Native API extra CA requires a curl backend with a matching OpenSSL ABI";return false;
    }
    // SSL_CTX changes are invisible to curl's connection-cache identity. Never
    // reuse a connection, TLS session, or cached mutable CA store across it.
    if(curl_easy_setopt(handle,CURLOPT_SSL_CTX_FUNCTION,ExtraTrust)!=CURLE_OK||
       curl_easy_setopt(handle,CURLOPT_SSL_CTX_DATA,transfer.get())!=CURLE_OK||
       curl_easy_setopt(handle,CURLOPT_FRESH_CONNECT,1L)!=CURLE_OK||
       curl_easy_setopt(handle,CURLOPT_FORBID_REUSE,1L)!=CURLE_OK||
       curl_easy_setopt(handle,CURLOPT_SSL_SESSIONID_CACHE,0L)!=CURLE_OK||
       curl_easy_setopt(handle,CURLOPT_CA_CACHE_TIMEOUT,0L)!=CURLE_OK){
      error="Could not isolate native API extra CA trust";return false;
    }
  }
  curl_easy_setopt(handle,CURLOPT_ACCEPT_ENCODING,"gzip,deflate,br");curl_easy_setopt(handle,CURLOPT_HTTP_CONTENT_DECODING,0L);curl_easy_setopt(handle,CURLOPT_HTTPHEADER,transfer->outgoing);
  curl_easy_setopt(handle,CURLOPT_ERRORBUFFER,transfer->diagnostic.data());
  // The callback discards every category except incoming body data. It emits
  // no raw payload/header logs and retains only fixed-size parser state.
  curl_easy_setopt(handle,CURLOPT_DEBUGFUNCTION,Observe);curl_easy_setopt(handle,CURLOPT_DEBUGDATA,transfer.get());curl_easy_setopt(handle,CURLOPT_VERBOSE,1L);
  curl_easy_setopt(handle,CURLOPT_XFERINFOFUNCTION,Progress);curl_easy_setopt(handle,CURLOPT_XFERINFODATA,transfer.get());curl_easy_setopt(handle,CURLOPT_NOPROGRESS,0L);

  curl_easy_setopt(handle,CURLOPT_HEADERFUNCTION,Header);curl_easy_setopt(handle,CURLOPT_HEADERDATA,transfer.get());
  curl_easy_setopt(handle,CURLOPT_WRITEFUNCTION,Body);curl_easy_setopt(handle,CURLOPT_WRITEDATA,transfer.get());
  if(transfer->request.authoritative_cookies)curl_easy_setopt(handle,CURLOPT_SUPPRESS_CONNECT_HEADERS,1L);
  else curl_easy_setopt(handle,CURLOPT_COOKIEFILE,"");
  for(const auto& cookie:transfer->request.cookies)curl_easy_setopt(handle,CURLOPT_COOKIELIST,cookie.c_str());
  if(transfer->request.credentials&&(!transfer->request.authoritative_cookies||transfer->request.preemptive_credentials)){curl_easy_setopt(handle,CURLOPT_USERNAME,transfer->request.username.c_str());curl_easy_setopt(handle,CURLOPT_PASSWORD,transfer->request.password.c_str());curl_easy_setopt(handle,CURLOPT_HTTPAUTH,transfer->request.preemptive_credentials?CURLAUTH_BASIC:CURLAUTH_ANY);}
  if(transfer->request.authoritative_cookies&&transfer->request.credentials&&!transfer->request.preemptive_credentials)curl_easy_setopt(handle,CURLOPT_FORBID_REUSE,1L);
  if(transfer->request.method=="HEAD"&&transfer->request.body.empty())curl_easy_setopt(handle,CURLOPT_NOBODY,1L);
  if(!transfer->request.body.empty()) {
    curl_easy_setopt(handle,CURLOPT_POSTFIELDS,transfer->request.body.data());curl_easy_setopt(handle,CURLOPT_POSTFIELDSIZE_LARGE,static_cast<curl_off_t>(transfer->request.body.size()));
  } else if(transfer->request.method=="POST" || transfer->request.method=="PUT" || transfer->request.method=="PATCH") {
    curl_easy_setopt(handle,CURLOPT_POSTFIELDS,"");curl_easy_setopt(handle,CURLOPT_POSTFIELDSIZE_LARGE,static_cast<curl_off_t>(0));
  }
  curl_easy_setopt(handle,CURLOPT_CUSTOMREQUEST,transfer->request.method.c_str());
  transfer->start_time=HTTPWallTime();const auto added=curl_multi_add_handle(multi_,handle);if(added!=CURLM_OK){error=curl_multi_strerror(added);return false;}
  active_[owner]=transfer;handles_[handle]=owner;return true;
}
CURLcode NativeCurlTransport::ExtraTrust(CURL*,void*context,void*state){
  auto&transfer=*static_cast<Transfer*>(state);
  if(!transfer.Current())return CURLE_ABORTED_BY_CALLBACK;
  return transfer.request.extra_ca&&transfer.request.extra_ca->Install(static_cast<SSL_CTX*>(context))?CURLE_OK:CURLE_SSL_CERTPROBLEM;
}
int NativeCurlTransport::Progress(void*state,curl_off_t,curl_off_t,curl_off_t,curl_off_t){
  return static_cast<Transfer*>(state)->surplus.Failed()?1:0;
}
int NativeCurlTransport::Observe(CURL*,curl_infotype type,char*bytes,size_t size,void*state){
  if(type!=CURLINFO_DATA_IN)return 0;
  auto&transfer=*static_cast<Transfer*>(state);
  if(!transfer.surplus.Observe({reinterpret_cast<const unsigned char*>(bytes),size})&&transfer.response.error.empty())transfer.response.error="Parse Error: Expected HTTP/";
  return 0;
}
size_t NativeCurlTransport::Header(char* bytes,size_t size,size_t count,void* state) {
  auto& transfer=*static_cast<Transfer*>(state);if(size && count>std::numeric_limits<size_t>::max()/size)return 0;const size_t length=size*count;
  if(length>262144-transfer.header_bytes){transfer.response.error="Native HTTP response headers exceed 256 KiB";return 0;}transfer.header_bytes+=length;
  if(!transfer.Current())return 0;
  const bool continuation=length&&(bytes[0]==' '||bytes[0]=='\t');
  if(continuation)transfer.folded_headers=true;
  const std::string line=Trim(std::string(bytes,length));
  // Folding must not bypass the previous parser-work ceiling by turning
  // thousands of physical field lines into one retained dictionary field.
  if(continuation||(!line.empty()&&!line.starts_with("HTTP/"))){
    if(++transfer.header_lines>1024){transfer.response.error="Native HTTP response header count exceeded";return 0;}
  }
  if(const auto security=CaptureHTTPSecurity(transfer.handle))transfer.response.security=security;
  if(continuation){
    if(!transfer.header_field||transfer.response.headers.empty()){transfer.response.error="Native HTTP header continuation has no preceding field";return 0;}
    transfer.response.headers.back().second+=" "+line;
    return length;
  }
  if(line.empty()&&transfer.header_status>=200)transfer.CommitHeaderMetadata();
  if(line.empty()&&transfer.header_status>=200&&transfer.request.authoritative_cookies){
    std::string error;const auto cookies=std::span<const std::string>(transfer.response.set_cookies).subspan(transfer.cookie_receipts);
    if(!(transfer.header_status==407&&!transfer.request.proxy_server.empty())&&!transfer.request.receive_cookies(cookies,error)){transfer.response.error=error.empty()?"Native authoritative cookie receipt rejected":error;return 0;}
    transfer.cookie_receipts=transfer.response.set_cookies.size();transfer.discarded_auth_body=transfer.IntermediateAuthentication();
  }
  if(line.empty()&&transfer.request.method=="HEAD"&&!transfer.request.body.empty()&&transfer.header_status>=200){transfer.head_upload_complete=true;return 0;}
  if(line.empty()&&transfer.header_status>=200)transfer.PrepareFraming();
  if(line.starts_with("HTTP/")) {
    transfer.surplus.Reset();transfer.framing_prepared=false;transfer.folded_headers=false;transfer.header_field=false;transfer.committed_headers=0;transfer.header_lines=0;
    transfer.http1=line.starts_with("HTTP/1.0 ")||line.starts_with("HTTP/1.1 ");
    transfer.discarded_auth_body=false;transfer.decoder.reset();transfer.decoder_prepared=false;transfer.body_forbidden=false;
    transfer.encoded_bytes=0;transfer.content_length.reset();
    const auto start=line.find(' ');transfer.header_status=start==std::string::npos?0:std::atoi(line.c_str()+start+1);
    if(!transfer.data(nullptr,0))return 0;
    transfer.response.headers.clear();transfer.response.status_text.clear();
    const auto code=line.find(' '),phrase=code==std::string::npos?std::string::npos:line.find(' ',code+1);
    if(phrase!=std::string::npos)transfer.response.status_text=line.substr(phrase+1);
  } else {
    const auto separator=line.find(':');
    if(separator!=std::string::npos) {
      if(transfer.response.headers.size()>=1024){transfer.response.error="Native HTTP response header count exceeded";return 0;}
      auto name=line.substr(0,separator),value=Trim(line.substr(separator+1));
      transfer.response.headers.emplace_back(std::move(name),std::move(value));transfer.header_field=true;
    }
  }
  return length;
}
size_t NativeCurlTransport::Body(char* bytes,size_t size,size_t count,void* state) {
  auto& transfer=*static_cast<Transfer*>(state);if(size && count>std::numeric_limits<size_t>::max()/size)return 0;const size_t length=size*count;
  if(transfer.discarded_auth_body)return transfer.Current()&&!transfer.surplus.Failed()?length:0;
  return !transfer.surplus.Failed()&&transfer.DecodedBody(bytes,length)?length:0;
}
void NativeCurlTransport::Finish(CURL* handle,CURLcode result) {
  const auto identified=handles_.find(handle);if(identified==handles_.end())return;
  const auto found=active_.find(identified->second);if(found==active_.end())return;
  const auto transfer=found->second;
  // A custom HEAD upload must send its body, then stop at the actual final
  // response headers. libcurl NOBODY suppresses uploads; reading a HEAD body
  // would instead wait for bytes that the HTTP peer must never send.
  if(result==CURLE_WRITE_ERROR&&transfer->head_upload_complete&&transfer->response.error.empty())result=CURLE_OK;
  // Content validation is our own native observation. Preserve libcurl's
  // actual completion code/errno instead of publishing a synthetic curlCode.
  if(result==CURLE_OK){
    if(!transfer->Current()){
      if(transfer->response.error.empty())transfer->response.error="Native HTTP decoding stopped at its original deadline or cancellation";
    } else if(transfer->PrepareDecoder()&&transfer->decoder&&transfer->encoded_bytes&&!transfer->decoder->Finish())transfer->DecodeFailure();
    // An actual successful HTTP transfer with zero representation octets is
    // empty, even if encoding metadata is present. Header validation above
    // still applies. Any nonempty compressed stream requires codec completion;
    // partial HTTP framing keeps its real curl failure and never enters here.
  }
  long status=0;char* url=nullptr;char* method=nullptr;
  curl_easy_getinfo(handle,CURLINFO_RESPONSE_CODE,&status);curl_easy_getinfo(handle,CURLINFO_EFFECTIVE_URL,&url);curl_easy_getinfo(handle,CURLINFO_EFFECTIVE_METHOD,&method);
  char* remote=nullptr;long port=0;curl_easy_getinfo(handle,CURLINFO_PRIMARY_IP,&remote);curl_easy_getinfo(handle,CURLINFO_PRIMARY_PORT,&port);transfer->response.remote_ip=remote?remote:"";transfer->response.remote_port=static_cast<int>(port);
  curl_easy_getinfo(handle,CURLINFO_OS_ERRNO,&transfer->response.os_errno);
  transfer->response.timing=CaptureHTTPTiming(handle,transfer->start_time,transfer->request.url.starts_with("https:"));transfer->response.error_code=result;
  if(const auto security=CaptureHTTPSecurity(handle))transfer->response.security=security;
  transfer->response.status=static_cast<int>(status);transfer->response.url=url?url:transfer->request.url;transfer->response.method=method?method:transfer->request.method;
  if(result!=CURLE_OK && transfer->response.error.empty())transfer->response.error=ResponseError(result,transfer->response.headers,transfer->diagnostic[0]?transfer->diagnostic.data():curl_easy_strerror(result));
  // NOBODY suppresses uploads. A custom HEAD upload ends at real response
  // headers instead; its Basic challenge retry therefore needs a fresh curl
  // transfer after those headers arrive, under the same native owner/deadline.
  bool basic_challenge=false,proxy_basic_challenge=false;
  const bool discarded_content_error=transfer->request.authoritative_cookies&&transfer->discarded_auth_body&&result==CURLE_BAD_CONTENT_ENCODING;
  if((transfer->head_upload_complete||transfer->request.authoritative_cookies)&&(transfer->response.error.empty()||discarded_content_error)&&(result==CURLE_OK||discarded_content_error)&&status==401&&transfer->request.credentials&&!transfer->request.preemptive_credentials){
    for(const auto&[name,value]:transfer->response.headers){auto lower=name,challenge=value;for(auto&c:lower)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));for(auto&c:challenge)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));if(lower=="www-authenticate"&&(challenge=="basic"||challenge.starts_with("basic ")))basic_challenge=true;}
  }
  if(transfer->head_upload_complete&&transfer->response.error.empty()&&result==CURLE_OK&&status==407&&!transfer->request.proxy_username.empty()&&!transfer->request.preemptive_proxy_credentials){
    for(const auto&[name,value]:transfer->response.headers){auto lower=name,challenge=value;for(auto&c:lower)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));for(auto&c:challenge)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));if(lower=="proxy-authenticate"&&(challenge=="basic"||challenge.starts_with("basic ")))proxy_basic_challenge=true;}
  }
  if((basic_challenge||proxy_basic_challenge)&&!transfer->request.authoritative_cookies){
    curl_slist* cookies=nullptr;if(curl_easy_getinfo(handle,CURLINFO_COOKIELIST,&cookies)==CURLE_OK){transfer->request.cookies.clear();for(auto*p=cookies;p;p=p->next)transfer->request.cookies.emplace_back(p->data);curl_slist_free_all(cookies);}
  }
  curl_multi_remove_handle(multi_,handle);handles_.erase(handle);active_.erase(found);
  curl_easy_cleanup(handle);transfer->handle=nullptr;transfer->decoder.reset();
  if(basic_challenge||proxy_basic_challenge){
    auto request=transfer->request;if(basic_challenge)request.preemptive_credentials=true;if(proxy_basic_challenge)request.preemptive_proxy_credentials=true;const auto prior=std::move(transfer->response.set_cookies);auto done=std::move(transfer->done);std::string error;
    auto callback=[done,prior](Response result)mutable{result.set_cookies.insert(result.set_cookies.begin(),prior.begin(),prior.end());done(std::move(result));};
    if(Start(transfer->owner,std::move(request),transfer->data,callback,error))return;
    transfer->response.error=error;callback(std::move(transfer->response));return;
  }
  transfer->done(std::move(transfer->response));
}
void NativeCurlTransport::Cancel(int owner) {
  const auto found=active_.find(owner);if(found==active_.end())return;
  const auto transfer=found->second;active_.erase(found);handles_.erase(transfer->handle);curl_multi_remove_handle(multi_,transfer->handle);
}
void NativeCurlTransport::Tick() {
  if(!multi_)return;
  int running=0;const auto result=curl_multi_perform(multi_,&running);
  if(result!=CURLM_OK){std::vector<CURL*> handles;for(const auto& [handle,owner]:handles_)handles.push_back(handle);for(auto handle:handles)Finish(handle,CURLE_FAILED_INIT);return;}
  int queued=0;while(auto message=curl_multi_info_read(multi_,&queued))if(message->msg==CURLMSG_DONE)Finish(message->easy_handle,message->data.result);
}
void NativeCurlTransport::Clear(){while(!active_.empty())Cancel(active_.begin()->first);}
void NativeCurlTransport::ResetIdle(){if(!active_.empty())return;if(multi_)curl_multi_cleanup(multi_);multi_=curl_multi_init();if(multi_){curl_multi_setopt(multi_,CURLMOPT_MAX_TOTAL_CONNECTIONS,64L);curl_multi_setopt(multi_,CURLMOPT_MAXCONNECTS,64L);}}
}  // namespace suitecut
