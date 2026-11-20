// SPDX-License-Identifier: MIT
// Prompts that go to the person (person_prompts.h): file choosers, permission
// and media requests, HTTP authentication and client certificates. Each one
// is held for a window's answer only with --suitecut-host-events=1 and while
// the session has subscribed the page; otherwise today's policy answers it,
// as it always has.
#include "page/source.h"

#include "context_auth_policy.h"
#include "context_permissions.h"
#include "include/cef_parser.h"

#include <cstdlib>

namespace suitecut::browser {
namespace {

// A person's answer to a Fetch.authRequired challenge, as CEF's auth callback.
class FetchAuthCallback : public CefAuthCallback {
 public:
  explicit FetchAuthCallback(std::function<void(CefRefPtr<CefDictionaryValue>)> answer) : answer_(std::move(answer)) {}
  void Continue(const CefString& username, const CefString& password) override {
    auto decision = CefDictionaryValue::Create();
    decision->SetString("response", "ProvideCredentials");
    decision->SetString("username", username);
    decision->SetString("password", password);
    Send(decision);
  }
  void Cancel() override {
    auto decision = CefDictionaryValue::Create();
    decision->SetString("response", "CancelAuth");
    Send(decision);
  }

 private:
  void Send(CefRefPtr<CefDictionaryValue> decision) {
    if (!answer_) return;
    auto answer = std::move(answer_);
    answer_ = nullptr;
    answer(decision);
  }
  std::function<void(CefRefPtr<CefDictionaryValue>)> answer_;
  IMPLEMENT_REFCOUNTING(FetchAuthCallback);
};

// Names a permission request's bits as the SDK's grant names.
CefRefPtr<CefListValue> PermissionNames(uint32_t requested) {
  auto names = CefListValue::Create();
  for (uint32_t bit = 1; bit && bit <= requested; bit <<= 1) {
    if (!(requested & bit)) continue;
    std::string_view name;
    for (const auto& entry : suitecut::NativePermissionDefinitions())
      if (entry.flags == bit) {
        name = entry.name;
        break;
      }
    if (name.empty())
      for (const auto& entry : suitecut::NativePermissionDefinitions())
        if (entry.flags & bit) {
          name = entry.name;
          break;
        }
    names->SetString(names->GetSize(), name.empty() ? std::string("unknown") : std::string(name));
  }
  return names;
}

CefRefPtr<CefListValue> MediaNames(uint32_t requested) {
  auto names = CefListValue::Create();
  if (requested & CEF_MEDIA_PERMISSION_DEVICE_AUDIO_CAPTURE) names->SetString(names->GetSize(), "microphone");
  if (requested & CEF_MEDIA_PERMISSION_DEVICE_VIDEO_CAPTURE) names->SetString(names->GetSize(), "camera");
  if (requested & CEF_MEDIA_PERMISSION_DESKTOP_AUDIO_CAPTURE) names->SetString(names->GetSize(), "display-audio");
  if (requested & CEF_MEDIA_PERMISSION_DESKTOP_VIDEO_CAPTURE) names->SetString(names->GetSize(), "display");
  return names;
}

// {"accept":bool}, or {"cancel":true} for a refusal.
bool Accepted(const CefRefPtr<CefDictionaryValue>& answer, bool& accept) {
  if (!answer) return false;
  if (answer->GetType("accept") == VTYPE_BOOL) {
    accept = answer->GetBool("accept");
    return true;
  }
  if (answer->GetType("cancel") == VTYPE_BOOL && answer->GetBool("cancel")) {
    accept = false;
    return true;
  }
  return false;
}

bool Canceled(const CefRefPtr<CefDictionaryValue>& answer) {
  return answer && answer->GetType("cancel") == VTYPE_BOOL && answer->GetBool("cancel");
}

// Milliseconds since the Unix epoch; CefBaseTime counts microseconds since 1601.
double UnixMs(const CefBaseTime& time) {
  return static_cast<double>((time.val - 11644473600000000LL) / 1000);
}

const char* FileMode(cef_file_dialog_mode_t mode) {
  switch (mode) {
    case FILE_DIALOG_OPEN: return "open";
    case FILE_DIALOG_OPEN_MULTIPLE: return "openMultiple";
    case FILE_DIALOG_OPEN_FOLDER: return "openFolder";
    case FILE_DIALOG_SAVE: return "save";
    default: return "open";
  }
}
}  // namespace

auto Source::PersonPromptsCreate() -> void {
  if (!HostEventsEnabled()) return;
  person_prompts_ = std::make_unique<SuiteCutPersonPrompts>([this](Dict info) { HostEvent("prompt", info); });
}

auto Source::PersonPromptsOn() const -> bool { return person_prompts_ && person_prompts_->On(); }

auto Source::FilePrompt(cef_file_dialog_mode_t mode, const CefString& title, const CefString& default_path,
                        const std::vector<CefString>& filters, CefRefPtr<CefFileDialogCallback> callback) -> bool {
  if (!PersonPromptsOn()) return false;
  auto info = CefDictionaryValue::Create();
  info->SetString("mode", FileMode(mode));
  info->SetString("title", title);
  info->SetString("defaultPath", default_path);
  auto accept = CefListValue::Create();
  for (const auto& filter : filters) accept->SetString(accept->GetSize(), filter);
  info->SetList("accept", accept);
  SuiteCutPersonPrompts::Hold hold;
  hold.document = true;
  hold.answer = [callback, mode](Dict answer) -> std::string {
    if (Canceled(answer)) {
      callback->Cancel();
      return "";
    }
    if (!answer || answer->GetType("paths") != VTYPE_LIST) return "A file chooser's answer has paths or cancel";
    const auto list = answer->GetList("paths");
    std::vector<CefString> paths;
    for (size_t i = 0; i < list->GetSize(); ++i) {
      if (list->GetType(i) != VTYPE_STRING || list->GetString(i).empty()) return "Each path is a string";
      paths.push_back(list->GetString(i));
    }
    if (paths.empty()) return "Pick at least one path, or cancel";
    if (mode != FILE_DIALOG_OPEN_MULTIPLE && paths.size() > 1) return "This file chooser takes one path";
    callback->Continue(paths);
    return "";
  };
  // Today, with nobody to ask, a page's own file chooser opens nothing.
  hold.policy = [callback] { callback->Cancel(); };
  hold.cancel = [callback] { callback->Cancel(); };
  return person_prompts_->Show("file", info, std::move(hold));
}

auto Source::PermissionPrompt(uint64_t prompt_id, const CefString& origin, uint32_t requested,
                              CefRefPtr<CefPermissionPromptCallback> callback) -> bool {
  if (!PersonPromptsOn()) return false;
  auto info = CefDictionaryValue::Create();
  info->SetString("origin", origin);
  info->SetList("permissions", PermissionNames(requested));
  SuiteCutPersonPrompts::Hold hold;
  hold.key = "permission:" + std::to_string(prompt_id);
  hold.answer = [callback](Dict answer) -> std::string {
    bool accept = false;
    if (!Accepted(answer, accept)) return "A permission's answer has accept: true or false";
    callback->Continue(accept ? CEF_PERMISSION_RESULT_ACCEPT : CEF_PERMISSION_RESULT_DENY);
    return "";
  };
  hold.policy = [this, callback, origin = origin.ToString(), requested] {
    const auto granted = engine_.contexts()->AllowedPermissions(page_id_, origin);
    callback->Continue(suitecut::NativePermissionRequestGranted(requested, granted) ? CEF_PERMISSION_RESULT_ACCEPT
                                                                                      : CEF_PERMISSION_RESULT_DENY);
  };
  hold.cancel = [callback] { callback->Continue(CEF_PERMISSION_RESULT_IGNORE); };
  return person_prompts_->Show("permission", info, std::move(hold));
}

auto Source::MediaPrompt(const CefString& origin, uint32_t requested, CefRefPtr<CefMediaAccessCallback> callback)
    -> bool {
  if (!PersonPromptsOn()) return false;
  auto info = CefDictionaryValue::Create();
  info->SetString("origin", origin);
  info->SetList("permissions", MediaNames(requested));
  SuiteCutPersonPrompts::Hold hold;
  hold.document = true;
  hold.answer = [callback, requested](Dict answer) -> std::string {
    bool accept = false;
    if (!Accepted(answer, accept)) return "A permission's answer has accept: true or false";
    callback->Continue(accept ? requested : 0);
    return "";
  };
  hold.policy = [this, callback, origin = origin.ToString(), requested] {
    callback->Continue(requested & engine_.contexts()->AllowedMediaPermissions(page_id_, origin));
  };
  hold.cancel = [callback] { callback->Cancel(); };
  return person_prompts_->Show("permission", info, std::move(hold));
}

auto Source::OnDismissPermissionPrompt(CefRefPtr<CefBrowser> browser, uint64_t prompt_id,
                                       cef_permission_request_result_t result) -> void {
  if (auto tab = AdoptedTab(browser)) return tab->OnDismissPermissionPrompt(browser, prompt_id, result);
  if (person_prompts_) person_prompts_->Dismissed("permission:" + std::to_string(prompt_id));
}

auto Source::OnSelectClientCertificate(CefRefPtr<CefBrowser> browser, bool proxy, const CefString& host, int port,
                                       const X509CertificateList& certificates,
                                       CefRefPtr<CefSelectClientCertificateCallback> callback) -> bool {
  if (auto tab = AdoptedTab(browser))
    return tab->OnSelectClientCertificate(browser, proxy, host, port, certificates, callback);
  // With none to choose from, or nobody to ask, CEF's default stays.
  if (!PersonPromptsOn() || certificates.empty()) return false;
  auto info = CefDictionaryValue::Create();
  info->SetString("host", host);
  info->SetInt("port", port);
  info->SetBool("proxy", proxy);
  auto list = CefListValue::Create();
  for (size_t i = 0; i < certificates.size(); ++i) {
    const auto& certificate = certificates[i];
    auto row = CefDictionaryValue::Create();
    row->SetInt("index", static_cast<int>(i));
    const auto subject = certificate->GetSubject();
    const auto issuer = certificate->GetIssuer();
    row->SetString("subject", subject ? subject->GetDisplayName() : CefString());
    row->SetString("issuer", issuer ? issuer->GetDisplayName() : CefString());
    row->SetDouble("validFrom", UnixMs(certificate->GetValidStart()));
    row->SetDouble("expires", UnixMs(certificate->GetValidExpiry()));
    list->SetDictionary(i, row);
  }
  info->SetList("certificates", list);
  SuiteCutPersonPrompts::Hold hold;
  hold.answer = [callback, certificates](Dict answer) -> std::string {
    if (Canceled(answer)) {
      callback->Select(nullptr);
      return "";
    }
    if (!answer || answer->GetType("index") != VTYPE_INT) return "A certificate's answer has index or cancel";
    const int index = answer->GetInt("index");
    if (index < 0 || static_cast<size_t>(index) >= certificates.size()) return "No certificate has that index";
    callback->Select(certificates[static_cast<size_t>(index)]);
    return "";
  };
  // Held, CEF's default can no longer run: continue without a certificate.
  hold.policy = [callback] { callback->Select(nullptr); };
  hold.cancel = [callback] { callback->Select(nullptr); };
  return person_prompts_->Show("certificate", info, std::move(hold));
}

// On the UI thread, from GetAuthCredentials. |agent| gives the context's
// credentials, if any match, to CEF; |policy| is today's answer.
auto Source::AuthPrompt(CefRefPtr<CefBrowser> browser, Dict info, bool agent_credentials,
                        CefRefPtr<CefAuthCallback> callback, std::function<void()> policy) -> void {
  if (auto tab = AdoptedTab(browser))
    return tab->AuthPrompt(browser, info, agent_credentials, callback, std::move(policy));
  if (!PersonPromptsOn() || closing_page_) {
    policy();
    return;
  }
  // The agent's credentials answer a challenge once; if it comes again they
  // were rejected, and the person is asked.
  const auto key = SuiteCutAgentAuthTries::Key(info->GetBool("proxy"), info->GetString("host"), info->GetInt("port"),
                                               info->GetString("realm"), info->GetString("scheme"));
  const bool agent = agent_credentials && agent_auth_tries_.Untried(key);
  info->SetBool("agentCredentials", agent);
  SuiteCutPersonPrompts::Hold hold;
  hold.answer = [callback](Dict answer) -> std::string {
    if (Canceled(answer)) {
      callback->Cancel();
      return "";
    }
    if (!answer || answer->GetType("username") != VTYPE_STRING || answer->GetType("password") != VTYPE_STRING)
      return "An authentication answer has username and password, or cancel";
    callback->Continue(answer->GetString("username"), answer->GetString("password"));
    return "";
  };
  // The session answers {"policy":true} for the agent's tab when |agent|:
  // that is the agent's try.
  hold.policy = [this, policy, agent, key] {
    if (agent) agent_auth_tries_.Tried(key);
    policy();
  };
  hold.cancel = [callback] { callback->Cancel(); };
  if (!person_prompts_->Show("auth", info, std::move(hold))) policy();
}

auto Source::FetchAuthPrompt(Dict input, Dict today, std::function<void(Dict)> answer) -> void {
  const auto request = input ? input->GetDictionary("request") : nullptr;
  const auto challenge = input ? input->GetDictionary("authChallenge") : nullptr;
  CefRefPtr<CefAuthCallback> callback = new FetchAuthCallback(std::move(answer));
  const bool provides = today && today->GetType("response") == VTYPE_STRING &&
                        today->GetString("response") == "ProvideCredentials";
  auto policy = [callback, today, provides] {
    if (provides)
      callback->Continue(today->GetString("username"), today->GetString("password"));
    else
      callback->Cancel();
  };
  if (!request || !challenge || !browser_) {
    policy();
    return;
  }
  auto info = CefDictionaryValue::Create();
  info->SetString("url", request->GetString("url"));
  info->SetBool("proxy", challenge->GetString("source") == "Proxy");
  CefURLParts parts;
  int port = 0;
  std::string host;
  if (CefParseURL(challenge->GetString("origin"), parts)) {
    host = CefString(&parts.host).ToString();
    const auto port_text = CefString(&parts.port).ToString();
    const auto scheme = CefString(&parts.scheme).ToString();
    port = !port_text.empty() ? std::atoi(port_text.c_str()) : scheme == "https" ? 443 : 80;
  }
  info->SetString("host", host);
  info->SetInt("port", port);
  info->SetString("realm", challenge->GetString("realm"));
  info->SetString("scheme", challenge->GetString("scheme"));
  AuthPrompt(browser_, info, provides, callback, std::move(policy));
}

// {"command":"host","action":"prompts","enabled":bool} and
// {"command":"host","action":"answer","prompt":ID,"answer":{...}}.
auto Source::PersonPromptCommand(int id, const std::string& action, Dict message) -> void {
  if (!person_prompts_) {
    Complete(id, false, "This engine holds no prompts for a host");
    return;
  }
  if (action == "prompts") {
    if (message->GetType("enabled") != VTYPE_BOOL) {
      Complete(id, false, "Prompts are enabled: true or false");
      return;
    }
    person_prompts_->Subscribe(message->GetBool("enabled"));
    Complete(id, true);
    return;
  }
  if (message->GetType("prompt") != VTYPE_STRING || message->GetType("answer") != VTYPE_DICTIONARY) {
    Complete(id, false, "An answer names its prompt and has an answer");
    return;
  }
  std::string error;
  const bool ok = person_prompts_->Answer(message->GetString("prompt"), message->GetDictionary("answer"), error);
  Complete(id, ok, error);
}

}  // namespace suitecut::browser
