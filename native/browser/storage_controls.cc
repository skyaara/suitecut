// SPDX-License-Identifier: MIT
#include "storage_controls.h"
#include "accepted_task_work.h"
#include "locator_handler_async_work.h"
#include "storage_scripts.h"
#include "storage_public_codec.h"
#include "storage_utf16.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <set>
#include <utility>
#include "include/cef_parser.h"
#include "include/cef_task.h"
namespace {
using Dict = SuiteCutStorageControls::Dict;
constexpr size_t kMaxState = 8 * 1048576, kBudget = 32 * 1048576, kMaxOrigin = 512 * 1024;
std::string JSON(Dict value) { auto wrapper = CefValue::Create(); wrapper->SetDictionary(value); return CefWriteJSON(wrapper, JSON_WRITER_DEFAULT); }
std::string StorageOrigin(const std::string& value) {
  CefURLParts parts; if (!CefParseURL(value, parts)) return {};
  const auto scheme = CefString(&parts.scheme).ToString(), host = CefString(&parts.host).ToString(), port = CefString(&parts.port).ToString();
  if ((scheme != "http" && scheme != "https") || host.empty() || !CefString(&parts.username).empty() || !CefString(&parts.password).empty() ||
      !CefString(&parts.query).empty() || !CefString(&parts.fragment).empty() || (CefString(&parts.path) != "/" && !CefString(&parts.path).empty())) return {};
  return scheme + "://" + host + (port.empty() || (scheme == "http" && port == "80") || (scheme == "https" && port == "443") ? "" : ":" + port);
}
bool ValidateKeyPath(CefRefPtr<CefValue> value, bool nullable) {
  if (!value) return false;
  if (value->GetType() == VTYPE_NULL) return nullable;
  if (value->GetType() == VTYPE_STRING) return true;
  if (value->GetType() != VTYPE_LIST) return false;
  const auto list = value->GetList(); if (!list || list->GetSize() > 128) return false;
  for (size_t index = 0; index < list->GetSize(); ++index) if (list->GetType(index) != VTYPE_STRING) return false;
  return true;
}
bool ValidateOPFS(Dict value) {
  if (!value->HasKey("opfs")) return true;
  const auto entries = value->GetList("opfs"); if (!entries || entries->GetSize() > 4096) return false;
  std::map<std::string, bool> paths;
  for (size_t index = 0; index < entries->GetSize(); ++index) {
    const auto entry = entries->GetDictionary(index); if (!entry || entry->GetType("path") != VTYPE_STRING || entry->GetType("type") != VTYPE_STRING) return false;
    const auto path = entry->GetString("path").ToString(), type = entry->GetString("type").ToString();
    if (path.empty() || path.size() > 4096 || path.find('\\') != std::string::npos || path.find('\0') != std::string::npos || (type != "file" && type != "directory") || !paths.emplace(path, type == "directory").second) return false;
    size_t begin = 0, depth = 0;
    while (begin <= path.size()) {
      const auto end = path.find('/', begin); const auto part = path.substr(begin, end == std::string::npos ? end : end - begin);
      if (part.empty() || part == "." || part == ".." || part.size() > 255 || ++depth > 64) return false;
      if (end == std::string::npos) break;
      begin = end + 1;
    }
    if (type == "file") {
      if (entry->GetType("base64") != VTYPE_STRING) return false;
      const auto encoded = entry->GetString("base64").ToString(); if (encoded.size() % 4 != 0) return false;
      for (size_t pos = 0; pos < encoded.size(); ++pos) {
        const auto ch = encoded[pos]; const bool alphabet = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '+' || ch == '/';
        if (!alphabet && !(ch == '=' && pos >= encoded.size() - 2)) return false;
        if (ch == '=' && pos + 1 < encoded.size() && encoded[pos + 1] != '=') return false;
      }
    } else if (entry->HasKey("base64")) return false;
  }
  for (const auto& [path, _] : paths) for (size_t pos = path.find('/'); pos != std::string::npos; pos = path.find('/', pos + 1)) {
    const auto parent = paths.find(path.substr(0, pos)); if (parent != paths.end() && !parent->second) return false;
  }
  return true;
}
bool ValidateOrigin(Dict value, bool strict_names = true) {
  if (value && value->GetType("stateJSON") == VTYPE_STRING) {
    const auto text = value->GetString("stateJSON").ToString();
    if (text.size() > kMaxOrigin || value->GetType("origin") != VTYPE_STRING) return false;
    const auto parsed = CefParseJSON(suitecut::StorageJSONValidationProjection(text), JSON_PARSER_RFC);
    const auto origin = parsed && parsed->GetType() == VTYPE_DICTIONARY ? parsed->GetDictionary() : nullptr;
    return origin && !origin->HasKey("stateJSON") && origin->GetString("origin") == value->GetString("origin") && ValidateOrigin(origin, false);
  }
  if (!value || value->GetType("origin") != VTYPE_STRING || StorageOrigin(value->GetString("origin").ToString()).empty()) return false;
  const auto storage = value->GetList("localStorage"); if (!storage || storage->GetSize() > 4096) return false;
  std::set<std::string> names;
  for (size_t index = 0; index < storage->GetSize(); ++index) {
    const auto entry = storage->GetDictionary(index);
    if (!entry || entry->GetType("name") != VTYPE_STRING || entry->GetType("value") != VTYPE_STRING || (strict_names && !names.insert(entry->GetString("name").ToString()).second)) return false;
  }
  if (JSON(value).size() > kMaxOrigin || !ValidateOPFS(value)) return false;
  if (!value->HasKey("indexedDB")) return true;
  const auto databases = value->GetList("indexedDB"); if (!databases || databases->GetSize() > 128) return false;
  std::set<std::string> db_names; size_t records = 0;
  for (size_t index = 0; index < databases->GetSize(); ++index) {
    const auto db = databases->GetDictionary(index);
    if (!db || db->GetType("name") != VTYPE_STRING || (strict_names && !db_names.insert(db->GetString("name").ToString()).second) || (db->GetType("version") != VTYPE_INT && db->GetType("version") != VTYPE_DOUBLE)) return false;
    const double version = db->GetType("version") == VTYPE_INT ? db->GetInt("version") : db->GetDouble("version");
    if (!std::isfinite(version) || std::floor(version) != version || version < 1 || version > 9007199254740991.0) return false;
    const auto stores = db->GetList("stores"); if (!stores || stores->GetSize() > 128) return false;
    std::set<std::string> store_names;
    for (size_t store_index = 0; store_index < stores->GetSize(); ++store_index) {
      const auto store = stores->GetDictionary(store_index); if (!store || store->GetType("name") != VTYPE_STRING || (strict_names && !store_names.insert(store->GetString("name").ToString()).second) || store->GetType("autoIncrement") != VTYPE_BOOL) return false;
      const auto key_path = store->GetType("keyPath"); if (!ValidateKeyPath(store->GetValue("keyPath"), true)) return false;
      const auto indexes = store->GetList("indexes"), entries = store->GetList("records");
      if (!indexes || !entries || indexes->GetSize() > 128 || entries->GetSize() > 10000 - records) return false;
      records += entries->GetSize(); std::set<std::string> index_names;
      for (size_t entry_index = 0; entry_index < entries->GetSize(); ++entry_index) {
        const auto entry = entries->GetDictionary(entry_index); if (!entry || entry->HasKey("value") == entry->HasKey("valueEncoded") || (key_path == VTYPE_NULL && entry->HasKey("key") == entry->HasKey("keyEncoded"))) return false;
      }
      for (size_t index_index = 0; index_index < indexes->GetSize(); ++index_index) {
        const auto entry = indexes->GetDictionary(index_index);
        if (!entry || entry->GetType("name") != VTYPE_STRING || (strict_names && !index_names.insert(entry->GetString("name").ToString()).second) ||
            entry->GetType("unique") != VTYPE_BOOL || entry->GetType("multiEntry") != VTYPE_BOOL || !ValidateKeyPath(entry->GetValue("keyPath"), false)) return false;
      }
    }
  }
  return true;
}
class StorageTask : public CefTask {
 public:
  explicit StorageTask(std::function<void()> fn) : fn_(std::move(fn)) {}
  void Execute() override{const suitecut::AcceptedTaskFrame native_frame(native_work_);fn_();}
 private:
  suitecut::AcceptedTaskReceipt native_work_;std::function<void()> fn_; IMPLEMENT_REFCOUNTING(StorageTask);
};
}
struct SuiteCutStorageControls::Work {
  suitecut::NativeLocatorHandlerAsyncWork handler_receipt;
  int owner = 0, context = 0, lease = 0; size_t index = 0, bytes = 0;
  bool restore = false, indexed_db = false, opfs = false;
  std::string codec; std::vector<std::string> origins;
  bool include_credentials=false,captured_credentials=false;
  std::map<std::string, Dict> state; Dict output; Reply reply;
};
SuiteCutStorageControls::SuiteCutStorageControls(std::shared_ptr<SuiteCutUtilityBrowsers> utilities, Hooks hooks)
    : utilities_(std::move(utilities)), hooks_(std::move(hooks)) {}
double SuiteCutStorageControls::Now() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
bool SuiteCutStorageControls::Current(const std::shared_ptr<Work>& work) const {
  const auto found = active_.find(work->owner); return found != active_.end() && found->second == work && !stopping_;
}
void SuiteCutStorageControls::Handle(int owner, const std::string& action, Dict params, Reply reply) {
  const int context = params->GetInt("contextId");
  if (stopping_ || owner <= 0 || !hooks_.context(context)) { reply(false, nullptr, "Native storage context is unavailable"); return; }
  if (action == "storageUploadBegin") {
    if (uploads_.size() >= 8 || next_upload_ == std::numeric_limits<uint64_t>::max()) { reply(false, nullptr, "Native storage upload capacity exceeded"); return; }
    const auto id = std::to_string(++next_upload_); uploads_[id] = Upload{context, {}, Now()};
    auto output = CefDictionaryValue::Create(); output->SetString("uploadId", id); reply(true, output, ""); WakeUploads(); return;
  }
  if (action == "storageUploadAppend" || action == "storageUploadDiscard") {
    const auto found = uploads_.find(params->GetString("uploadId").ToString());
    if (found == uploads_.end() || found->second.context != context) { reply(false, nullptr, "Native storage upload is unavailable"); return; }
    if (action == "storageUploadDiscard") { upload_bytes_ -= found->second.json.size(); uploads_.erase(found); reply(true, nullptr, ""); return; }
    const auto chunk = params->GetString("data").ToString();
    if (params->GetType("data") != VTYPE_STRING || chunk.size() > 32768 || chunk.size() > kMaxState - found->second.json.size() || chunk.size() > kBudget - upload_bytes_) {
      reply(false, nullptr, "Native storage upload bytes capacity exceeded"); return;
    }
    found->second.json += chunk; found->second.updated = Now(); upload_bytes_ += chunk.size(); reply(true, nullptr, ""); return;
  }
  if (action != "storageState" && action != "setStorageState") { reply(false, nullptr, "Unknown native storage action"); return; }
  if (active_.size() >= 64 || active_.contains(owner)) { reply(false, nullptr, "Invalid native storage operation or capacity exceeded"); return; }
  auto work = std::make_shared<Work>(); work->owner = owner; work->context = context; work->restore = action == "setStorageState";
  work->include_credentials=params->GetBool("credentials");
  work->indexed_db = params->GetBool("indexedDB"); work->opfs = params->GetBool("opfs"); work->codec = suitecut::StoragePublicCodecSource(); work->reply = std::move(reply);
  work->output = CefDictionaryValue::Create(); work->output->SetList("origins", CefListValue::Create());
  if (!hooks_.origins(context, work->origins)) { work->reply(false, nullptr, "Native storage origin tracking capacity exceeded"); return; }
  Dict state;
  if (work->restore) {
    if (params->HasKey("uploadId")) {
      const auto found = uploads_.find(params->GetString("uploadId").ToString());
      if (found == uploads_.end() || found->second.context != context) { work->reply(false, nullptr, "Native storage upload is unavailable"); return; }
      const auto parsed = CefParseJSON(CefString(found->second.json), JSON_PARSER_RFC);
      state = parsed && parsed->GetType() == VTYPE_DICTIONARY ? parsed->GetDictionary() : nullptr;
      upload_bytes_ -= found->second.json.size(); uploads_.erase(found);
    } else state = params->GetDictionary("state");
    const auto cookies = state ? state->GetList("cookies") : nullptr, origins = state ? state->GetList("origins") : nullptr;
    if (!state || !cookies || !origins || cookies->GetSize() > 4096 || origins->GetSize() > 1024) { work->reply(false, nullptr, "Invalid native storage state"); return; }
    work->bytes = JSON(state).size();
    if (work->bytes > kMaxState || work->bytes > kBudget - retained_bytes_) { work->reply(false, nullptr, "Native storage state retained bytes capacity exceeded"); return; }
    for (size_t index = 0; index < origins->GetSize(); ++index) {
      const auto origin = origins->GetDictionary(index);
      if (!ValidateOrigin(origin)) { work->reply(false, nullptr, "Invalid native origin storage state"); return; }
      const auto key = StorageOrigin(origin->GetString("origin").ToString());
      if (work->state.contains(key)) { work->reply(false, nullptr, "Duplicate native storage origin"); return; }
      auto normalized = origin->Copy(false); normalized->SetString("origin", key); work->state[key] = normalized;
      if (std::find(work->origins.begin(), work->origins.end(), key) == work->origins.end()) work->origins.push_back(key);
    }
    if (work->origins.size() > 1024) { work->reply(false, nullptr, "Native storage origin capacity exceeded"); return; }
    std::string cookie_error;
    if (hooks_.validate_credentials&&!hooks_.validate_credentials(state,cookie_error)){work->reply(false,nullptr,cookie_error);return;}
    if (!(hooks_.validate_context_cookies?hooks_.validate_context_cookies(owner,context,state,cookie_error):hooks_.validate_cookies(state, cookie_error))) { work->reply(false, nullptr, cookie_error); return; }
    work->output->SetList("cookies", cookies->Copy());
  }
  retained_bytes_ += work->bytes; active_[owner] = work;
  if(work->restore&&hooks_.credentials){hooks_.credentials(owner,context,"restore",state,[weak=weak_from_this(),work](bool ok,Dict,const std::string&error){const auto self=weak.lock();if(!self||!self->Current(work))return;if(!ok)self->Finish(work,false,error);else self->Cookies(work);});}
  else Cookies(work);
}
void SuiteCutStorageControls::Cookies(const std::shared_ptr<Work>&work){
  if(!Current(work))return;
  const int owner=work->owner,context=work->context;
  auto cookies_params = CefDictionaryValue::Create();if(work->restore)cookies_params->SetBool("storageInternal",true);
  hooks_.cookies(owner, context, work->restore ? "clearCookies" : "cookies", cookies_params,
      [weak = weak_from_this(), work](bool ok, Dict output, const std::string& error) {
    const auto self = weak.lock(); if (!self || !self->Current(work)) return;
    if (!ok) { self->Finish(work, false, error); return; }
    if (!work->restore) {
      const size_t bytes = JSON(output).size();
      if (bytes > kMaxState - work->bytes || bytes > kBudget - self->retained_bytes_) { self->Finish(work, false, "Native captured cookie bytes capacity exceeded"); return; }
      work->bytes += bytes; self->retained_bytes_ += bytes;
      work->output->SetList("cookies", output->GetList("cookies")); self->Next(work); return;
    }
    auto params = CefDictionaryValue::Create(); params->SetList("cookies", work->output->GetList("cookies")); params->SetBool("storageInternal", true);
    self->hooks_.cookies(work->owner, work->context, "addCookies", params, [weak, work](bool ok, Dict, const std::string& error) {
      const auto self = weak.lock(); if (!self || !self->Current(work)) return;
      if (!ok) self->Finish(work, false, error); else self->Next(work);
    });
  });
}
void SuiteCutStorageControls::Next(const std::shared_ptr<Work>& work) {
  if (!Current(work)) return;
  if (work->index >= work->origins.size()) {
    if(!work->restore&&work->include_credentials&&!work->captured_credentials&&hooks_.credentials){
      work->captured_credentials=true;hooks_.credentials(work->owner,work->context,"capture",nullptr,[weak=weak_from_this(),work](bool ok,Dict output,const std::string&error){const auto self=weak.lock();if(!self||!self->Current(work))return;if(!ok||!output){self->Finish(work,false,error);return;}const size_t bytes=JSON(output).size();if(bytes>kMaxState-work->bytes||bytes>kBudget-self->retained_bytes_){self->Finish(work,false,"Native credential storage bytes capacity exceeded");return;}work->bytes+=bytes;self->retained_bytes_+=bytes;work->output->SetList("credentials",output->GetList("credentials"));self->Next(work);});return;
    }
    if (!work->restore && JSON(work->output).size() > kMaxState) {
      Finish(work, false, "Native captured storage state exceeds 8 MiB"); return;
    }
    Finish(work, true, ""); return;
  }
  const auto origin = work->origins[work->index];
  auto ready = [weak = weak_from_this(), work](CefRefPtr<CefFrame>, const std::string& error) { if (const auto self = weak.lock()) self->ReadyOrigin(work, error); };
  if (work->lease) utilities_->Navigate(work->lease, origin, std::move(ready));
  else {
    physical_[work->owner]=work->context;
    work->lease=utilities_->Acquire(hooks_.context(work->context),origin,std::move(ready));
    if(!work->lease){physical_.erase(work->owner);if(hooks_.settled)hooks_.settled(work->context);}
  }
}
void SuiteCutStorageControls::ReadyOrigin(const std::shared_ptr<Work>& work, const std::string& error) {
  if (!Current(work)) return;
  if (!error.empty()) { Finish(work, false, error); return; }
  std::string expression;
  if (!work->restore) expression = suitecut::StorageCaptureScript(work->codec, work->indexed_db, work->opfs);
  else {
    const auto origin = work->origins[work->index]; auto input = CefDictionaryValue::Create();
    const auto found = work->state.find(origin);
    if (found != work->state.end()) input = found->second;
    else { input->SetString("origin", origin); input->SetList("localStorage", CefListValue::Create()); }
    expression = suitecut::StorageRestoreScript(work->codec, JSON(input), true);
  }
  utilities_->Evaluate(work->lease, expression, [weak = weak_from_this(), work](bool ok, Dict result, const std::string& error) {
    const auto self = weak.lock(); if (!self || !self->Current(work)) return;
    if (!ok) { self->Finish(work, false, error); return; }
    if (!work->restore) {
      const auto remote = result->GetDictionary("result"); const auto origin = remote ? remote->GetDictionary("value") : nullptr;
      if (!ValidateOrigin(origin)) { self->Finish(work, false, "Native captured origin storage exceeds schema or capacity"); return; }
      const bool empty = origin->HasKey("stateJSON") ? origin->GetBool("empty") : origin->GetList("localStorage")->GetSize() == 0 &&
        (!origin->HasKey("indexedDB") || origin->GetList("indexedDB")->GetSize() == 0) &&
        (!origin->HasKey("opfs") || origin->GetList("opfs")->GetSize() == 0);
      const size_t bytes = empty ? 0 : JSON(origin).size();
      if (bytes > kMaxState - work->bytes || bytes > kBudget - self->retained_bytes_) { self->Finish(work, false, "Native captured storage bytes capacity exceeded"); return; }
      work->bytes += bytes; self->retained_bytes_ += bytes;
      const auto origins = work->output->GetList("origins"); if (!empty) origins->SetDictionary(origins->GetSize(), origin);
    }
    if (work->restore) self->hooks_.record_origin(work->context, work->origins[work->index]);
    ++work->index; self->Next(work);
  });
}
void SuiteCutStorageControls::Finish(const std::shared_ptr<Work>& work, bool success, const std::string& error) {
  if (!Current(work)) return;
  active_.erase(work->owner);
  if (work->lease) {
    const int lease = work->lease; work->lease = 0; closing_[work->owner] = work;
    utilities_->Release(lease, [weak = weak_from_this(), work, success, error] {
      const auto self = weak.lock(); if (!self) return;
      self->physical_.erase(work->owner);self->closing_.erase(work->owner); self->retained_bytes_ -= work->bytes; work->bytes = 0;
      auto reply = std::move(work->reply); if (reply) reply(success, success && !work->restore ? work->output : nullptr, error);
      if(self->hooks_.settled)self->hooks_.settled(work->context);
    });
  } else {
    retained_bytes_ -= work->bytes; work->bytes = 0;
    auto reply = std::move(work->reply); if (reply) reply(success, success && !work->restore ? work->output : nullptr, error);
  }
}
void SuiteCutStorageControls::Cancel(int owner) {
  auto found = active_.find(owner); std::shared_ptr<Work> work;
  if (found != active_.end()) { work = found->second; active_.erase(found); }
  else { const auto closing = closing_.find(owner); if (closing == closing_.end()) return; work = closing->second; closing_.erase(closing); }
  retained_bytes_ -= work->bytes; work->bytes = 0;
  work->reply = {}; work->output = nullptr; work->state.clear(); work->origins.clear(); work->codec.clear();
  if(work->lease){const int lease=work->lease;work->lease=0;utilities_->Release(lease,[weak=weak_from_this(),work]{if(const auto self=weak.lock()){self->physical_.erase(work->owner);if(self->hooks_.settled)self->hooks_.settled(work->context);}});}
}
void SuiteCutStorageControls::ContextClosed(int context) {
  std::vector<int> canceled; for (const auto& [id, work] : active_) if (work->context == context) canceled.push_back(id);
  for (const int id : canceled) { const auto work = active_.at(id); Finish(work, false, "Native storage context closed"); }
  for (auto it = uploads_.begin(); it != uploads_.end();) if (it->second.context == context) { upload_bytes_ -= it->second.json.size(); it = uploads_.erase(it); } else ++it;
}
void SuiteCutStorageControls::Clear() {
  std::vector<int> owners; for (const auto& [id, work] : active_) owners.push_back(id); for (const int id : owners) Cancel(id);
  std::vector<int> closing; for (const auto& [id, work] : closing_) closing.push_back(id); for (const int id : closing) Cancel(id);
  stopping_ = true; uploads_.clear(); upload_bytes_ = 0;
}
void SuiteCutStorageControls::WakeUploads() {
  if (upload_timer_ || uploads_.empty() || stopping_) return;
  upload_timer_ = true; CefPostDelayedTask(TID_UI, new StorageTask([weak = weak_from_this()] { if (const auto self = weak.lock()) self->TickUploads(); }), 1000);
}
void SuiteCutStorageControls::TickUploads() {
  upload_timer_ = false;
  for (auto it = uploads_.begin(); it != uploads_.end();) if (Now() - it->second.updated >= 60000) { upload_bytes_ -= it->second.json.size(); it = uploads_.erase(it); } else ++it;
  WakeUploads();
}
void SuiteCutStorageControls::Diagnostics(Dict output) const {
  output->SetInt("storagePhysicalLeases",static_cast<int>(physical_.size()));
  output->SetInt("storageClosingOperations", static_cast<int>(closing_.size()));
  output->SetInt("storageOperations", static_cast<int>(active_.size())); output->SetDouble("storageRetainedBytes", static_cast<double>(retained_bytes_));
  output->SetInt("storageUploads", static_cast<int>(uploads_.size())); output->SetDouble("storageUploadBytes", static_cast<double>(upload_bytes_));
}
