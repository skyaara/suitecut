// SPDX-License-Identifier: MIT
#pragma once
#include "runtime/engine.h"
namespace suitecut::browser {
class App : public CefApp, public CefBrowserProcessHandler {
 public:
  explicit App(Writer& writer);
#if defined(__linux__)
  void BindAcceptedEndpoint(suitecut::runner::RetainedSourceEndpoint* endpoint) {
    native_work_endpoint_ = endpoint;
  }
#endif
  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override;
  void OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar) override;
  void OnBeforeCommandLineProcessing(const CefString&, CefRefPtr<CefCommandLine> command) override;
  void OnContextInitialized() override;
  void ReleaseSource();

 private:
  Writer& writer_;
  std::unique_ptr<Engine> engine_;
  const double startup_started_;
#if defined(__linux__)
  suitecut::runner::RetainedSourceEndpoint* native_work_endpoint_ = nullptr;
#endif
  IMPLEMENT_REFCOUNTING(App);
};

}  // namespace suitecut::browser
