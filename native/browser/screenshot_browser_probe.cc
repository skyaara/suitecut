// SPDX-License-Identifier: MIT
// Isolated genuine CEF initialized-codec/paint capability probe. It is never
// linked into the production browser; controlled image tests run on actual UI.
#include "screenshot_compare_control_test.h"
#include "include/cef_api_hash.h"
#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_client.h"
#include "include/cef_command_line.h"
#include "include/cef_load_handler.h"
#include "include/cef_render_handler.h"
#include "include/cef_task.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <string>
#include <unistd.h>
#include <vector>
namespace {
class Task:public CefTask{public:explicit Task(std::function<void()> work):work_(std::move(work)){}void Execute()override{work_();}private:std::function<void()> work_;IMPLEMENT_REFCOUNTING(Task);};
struct Result{bool codecs=false,paint=false;size_t paints=0;int invalidations=0;};
class Client:public CefClient,public CefRenderHandler,public CefLifeSpanHandler,public CefLoadHandler{
 public:
  explicit Client(Result& result):result_(result){}
  CefRefPtr<CefRenderHandler> GetRenderHandler()override{return this;}
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler()override{return this;}
  CefRefPtr<CefLoadHandler> GetLoadHandler()override{return this;}
  void GetViewRect(CefRefPtr<CefBrowser>,CefRect& rect)override{rect=CefRect(0,0,32,24);}
  void OnAfterCreated(CefRefPtr<CefBrowser> browser)override{browser_=browser;}
  void OnBeforeClose(CefRefPtr<CefBrowser>)override{browser_=nullptr;CefQuitMessageLoop();}
  void OnLoadEnd(CefRefPtr<CefBrowser>,CefRefPtr<CefFrame> frame,int)override{
    if(!frame->IsMain())return;
    CefPostTask(TID_UI,new Task([self=CefRefPtr<Client>(this)]{self->Invalidate();}));
  }
  void OnPaint(CefRefPtr<CefBrowser>,PaintElementType type,const RectList&,const void* buffer,int width,int height)override{
    if(type!=PET_VIEW||width!=32||height!=24)return;
    ++result_.paints;const auto* data=static_cast<const uint8_t*>(buffer);const size_t center=(size_t(height/2)*width+width/2)*4;
    if(data[center]!=0||data[center+1]!=255||data[center+2]!=0||data[center+3]!=255||!waiting_)return;
    waiting_=false;
    if(first_.empty()){
      first_.assign(data,data+size_t(width)*height*4);
      CefPostTask(TID_UI,new Task([self=CefRefPtr<Client>(this)]{self->Invalidate();}));
    }else{
      assert(result_.invalidations==2&&result_.paints>first_paint_);
      assert(std::vector<uint8_t>(data,data+size_t(width)*height*4)==first_);result_.paint=true;
      CefPostTask(TID_UI,new Task([self=CefRefPtr<Client>(this)]{if(self->browser_)self->browser_->GetHost()->CloseBrowser(true);}));
    }
  }
  void Invalidate(){if(!browser_)return;waiting_=true;if(first_.empty())first_paint_=result_.paints;++result_.invalidations;browser_->GetHost()->Invalidate(PET_VIEW);}
  void Expire(){if(browser_)browser_->GetHost()->CloseBrowser(true);else CefQuitMessageLoop();}
 private:Result& result_;CefRefPtr<CefBrowser> browser_;std::vector<uint8_t> first_;size_t first_paint_=0;bool waiting_=false;IMPLEMENT_REFCOUNTING(Client);
};
class App:public CefApp,public CefBrowserProcessHandler{
 public:explicit App(Result& result):result_(result){}
  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler()override{return this;}
  void OnBeforeCommandLineProcessing(const CefString&,CefRefPtr<CefCommandLine> command)override{
    command->AppendSwitchWithValue("ozone-platform","headless");command->AppendSwitch("disable-gpu");command->AppendSwitchWithValue("use-gl","angle");command->AppendSwitchWithValue("use-angle","swiftshader");command->AppendSwitch("enable-unsafe-swiftshader");
  }
  void OnContextInitialized()override{
    assert(CefCurrentlyOn(TID_UI));RunScreenshotComparisonControlTests();result_.codecs=true;
    client_=new Client(result_);CefWindowInfo window;window.SetAsWindowless(0);window.runtime_style=CEF_RUNTIME_STYLE_ALLOY;CefBrowserSettings settings;settings.windowless_frame_rate=120;
    assert(CefBrowserHost::CreateBrowser(window,client_,"data:text/html,<style>html,body{margin:0;background:%2300ff00}</style>",settings,nullptr,nullptr));
    CefPostDelayedTask(TID_UI,new Task([client=client_]{client->Expire();}),10000);
  }
  void Clear(){client_=nullptr;}
 private:Result& result_;CefRefPtr<Client> client_;IMPLEMENT_REFCOUNTING(App);
};
__attribute__((noinline)) int Browser(const CefMainArgs& args,Result& result,CefRefPtr<App> app){
  const auto root=std::filesystem::temp_directory_path()/("suitecut-screenshot-component-"+std::to_string(getpid()));std::filesystem::create_directories(root);CefSettings settings;CefString(&settings.root_cache_path)=root.string();settings.windowless_rendering_enabled=true;settings.log_severity=LOGSEVERITY_DISABLE;
  if(!CefInitialize(args,settings,app,nullptr))return 1;
  CefRunMessageLoop();app->Clear();CefShutdown();std::filesystem::remove_all(root);
  std::printf("{\"codecs\":%s,\"paint\":%s,\"actualPaints\":%zu,\"invalidations\":%d}\n",result.codecs?"true":"false",result.paint?"true":"false",result.paints,result.invalidations);return result.codecs&&result.paint?0:2;
}
}
__attribute__((noinline,no_stack_protector)) int main(int argc,char** argv){assert(cef_api_hash(CEF_API_VERSION,0));CefMainArgs args(argc,argv);Result result;CefRefPtr<App> app=new App(result);const auto code=CefExecuteProcess(args,app,nullptr);if(code>=0)return code;return Browser(args,result,app);}
