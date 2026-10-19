// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "init_startup_readiness.h"
#include "include/cef_api_hash.h"
#include "include/cef_urlrequest.h"
#include <cassert>
#include <cstdio>
#include <type_traits>

// Controlled native identity interfaces only. No real browser/V8 instance,
// pre-author barrier, credential device, send/delivery or invocation claim.
class IdentityBrowser:public CefBrowser {
 public:
  bool valid=true;std::function<void()>on_valid;int id=8;CefRefPtr<CefFrame>main;
  bool IsValid()override{if(on_valid)on_valid();return valid;}int GetIdentifier()override{return id;}
  bool IsSame(CefRefPtr<CefBrowser>other)override{return other.get()==this;}
  CefRefPtr<CefFrame>GetMainFrame()override{return main;}
  CefRefPtr<CefBrowserHost>GetHost()override{assert(false);return nullptr;}
  bool CanGoBack()override{return false;}void GoBack()override{assert(false);}
  bool CanGoForward()override{return false;}void GoForward()override{assert(false);}
  bool IsLoading()override{return false;}void Reload()override{assert(false);}
  void ReloadIgnoreCache()override{assert(false);}void StopLoad()override{assert(false);}
  bool IsPopup()override{return false;}bool HasDocument()override{return true;}
  CefRefPtr<CefFrame>GetFocusedFrame()override{return main;}
  CefRefPtr<CefFrame>GetFrameByIdentifier(const CefString&)override{return nullptr;}
  CefRefPtr<CefFrame>GetFrameByName(const CefString&)override{return nullptr;}
  size_t GetFrameCount()override{return 1;}
  void GetFrameIdentifiers(std::vector<CefString>&)override{}void GetFrameNames(std::vector<CefString>&)override{}
 private:IMPLEMENT_REFCOUNTING(IdentityBrowser);
};
class IdentityFrame:public CefFrame {
 public:
  IdentityFrame(IdentityBrowser*browser,std::string id,bool main):browser(browser),id(std::move(id)),main(main){}
  IdentityBrowser*browser;std::string id;bool main,valid=true;int v8_reads=0,sends=0;CefRefPtr<CefV8Context>context;
  bool IsValid()override{return valid;}bool IsMain()override{return main;}
  CefString GetIdentifier()override{return id;}CefRefPtr<CefBrowser>GetBrowser()override{return browser;}
  CefRefPtr<CefV8Context>GetV8Context()override{++v8_reads;return context;}
  void SendProcessMessage(CefProcessId,CefRefPtr<CefProcessMessage>)override{++sends;assert(false);}
  void Undo()override{assert(false);}void Redo()override{assert(false);}void Cut()override{assert(false);}void Copy()override{assert(false);}
  void Paste()override{assert(false);}void PasteAndMatchStyle()override{assert(false);}void Delete()override{assert(false);}void SelectAll()override{assert(false);}
  void ViewSource()override{assert(false);}void GetSource(CefRefPtr<CefStringVisitor>)override{assert(false);}void GetText(CefRefPtr<CefStringVisitor>)override{assert(false);}
  void LoadRequest(CefRefPtr<CefRequest>)override{assert(false);}void LoadURL(const CefString&)override{assert(false);}
  void ExecuteJavaScript(const CefString&,const CefString&,int)override{assert(false);}
  bool IsFocused()override{return false;}CefString GetName()override{return {};}
  CefRefPtr<CefFrame>GetParent()override{return nullptr;}CefString GetURL()override{return "about:blank";}
  void VisitDOM(CefRefPtr<CefDOMVisitor>)override{assert(false);}
  CefRefPtr<CefURLRequest>CreateURLRequest(CefRefPtr<CefRequest>,CefRefPtr<CefURLRequestClient>)override{assert(false);return nullptr;}
 private:IMPLEMENT_REFCOUNTING(IdentityFrame);
};
class IdentityContext:public CefV8Context {
 public:
  IdentityContext(IdentityBrowser*browser,IdentityFrame*frame):browser(browser),frame(frame){}
  IdentityBrowser*browser;IdentityFrame*frame;bool valid=true;int reads=0;std::function<void()>on_valid;
  bool IsValid()override{++reads;if(on_valid)on_valid();return valid;}
  bool IsSame(CefRefPtr<CefV8Context>other)override{return other.get()==this;}
  CefRefPtr<CefBrowser>GetBrowser()override{return browser;}CefRefPtr<CefFrame>GetFrame()override{return frame;}
  CefRefPtr<CefTaskRunner>GetTaskRunner()override{return nullptr;}
  CefRefPtr<CefV8Value>GetGlobal()override{assert(false);return nullptr;}
  bool Enter()override{assert(false);return false;}bool Exit()override{assert(false);return false;}
  bool Eval(const CefString&,const CefString&,int,CefRefPtr<CefV8Value>&,CefRefPtr<CefV8Exception>&)override{assert(false);return false;}
 private:IMPLEMENT_REFCOUNTING(IdentityContext);
};
using Renderer=suitecut::InitStartupReceiver;using Browser=suitecut::InitStartupReadiness;
using Owner=suitecut::InitSnapshotOwner;using Pin=suitecut::InitSnapshotPin;using Status=suitecut::InitStartupSelections::Status;
std::string Body(int credential=0,bool installed=false,std::string source="native()"){return "{\"contextId\":4,\"version\":1,\"scripts\":[{\"id\":1,\"source\":\""+source+"\"}],\"bindingVersion\":17,\"bindings\":[],\"credentialVersion\":"+std::to_string(credential)+",\"credentialsInstalled\":"+(installed?"true":"false")+"}";}
struct Fixture {
  Owner owner{4,8,19,"actual-channel"};CefRefPtr<IdentityBrowser>browser=new IdentityBrowser;
  CefRefPtr<IdentityFrame>main=new IdentityFrame(browser.get(),"actual-channel",true),child=new IdentityFrame(browser.get(),"actual-child",false);
  CefRefPtr<IdentityContext>context=new IdentityContext(browser.get(),child.get());
  suitecut::InitStartupRendererPeer::Ptr renderer_peer;suitecut::InitStartupBrowserPeer::Ptr browser_peer;
  Renderer renderer;double now=1;bool current=true;uint64_t generation=1;int queries=0;std::function<void()>on_query;
  Browser browser_data{{[this]{return now;},[this](int command,uint64_t gen){++queries;if(on_query)on_query();return current&&command==7&&gen==generation;}}};
  Fixture(){browser->main=main;child->context=context;renderer_peer=suitecut::InitStartupRendererPeer::Observe(owner,browser,main);browser_peer=suitecut::InitStartupBrowserPeer::Observe(owner,browser,main);assert(renderer_peer&&browser_peer&&renderer.Admit(renderer_peer));}
  Renderer::Result Receive(Pin pin={{4,8,19,"actual-channel"},1,1},std::string body=Body()){auto message=suitecut::InitSnapshotMessage::Build(pin,body);assert(message);return renderer.Receive(renderer_peer,PID_BROWSER,main,message,pin);}
  std::optional<Browser::Operation>Operation(double deadline=10){return browser_data.ObserveOperation(7,generation,deadline);}
  std::shared_ptr<Browser::CredentialLease>Credentials(const Browser::Operation&op,uint64_t reset=1,int version=0){return browser_data.ResetCredentials(browser_peer,op,reset,version);}
  std::shared_ptr<Browser::Ticket>Ticket(const Browser::Operation&op,const Pin&pin,uint64_t reset=1,int version=0,bool installed=false){return browser_data.Reserve(browser_peer,op,pin,reset,version,installed);}
  std::shared_ptr<const suitecut::InitStartupDocument>Document(uint64_t sequence=1){return suitecut::InitStartupDocument::Observe(renderer_peer,child,context,sequence);}
  void Close(){browser_data.Retire(browser_peer);renderer.Retire(renderer_peer);renderer.TakeReleased();assert(!main->sends&&!child->sends);}
};
static_assert(!std::is_convertible_v<suitecut::InitStartupBrowserPeer::Ptr,suitecut::InitStartupRendererPeer::Ptr>);
static_assert(!std::is_constructible_v<suitecut::InitStartupRendererPeer,Owner,CefRefPtr<CefBrowser>,CefRefPtr<CefFrame>>);
static_assert(!std::is_default_constructible_v<Browser::Operation>);
static_assert(!std::is_aggregate_v<suitecut::InitStartupDocument>);
int main(){cef_api_hash(CEF_API_VERSION,0);int groups=0;
 {Fixture f;auto r=f.Receive();assert(r.status==Status::Installed&&r.mapping_observed&&r.receipt&&f.renderer.Physical()==1);auto op=f.Operation();assert(op);auto credential=f.Credentials(*op);assert(credential&&f.browser_data.CredentialsCompleted(credential,true));auto ticket=f.Ticket(*op,r.receipt->Pin());assert(ticket&&!f.browser_data.Publish(ticket));const auto reads=f.child->v8_reads;assert(f.browser_data.Received(f.browser_peer,PID_RENDERER,f.main,r.receipt->Message()));auto eligible=f.browser_data.Publish(ticket);assert(eligible&&f.browser_data.Current(eligible)&&f.child->v8_reads==reads&&f.main->v8_reads==0);r.receipt.reset();f.Close();assert(!f.renderer.Physical());++groups;}
 {Fixture f;auto invalid=f.owner;++invalid.browser;assert(!suitecut::InitStartupRendererPeer::Observe(invalid,f.browser,f.main));assert(!suitecut::InitStartupBrowserPeer::Observe(f.owner,nullptr,f.main));assert(!suitecut::InitStartupBrowserPeer::Observe(f.owner,f.browser,f.child));auto other=new IdentityContext(f.browser.get(),f.child.get());assert(!suitecut::InitStartupDocument::Observe(f.renderer_peer,f.child,other,1));assert(!suitecut::InitStartupDocument::Observe(f.renderer_peer,f.child,f.context,0));f.child->id.clear();assert(!f.Document());f.child->id="actual-child";f.Close();++groups;}
 {Fixture f;auto first=f.Receive();Pin next{f.owner,2,2};auto bad=f.Receive(next,"{}");assert(bad.status==Status::Malformed&&!bad.mapping_observed&&f.renderer.Latest(f.renderer_peer)->Pin()==first.receipt->Pin()&&f.renderer.Physical()==1);first.receipt.reset();f.Close();++groups;}
 {Fixture f;Pin pin{f.owner,1,1};for(int field=0;field<3;++field){auto wrong=pin;if(field==0)++wrong.owner.browser;if(field==1)++wrong.owner.renderer_epoch;if(field==2)wrong.owner.frame="other";auto message=suitecut::InitSnapshotMessage::Build(wrong,Body());auto result=f.renderer.Receive(f.renderer_peer,PID_BROWSER,f.main,message,pin);assert(result.status==Status::Malformed&&!result.mapping_observed);}auto message=suitecut::InitSnapshotMessage::Build(pin,Body());assert(f.renderer.Receive(f.renderer_peer,PID_RENDERER,f.main,message,pin).status==Status::NotAdmitted);assert(f.renderer.Receive(f.renderer_peer,PID_BROWSER,f.child,message,pin).status==Status::NotAdmitted);assert(!f.renderer.Physical());f.Close();++groups;}
 {Fixture f;auto first=f.Receive(),duplicate=f.Receive();assert(duplicate.status==Status::Duplicate&&duplicate.mapping_observed&&f.renderer.Physical()==1);first.receipt.reset();duplicate.receipt.reset();f.Close();++groups;}
 {Fixture f;auto first=f.Receive();auto document=f.Document();assert(document);auto old=f.renderer.Select(document);assert(old&&f.renderer.Current(*old));auto next=f.Receive({f.owner,2,2},Body(0,false,"new()"));assert(next.receipt&&f.renderer.Current(*old)&&old->Snapshot().Pin()==first.receipt->Pin());assert(f.renderer.Release(*old));old.reset();first.receipt.reset();assert(f.renderer.TakeReleased().size()==1);next.receipt.reset();f.Close();assert(!f.renderer.Physical());++groups;}
 {Fixture f;auto r=f.Receive();auto document=f.Document();f.context->reads=0;f.context->on_valid=[&]{if(f.context->reads>=3){assert(f.renderer.Readers()==1);f.context->valid=false;}};assert(!f.renderer.Select(document)&&!f.renderer.Readers()&&f.renderer.Physical()==1);r.receipt.reset();f.Close();++groups;}
 {Fixture f;assert(!f.Operation(1));f.now=0;assert(f.Operation(1));f.generation=2;assert(!f.browser_data.ObserveOperation(7,1,10));f.now=std::numeric_limits<double>::quiet_NaN();assert(!f.Operation());f.Close();++groups;}
 {Fixture f;auto r=f.Receive();auto op=f.Operation();auto credential=f.Credentials(*op);assert(f.browser_data.CredentialsCompleted(credential,true));auto ticket=f.Ticket(*op,r.receipt->Pin());f.now=10;assert(!f.browser_data.Received(f.browser_peer,PID_RENDERER,f.main,r.receipt->Message())&&f.browser_data.Observed(ticket)&&!f.browser_data.Publish(ticket)&&f.renderer.Physical()==1);r.receipt.reset();f.Close();++groups;}
 {Fixture f;auto r=f.Receive();auto op=f.Operation();auto credential=f.Credentials(*op);auto ticket=f.Ticket(*op,r.receipt->Pin());assert(f.browser_data.Received(f.browser_peer,PID_RENDERER,f.main,r.receipt->Message()));f.now=10;assert(!f.browser_data.CredentialsCompleted(credential,true));f.now=2;assert(!f.browser_data.CredentialsCompleted(credential,true)&&!f.browser_data.Publish(ticket));r.receipt.reset();f.Close();++groups;}
 {Fixture f;auto r=f.Receive();auto op=f.Operation();auto credential=f.Credentials(*op);assert(f.browser_data.CredentialsCompleted(credential,true));auto ticket=f.Ticket(*op,r.receipt->Pin());assert(!f.browser_data.Received(f.browser_peer,PID_BROWSER,f.main,r.receipt->Message()));assert(!f.browser_data.Received(f.browser_peer,PID_RENDERER,f.child,r.receipt->Message()));auto wrong=suitecut::InitStartupSemanticMessage::Build(r.receipt->Pin(),1,false);assert(!f.browser_data.Received(f.browser_peer,PID_RENDERER,f.main,wrong));auto malformed=r.receipt->Message();malformed->GetArgumentList()->SetBool(6,false);assert(!f.browser_data.Received(f.browser_peer,PID_RENDERER,f.main,malformed)&&!f.browser_data.Publish(ticket));r.receipt.reset();f.Close();++groups;}
 {Fixture f;auto r=f.Receive();auto op=f.Operation();auto credential=f.Credentials(*op);assert(f.browser_data.CredentialsCompleted(credential,true));auto ticket=f.Ticket(*op,r.receipt->Pin());auto other=suitecut::InitStartupBrowserPeer::Observe(f.owner,f.browser,f.main);assert(!f.browser_data.Received(other,PID_RENDERER,f.main,r.receipt->Message()));assert(f.browser_data.Received(f.browser_peer,PID_RENDERER,f.main,r.receipt->Message()));f.generation=2;assert(!f.browser_data.Publish(ticket));r.receipt.reset();f.Close();++groups;}
 {Fixture f;auto r=f.Receive();auto op=f.Operation();auto credential=f.Credentials(*op);assert(f.browser_data.CredentialsCompleted(credential,true));auto ticket=f.Ticket(*op,r.receipt->Pin());assert(f.browser_data.Received(f.browser_peer,PID_RENDERER,f.main,r.receipt->Message()));auto reset=f.Credentials(*op,2,1);assert(reset&&!f.browser_data.Publish(ticket));assert(!f.browser_data.CredentialsCompleted(credential,true));auto newer=f.Receive({f.owner,2,2},Body(1,true));auto next=f.Ticket(*op,newer.receipt->Pin(),2,1,true);assert(f.browser_data.CredentialsCompleted(reset,true)&&f.browser_data.Received(f.browser_peer,PID_RENDERER,f.main,newer.receipt->Message())&&f.browser_data.Publish(next));r.receipt.reset();newer.receipt.reset();f.Close();++groups;}
 {Fixture f;auto r=f.Receive();auto op=f.Operation();auto credential=f.Credentials(*op);assert(f.browser_data.CredentialsCompleted(credential,true));auto ticket=f.Ticket(*op,r.receipt->Pin());assert(f.browser_data.Received(f.browser_peer,PID_RENDERER,f.main,r.receipt->Message()));f.on_query=[&]{f.now=10;};assert(!f.browser_data.Publish(ticket));f.on_query={};f.now=2;assert(f.browser_data.Publish(ticket));r.receipt.reset();f.Close();++groups;}
 {Fixture f;auto r=f.Receive();auto op=f.Operation();Browser foreign{{[]{return 1;},[](int,uint64_t){return true;}}};assert(!foreign.ResetCredentials(f.browser_peer,*op,1,0));auto credential=f.Credentials(*op);assert(credential&&!f.browser_data.CredentialsCompleted(credential,false)&&!f.browser_data.CredentialsCompleted(credential,true));auto ticket=f.Ticket(*op,r.receipt->Pin());assert(f.browser_data.Received(f.browser_peer,PID_RENDERER,f.main,r.receipt->Message())&&!f.browser_data.Publish(ticket)&&!foreign.Publish(ticket));r.receipt.reset();f.Close();++groups;}
 {Fixture f;auto r=f.Receive();auto op=f.Operation();auto credential=f.Credentials(*op);auto ticket=f.Ticket(*op,r.receipt->Pin());assert(ticket&&f.browser_data.Forget(ticket)&&!f.browser_data.Pending()&&f.renderer.Physical()==1);assert(!f.browser_data.Received(f.browser_peer,PID_RENDERER,f.main,r.receipt->Message()));f.Close();assert(f.renderer.Physical()==1&&f.renderer.TakeReleased().empty());r.receipt.reset();assert(f.renderer.TakeReleased().size()==1&&!f.renderer.Physical());++groups;}
 {Fixture f;auto r=f.Receive();auto op=f.Operation();auto credential=f.Credentials(*op);assert(f.browser_data.CredentialsCompleted(credential,true));auto ticket=f.Ticket(*op,r.receipt->Pin());assert(f.browser_data.Received(f.browser_peer,PID_RENDERER,f.main,r.receipt->Message()));f.browser->valid=false;assert(!f.browser_data.Publish(ticket));r.receipt.reset();f.Close();++groups;}
 {Fixture f;auto old=f.Receive();f.browser->on_valid=[&]{if(f.renderer.Physical()>1)f.browser->valid=false;};auto rejected=f.Receive({f.owner,2,2},Body(0,false,"new()"));assert(rejected.status==Status::Invalidated&&rejected.mapping_observed&&!rejected.receipt&&!f.renderer.Latest(f.renderer_peer)&&f.renderer.Physical()==2);f.browser->on_valid={};f.browser->valid=true;assert(f.renderer.Latest(f.renderer_peer)->Pin()==old.receipt->Pin()&&f.renderer.Physical()==2);old.receipt.reset();f.Close();assert(!f.renderer.Physical());++groups;}
 {Fixture f;auto op=f.Operation();auto credential=f.Credentials(*op);assert(credential);std::shared_ptr<Browser::Ticket>first;for(uint64_t i=1;i<=64;++i){auto ticket=f.Ticket(*op,{f.owner,i,i});assert(ticket);if(i==1)first=ticket;}assert(f.browser_data.Pending()==64&&!f.Ticket(*op,{f.owner,65,65}));assert(f.browser_data.Forget(first)&&f.browser_data.Pending()==63&&f.Ticket(*op,{f.owner,65,65})&&f.browser_data.Pending()==64);assert(!f.renderer.Physical());f.Close();++groups;}
 {Fixture f;auto actual=f.Receive();auto duplicate=suitecut::InitStartupRendererPeer::Observe(f.owner,f.browser,f.main);assert(duplicate&&!f.renderer.Latest(duplicate)&&!f.renderer.Admit(duplicate));assert(f.renderer.Latest(f.renderer_peer)->Pin()==actual.receipt->Pin());actual.receipt.reset();f.Close();++groups;}
 std::printf("Startup adapters: %d controlled groups PASS (real CEF mappings/messages; native identities controlled, no bootstrap/capture)\n",groups);
}
