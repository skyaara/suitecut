#include <cstdio>
#include <csignal>
#include "include/base/cef_compiler_specific.h"
#include "include/cef_app.h"
#include "include/cef_client.h"

class Client final : public CefClient, public CefLifeSpanHandler, public CefRenderHandler, public CefLoadHandler, public CefRequestHandler, public CefDisplayHandler {
 public:
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override{return this;}
  CefRefPtr<CefRenderHandler> GetRenderHandler() override{return this;}
  CefRefPtr<CefLoadHandler> GetLoadHandler() override{return this;}
  CefRefPtr<CefRequestHandler> GetRequestHandler() override{return this;}
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override{return this;}
  void GetViewRect(CefRefPtr<CefBrowser>,CefRect& r) override{r=CefRect(0,0,640,360);}
  void OnPaint(CefRefPtr<CefBrowser>,PaintElementType,const RectList&,const void*,int,int) override{}
  bool OnConsoleMessage(CefRefPtr<CefBrowser> browser,cef_log_severity_t,const CefString& msg,const CefString&,int) override{std::fprintf(stderr,"%s\n",msg.ToString().c_str());return false;}
  void OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser,TerminationStatus status,int code,const CefString&) override{std::fprintf(stderr,"SRC_CRASH:status=%d exit=%d\n",int(status),code);browser->GetHost()->CloseBrowser(true);}
  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override{if(browser->GetIdentifier()==1)CefQuitMessageLoop();}
 private:bool started_=false;IMPLEMENT_REFCOUNTING(Client);
};
class App final:public CefApp,public CefBrowserProcessHandler{
 public:
  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override{return this;}
  void OnBeforeCommandLineProcessing(const CefString&,CefRefPtr<CefCommandLine> command) override{command->AppendSwitch("no-first-run");}
  void OnContextInitialized() override{
    CefWindowInfo window;if(!CefCommandLine::GetGlobalCommandLine()->HasSwitch("windowed"))window.SetAsWindowless(0);
    CefBrowserSettings settings;settings.windowless_frame_rate=30;
    CefBrowserHost::CreateBrowser(window,new Client(),"about:blank",settings,nullptr,nullptr);
  }
 private:IMPLEMENT_REFCOUNTING(App);
};
NO_STACK_PROTECTOR
int main(int argc,char**argv){signal(SIGPIPE,SIG_IGN);CefMainArgs args(argc,argv);int subprocess=CefExecuteProcess(args,nullptr,nullptr);if(subprocess>=0)return subprocess;CefSettings settings;settings.windowless_rendering_enabled=true;settings.log_severity=LOGSEVERITY_ERROR;auto cmd=CefCommandLine::CreateCommandLine();cmd->InitFromArgv(argc,argv);CefString(&settings.root_cache_path)=cmd->GetSwitchValue("profile");CefRefPtr<App>app=new App();if(!CefInitialize(args,settings,app,nullptr))return 3;CefRunMessageLoop();app=nullptr;CefShutdown();return 0;}
