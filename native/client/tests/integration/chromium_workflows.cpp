// SPDX-License-Identifier: MIT
// Real HTTP workflows for the public native browser/agent facade. No Node/server dependency.
#include <suitecut/agent.hpp>
#include <suitecut/agent_driver.hpp>
#include <suitecut/browser.hpp>

#include <arpa/inet.h>
#include "posix_fixture.hpp"
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
using suitecut::Json;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

// One bounded fixture thread; all browser/library threads belong to the SDK.
// Sockets are close-on-exec and accept only local connections on an ephemeral port.
class FixtureServer {
 public:
  FixtureServer() {
    socket_ = suitecut_test::socket();
    if (socket_ < 0) throw std::runtime_error("Cannot create fixture socket");
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(socket_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
        ::listen(socket_, 16) != 0) {
      ::close(socket_); throw std::runtime_error("Cannot listen on fixture loopback socket");
    }
    socklen_t length = sizeof(address);
    if (::getsockname(socket_, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
      ::close(socket_); throw std::runtime_error("Cannot inspect fixture port");
    }
    port_ = ntohs(address.sin_port);
    try { worker_ = std::thread([this] { serve(); }); }
    catch (...) { ::close(socket_); throw; }
  }
  ~FixtureServer() {
    stopping_ = true;
    ::shutdown(socket_, SHUT_RDWR);
    if (worker_.joinable()) worker_.join();
    ::close(socket_);
  }
  std::string origin(bool alternate_host = false) const {
    return "http://" + std::string(alternate_host ? "localhost" : "127.0.0.1") + ":" + std::to_string(port_);
  }
  int api_requests() const { return api_requests_.load(); }
  std::string error() const { return failed_.load() ? "Fixture server failed while serving a request" : ""; }

 private:
  std::string main_page() const {
    return R"HTML(<!doctype html><html lang="en"><meta charset="utf-8"><title>Native HTTP workflow</title>
<style>body{font:16px sans-serif;margin:20px}label,button,a{display:block;margin:8px}iframe{width:420px;height:160px}</style>
<label>Customer<input id="customer"></label><button id="save">Save order</button><output id="status">Waiting</output>
<label>Upload evidence<input id="upload" type="file" multiple></label><output id="upload-result"></output>
<label>Evidence directory<input id="directory" type="file" webkitdirectory multiple></label><output id="directory-result"></output>
<div id="drag-source" draggable="true" style="display:inline-block;width:160px;height:50px;background:#ddd">Drag source</div>
<div id="drop-target" style="display:inline-block;margin-left:40px;width:160px;height:50px;background:#cdf">Drop target</div><output id="drag-result"></output>
<iframe id="child" src="/frame" title="Order notes"></iframe><output id="frame-result"></output>
<a id="next" href="/next">Next page</a><a id="download" href="/download">Download receipt</a>
<button id="fetch">Fetch server value</button><output id="fetch-result"></output>
<script>
window.clicks=0;window.trusted=false;
document.querySelector('#save').onclick=e=>{clicks++;trusted=e.isTrusted;document.querySelector('#status').textContent='Saved '+document.querySelector('#customer').value};
document.querySelector('#upload').onchange=async e=>{const f=e.target.files;document.querySelector('#upload-result').textContent=await Promise.all([...f].map(async x=>x.name+':'+await x.text())).then(v=>v.join('|'))};
document.querySelector('#directory').onchange=async e=>{document.querySelector('#directory-result').textContent=await Promise.all([...e.target.files].map(async f=>f.webkitRelativePath+':'+await f.text())).then(v=>v.sort().join('|'))};
document.querySelector('#drag-source').ondragstart=e=>{window.dragStartedTrusted=e.isTrusted;e.dataTransfer.setData('text/plain','native-drag-token')};
document.querySelector('#drop-target').ondragover=e=>e.preventDefault();
document.querySelector('#drop-target').ondrop=e=>{e.preventDefault();window.dragDroppedTrusted=e.isTrusted;document.querySelector('#drag-result').textContent=e.dataTransfer.getData('text/plain')};
addEventListener('message',e=>{if(e.data&&e.data.kind==='note')document.querySelector('#frame-result').textContent=e.data.value});
document.querySelector('#fetch').onclick=async()=>{document.querySelector('#fetch-result').textContent=await fetch('/api/value').then(r=>r.text())};
window.fetchProof=url=>fetch(url).then(r=>r.text());
</script></html>)HTML";
  }
  void respond(int client, const std::string& target, const std::string& request) {
    std::string lowered = request;
    for (auto& c : lowered) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    std::string body, type = "text/html; charset=utf-8", extra, status = "200 OK";
    if (target == "/" || target.starts_with("/?")) body = main_page();
    else if (target.starts_with("/frame")) body = R"HTML(<!doctype html><meta charset="utf-8"><title>Native child frame</title>
<label>Frame note<input id="note"></label><button id="send">Send note</button><output id="note-status"></output>
<script>document.querySelector('#send').onclick=e=>{const value=document.querySelector('#note').value;document.querySelector('#note-status').textContent=value;window.lastFrameTrusted=e.isTrusted;parent.postMessage({kind:'note',value},'*')}</script>)HTML";
    else if (target == "/next") body = "<!doctype html><title>Native next page</title><h1>Next document</h1><a href='/'>Return</a>";
    else if (target.starts_with("/api/")) {
      ++api_requests_; type = "text/plain; charset=utf-8";
      body = target == "/api/headers" ?
        (lowered.find("x-native-proof: header-proof-42") != std::string::npos ? "header accepted" : "header missing") : "server value λ";
    } else if (target == "/auth") {
      type = "text/plain; charset=utf-8";
      if (lowered.find("authorization: basic dxnlcjpwyxnz") != std::string::npos) body = "authorized proof";
      else { status = "401 Unauthorized"; extra = "WWW-Authenticate: Basic realm=\"native-proof\"\r\n"; body = "authentication required"; }
    }
    else if (target == "/download") {
      type = "text/plain; charset=utf-8"; extra = "Content-Disposition: attachment; filename=receipt.txt\r\n";
      body = "Native receipt λ\n";
    } else { body = "<!doctype html><title>Fixture missing</title>Missing fixture path"; }
    std::string response = "HTTP/1.1 " + status + "\r\nConnection: close\r\nCache-Control: no-store\r\nContent-Type: " +
      type + "\r\n" + extra + "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
    std::size_t offset = 0;
    while (offset < response.size()) {
      const auto sent = ::send(client, response.data() + offset, response.size() - offset, MSG_NOSIGNAL);
      if (sent <= 0) return;
      offset += static_cast<std::size_t>(sent);
    }
  }
  void serve() noexcept {
    while (!stopping_) {
      const int client = suitecut_test::accept(socket_);
      if (client < 0) { if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue; if (!stopping_) failed_ = true; return; }
      timeval timeout{1, 0};
      ::setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
      try {
        std::string request;
        char bytes[2048];
        while (request.size() < 16384 && request.find("\r\n\r\n") == std::string::npos) {
          const auto count = ::recv(client, bytes, sizeof(bytes), 0);
          if (count <= 0) break;
          request.append(bytes, static_cast<std::size_t>(count));
        }
        const auto first = request.find(' '), second = request.find(' ', first == std::string::npos ? 0 : first + 1);
        if (first != std::string::npos && second != std::string::npos) respond(client, request.substr(first + 1, second - first - 1), request);
      } catch (...) { failed_ = true; }
      ::close(client);
    }
  }
  int socket_ = -1, port_ = 0;
  std::thread worker_;
  std::atomic<bool> stopping_{false}, failed_{false};
  std::atomic<int> api_requests_{0};
};

void Check(bool condition, const std::string& name, Json& report) {
  if (!condition) throw std::runtime_error("Check failed: " + name);
  report["checks"].as_array().emplace_back(name);
  std::cout << Json{{"event", "check"}, {"name", name}}.dump() << std::endl;
}
std::string Read(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) throw std::runtime_error("Cannot read " + path.string());
  std::ostringstream data; data << stream.rdbuf(); return data.str();
}
void Write(const std::filesystem::path& path, const std::string& data) {
  std::ofstream stream(path, std::ios::binary); stream << data;
  if (!stream) throw std::runtime_error("Cannot write " + path.string());
}
std::string Ref(const Json& value, const std::string& role, const std::string& name) {
  if (value.is_array()) {
    for (const auto& child : value.as_array()) { auto ref = Ref(child, role, name); if (!ref.empty()) return ref; }
  } else if (value.is_object()) {
    if (value.contains("role") && value.contains("name") && value.contains("ref") &&
        value.at("role") == Json(role) && value.at("name") == Json(name)) return value.at("ref").as_string();
    if (value.contains("children")) return Ref(value.at("children"), role, name);
  }
  return {};
}
} // namespace

int main(int argc, char** argv) {
  std::string executable, output = "chromium-workflows.json";
  std::vector<std::string> arguments;
  for (int i = 1; i < argc; i += 2) {
    if (i + 1 == argc) { std::cerr << "Expected --browser/--output/--browser-arg value\n"; return 2; }
    const std::string option = argv[i], value = argv[i + 1];
    if (option == "--browser") executable = value;
    else if (option == "--output") output = value;
    else if (option == "--browser-arg") arguments.push_back(value);
    else { std::cerr << "Unknown option: " << option << '\n'; return 2; }
  }
  if (executable.empty()) { std::cerr << "--browser /absolute/path/to/chrome is required\n"; return 2; }
  Json report{{"client", "native-cpp"}, {"backend", "chromium"}, {"passed", false},
              {"checks", Json::array{}}, {"scope", "Finite real HTTP workflows; native C++ agent/browser, no Node."}};
  const auto started = Clock::now();
  std::optional<suitecut::Browser> browser;
  int result = 0;
  try {
    const auto directory = std::filesystem::absolute(output).parent_path();
    std::filesystem::create_directories(directory);
    FixtureServer server;
    report["fixtureOrigin"] = server.origin();
    suitecut::LaunchOptions options;
    options.executable = std::filesystem::absolute(executable).string(); options.arguments = arguments;
    options.startup_timeout = 15s; options.shutdown_timeout = 3s;
    browser.emplace(suitecut::Browser::launch_chromium(options));
    report["ready"] = browser->ready();
    auto page = browser->main_page(); page.set_timeout(5s);
    page.navigate(server.origin() + "/");
    Check(page.evaluate("document.title") == Json("Native HTTP workflow"), "real HTTP document navigation", report);

    const auto observation = page.observe();
    const auto button_ref = Ref(observation, "button", "Save order");
    Check(!button_ref.empty(), "HTTP snapshot gives the agent an actionable reference", report);
    const Json customer{{"kind", "label"}, {"value", "Customer"}};
    auto batch = suitecut::Agent::run(page, Json::array{
      Json{{"action", "fill"}, {"target", customer}, {"value", "Customer λ 😀"},
        {"expect", Json{{"kind", "inputValue"}, {"target", customer}, {"value", "Customer λ 😀"}}}},
      Json{{"action", "click"}, {"target", Json{{"kind", "ref"}, {"value", button_ref}}},
        {"expect", Json{{"kind", "text"}, {"target", Json{{"kind", "css"}, {"value", "#status"}}},
          {"value", "Saved Customer λ 😀"}}}}
    });
    report["agentBatch"] = batch.json();
    Check(batch.ok && batch.actions.size() == 2 &&
          batch.actions[0].effect == suitecut::AgentEffect::confirmed &&
          batch.actions[1].effect == suitecut::AgentEffect::confirmed &&
          page.evaluate("clicks===1&&trusted").as_bool(), "agent plan executes trusted UI actions and verifies both outcomes", report);

    {
      suitecut::AgentDriver driver(*browser);
      const auto tools = suitecut::AgentDriver::tools();
      report["agentToolCount"] = tools.as_array().size();
      Check(tools.is_array() && tools.as_array().size() >= 20, "native agent driver publishes tool schemas", report);
      const auto snapshot = driver.call("snapshot");
      const auto driver_ref = Ref(snapshot, "button", "Save order");
      Check(!driver_ref.empty(), "agent tool snapshot exposes a genuine actionable reference", report);
      driver.call("fill", Json{{"target", customer}, {"text", "Driver Ω 漢字"}});
      driver.call("click", Json{{"target", Json{{"kind", "ref"}, {"value", driver_ref}}}});
      Check(driver.call("get", Json{{"kind", "text"}, {"target", Json{{"kind", "css"}, {"value", "#status"}}}}) ==
            Json("Saved Driver Ω 漢字") && page.evaluate("clicks===2&&trusted").as_bool(),
            "native agent tools drive the real trusted form and read its outcome", report);
      const auto initial_diff = driver.call("diff");
      const auto repeated_diff = driver.call("diff");
      Check(!initial_diff.at("hasBaseline").as_bool() && !initial_diff.at("changed").as_bool() &&
            repeated_diff.at("hasBaseline").as_bool() && !repeated_diff.at("changed").as_bool(),
            "agent semantic diff ignores changing observation references", report);
      page.evaluate("document.querySelector('#status').textContent='Changed after baseline'");
      Check(driver.call("diff").at("changed").as_bool(), "agent semantic diff detects an actual visible change", report);

      const auto submitted = driver.call("submit", Json{{"family", "evaluate"},
        {"fields", Json{{"expression", "confirm('agent driver modal')"}}}, {"timeoutMs", 3000}});
      const auto modal_id = submitted.at("id");
      bool modal_opened = false;
      const auto modal_deadline = Clock::now() + 1s;
      while (!modal_opened && Clock::now() < modal_deadline) {
        const auto events = driver.call("events", Json{{"maxEvents", 256}, {"waitMs", 20}});
        for (const auto& event : events.at("events").as_array()) {
          const auto& data = event.at("data");
          if (data.is_object() && data.contains("method") && data.at("method") == Json("Page.javascriptDialogOpening") &&
              data.at("params").at("message") == Json("agent driver modal")) modal_opened = true;
        }
      }
      Check(modal_opened, "agent event tool receives the actual pending browser dialog", report);
      Check(driver.call("result", Json{{"id", modal_id}}).at("status") == Json("pending"),
            "agent result polling remains nonblocking while a modal is open", report);
      driver.call("devtools", Json{{"method", "Page.handleJavaScriptDialog"}, {"params", Json{{"accept", true}}}});
      const auto completed = driver.call("result", Json{{"id", modal_id}, {"waitMs", 1000}});
      Check(completed.at("status") == Json("completed") && completed.at("result").at("result").at("value") == Json(true),
            "agent continuation resolves its submitted modal without Node or callbacks", report);

      report["webmcpRuntime"] = page.evaluate(R"JS((()=>{
        const inspect=value=>{if(value==null)return {type:typeof value};const chain=[];
          for(let p=Object.getPrototypeOf(value),depth=0;p&&depth<4;p=Object.getPrototypeOf(p),depth++){
            chain.push({constructor:p.constructor?.name||null,properties:Object.getOwnPropertyNames(p).map(name=>{
              const d=Object.getOwnPropertyDescriptor(p,name);return {name,type:typeof d.value,getter:typeof d.get==='function'};})});}
          return {type:typeof value,ownProperties:Object.getOwnPropertyNames(value),prototypeChain:chain};};
        return {secureContext:isSecureContext,origin:location.origin,url:location.href,
          navigatorModelContext:inspect(navigator.modelContext),navigatorModelContextTesting:inspect(navigator.modelContextTesting),
          windowModelContext:inspect(window.modelContext),windowModelContextTesting:inspect(window.modelContextTesting),
          documentModelContext:inspect(document.modelContext),documentModelContextTesting:inspect(document.modelContextTesting),
          modelContextConstructor:typeof window.ModelContext,
          modelContextStaticProperties:typeof window.ModelContext==='function'?Object.getOwnPropertyNames(window.ModelContext).map(name=>{
            const d=Object.getOwnPropertyDescriptor(window.ModelContext,name);return {name,type:typeof d.value,getter:typeof d.get==='function'};}):[],
          modelContextPrototype:typeof window.ModelContext==='function'?inspect(window.ModelContext.prototype):null,
          modelContextPrototypeProperties:typeof window.ModelContext==='function'?Object.getOwnPropertyNames(window.ModelContext.prototype).map(name=>{
            const d=Object.getOwnPropertyDescriptor(window.ModelContext.prototype,name);return {name,type:typeof d.value,getter:typeof d.get==='function'};}):[],
          documentModelProperties:Object.getOwnPropertyNames(Object.getPrototypeOf(document)).filter(name=>/model|mcp/i.test(name)),
          navigatorModelProperties:Object.getOwnPropertyNames(Object.getPrototypeOf(navigator)).filter(name=>/model|mcp/i.test(name))};
      })())JS");
      const bool webmcp_available = page.evaluate("(()=>{const provider=navigator.modelContext||document.modelContext||window.modelContext;const tools=navigator.modelContextTesting||document.modelContextTesting||window.modelContextTesting||provider;return !!provider&&typeof provider.registerTool==='function'&&!!tools&&(typeof tools.listTools==='function'||typeof tools.getTools==='function')&&typeof tools.executeTool==='function'})()").as_bool();
      report["webmcp"] = Json{{"available", webmcp_available}, {"status", webmcp_available ? "running" : "unavailable"}};
      if (webmcp_available) {
        page.evaluate("(()=>{const provider=navigator.modelContext||document.modelContext||window.modelContext;provider.registerTool({name:'native_workflow_echo',description:'Echo a fixture message',inputSchema:{type:'object',properties:{message:{type:'string'}},required:['message']},execute:async p=>({content:[{type:'text',text:'Native echo '+p.message}]})});return true})()");
        const auto published = driver.call("webmcp", Json{{"action", "list"}});
        bool listed = false;
        for (const auto& tool : published.at("tools").as_array())
          if (tool.at("name") == Json("native_workflow_echo")) listed = true;
        Check(published.at("untrusted").as_bool() && listed, "agent discovers an actual page-published WebMCP tool as untrusted data", report);
        const auto invoked = driver.call("webmcp", Json{{"action", "invoke"}, {"name", "native_workflow_echo"},
          {"parameters", Json{{"message", "λ 😀"}}}});
        Check(invoked.at("untrusted").as_bool() && invoked.at("result").dump().find("Native echo λ 😀") != std::string::npos,
              "agent invokes the browser WebMCP API and labels the real result untrusted", report);
        report["webmcp"]["status"] = "passed";
      } else {
        report["webmcp"]["reason"] = "Browser does not expose the required real modelContext registration/testing APIs; no WebMCP assertions executed.";
      }

      page.evaluate("localStorage.setItem('native-session','local λ');sessionStorage.setItem('native-tab','session 😀');document.cookie='native-state=cookie-proof; Path=/'");
      const auto session = driver.call("state", Json{{"action", "get"}});
      Check(!session.at("cookies").as_array().empty() && session.at("origins").as_array().size() == 1,
            "agent state tool exports the current HTTP origin and cookies", report);
      page.evaluate("localStorage.clear();sessionStorage.clear();document.cookie='native-state=; Max-Age=0; Path=/'");
      const auto scripts_before_state = page.init_scripts().as_array().size();
      driver.call("state", Json{{"action", "set"}, {"state", session}});
      Check(driver.call("evaluate", Json{{"expression", "localStorage.getItem('native-session')==='local λ'&&sessionStorage.getItem('native-tab')==='session 😀'&&document.cookie.includes('native-state=cookie-proof')"}}).as_bool(),
            "agent state import restores actual cookies and local/session storage", report);
      driver.call("history", Json{{"action", "reload"}});
      Check(driver.call("evaluate", Json{{"expression", "localStorage.getItem('native-session')==='local λ'&&sessionStorage.getItem('native-tab')==='session 😀'"}}).as_bool(),
            "imported agent state survives a document reload", report);
      page.evaluate("localStorage.setItem('native-session','local changed Ω');sessionStorage.setItem('native-tab','session changed Ω')");
      driver.call("history", Json{{"action", "reload"}});
      Check(page.evaluate("localStorage.getItem('native-session')==='local changed Ω'&&sessionStorage.getItem('native-tab')==='session changed Ω'").as_bool(),
            "state restoration does not overwrite subsequent storage changes on reload", report);
      Check(page.init_scripts().as_array().size() == scripts_before_state, "state restoration consumes no init-script quota", report);
      auto mismatched_state = session;
      mismatched_state.at("origins").at(0)["origin"] = "https://unmatched.invalid";
      bool mismatched_rejected = false;
      try { driver.call("state", Json{{"action", "set"}, {"state", mismatched_state}}); }
      catch (const std::exception&) { mismatched_rejected = true; }
      Check(mismatched_rejected && page.evaluate("localStorage.getItem('native-session')==='local changed Ω'&&sessionStorage.getItem('native-tab')==='session changed Ω'&&document.cookie.includes('native-state=cookie-proof')").as_bool(),
            "mismatched-origin state is rejected before modifying cookies or storage", report);
      driver.call("fill", Json{{"target", customer}, {"text", "Driver Ω 漢字"}});

      auto isolated_context = browser->new_context();
      auto isolated_page = isolated_context.new_page();
      isolated_page.navigate(server.origin() + "/");
      Check(isolated_page.evaluate("localStorage.getItem('native-session')===null&&!document.cookie.includes('native-state=')").as_bool(),
            "new browser context starts with independent HTTP origin storage", report);
      isolated_page.restore_storage_state(session);
      Check(isolated_page.evaluate("localStorage.getItem('native-session')==='local λ'&&sessionStorage.getItem('native-tab')==='session 😀'&&document.cookie.includes('native-state=cookie-proof')").as_bool(),
            "native state import transfers actual origin state to an isolated context", report);
      isolated_page.close(); isolated_context.close();

      const auto child = driver.call("pages", Json{{"action", "new"}, {"url", server.origin() + "/"}});
      const auto child_id = child.at("pageId");
      Check(!child.at("isolatedContext").as_bool() &&
            driver.call("evaluate", Json{{"expression", "localStorage.getItem('native-session')==='local changed Ω'&&document.cookie.includes('native-state=cookie-proof')"}}).as_bool(),
            "new agent tab shares its browser context origin storage", report);
      driver.call("fill", Json{{"target", customer}, {"text", "Isolated agent page"}});
      driver.call("pages", Json{{"action", "switch"}, {"pageId", 1}});
      Check(driver.call("get", Json{{"kind", "value"}, {"target", customer}}) == Json("Driver Ω 漢字"),
            "agent page creation and switching preserve independent document form state", report);
      driver.call("pages", Json{{"action", "close"}, {"pageId", child_id}});
      Check(driver.call("pages").at("pages").as_array().size() == 1, "agent page close retires the child page", report);
    }

    page.evaluate("setTimeout(()=>window.asyncReady=42,30)");
    Check(page.wait_for_function("window.asyncReady===42").as_bool(), "native predicate wait observes delayed page state", report);
    page.wait_for_url(server.origin() + "/");
    page.wait_for_load_state("load");
    Check(page.evaluate("document.readyState") == Json("complete"), "native URL and load-state waits", report);
    page.navigate(server.origin() + "/next");
    page.go_back();
    Check(page.evaluate("document.title") == Json("Native HTTP workflow"), "native history back restores previous document", report);
    page.go_forward();
    Check(page.evaluate("document.title") == Json("Native next page"), "native history forward restores next document", report);
    page.reload();
    Check(page.evaluate("document.title") == Json("Native next page"), "native reload waits for actual document", report);
    page.navigate(server.origin() + "/");

    auto frame = page.frame_locator("#child");
    frame.get_by_label("Frame note").fill("Frame λ 😀");
    frame.get_by_role("button", "Send note").click();
    Check(page.wait_for_function("document.querySelector('#frame-result').textContent==='Frame λ 😀'").as_bool() &&
          frame.evaluate("window.lastFrameTrusted===true").as_bool(), "native frame locators deliver trusted input and parent outcome", report);
    page.evaluate("(()=>{const f=document.createElement('iframe');f.id='remote';f.src=" + Json(server.origin(true) + "/frame").dump() + ";document.body.append(f)})()");
    auto remote = page.frame_locator("#remote");
    remote.get_by_label("Frame note").fill("Cross frame Ω");
    remote.get_by_role("button", "Send note").click();
    Check(page.wait_for_function("document.querySelector('#frame-result').textContent==='Cross frame Ω'").as_bool() &&
          remote.evaluate("window.lastFrameTrusted===true").as_bool(), "native cross-origin frame targeting preserves trusted input and parent outcome", report);
    const auto upload_path = directory / "workflow-upload.txt";
    Write(upload_path, "Actual uploaded bytes λ 😀");
    {
      suitecut::AgentDriver driver(*browser);
      driver.call("upload", Json{{"target", Json{{"kind", "label"}, {"value", "Upload evidence"}}},
        {"files", Json::array{upload_path.string()}}});
      driver.call("wait_for", Json{{"expression", "document.querySelector('#upload-result').textContent==='workflow-upload.txt:Actual uploaded bytes λ 😀'"},
        {"timeoutMs", 2000}});
      driver.call("fill", Json{{"target", Json{{"kind", "label"}, {"value", "Frame note"}, {"framePath", Json::array{"#child"}}}},
        {"text", "Agent frame 漢字"}});
      driver.call("click", Json{{"target", Json{{"kind", "role"}, {"value", "button"}, {"name", "Send note"},
        {"framePath", Json::array{"#child"}}}}});
      Check(page.wait_for_function("document.querySelector('#frame-result').textContent==='Agent frame 漢字'").as_bool(),
            "agent semantic targets support native frame paths", report);
    }
    Check(page.wait_for_function("document.querySelector('#upload-result').textContent==='workflow-upload.txt:Actual uploaded bytes λ 😀'").as_bool(),
          "native file upload exposes the actual file bytes to the page", report);
    bool missing_rejected = false;
    try { page.get_by_label("Upload evidence").set_input_files({(upload_path / "missing.txt").string()}); }
    catch (const std::exception&) { missing_rejected = true; }
    Check(missing_rejected && page.evaluate("document.querySelector('#upload').files[0].name==='workflow-upload.txt'").as_bool(),
          "missing upload file is rejected before replacing the current selection", report);
    const auto second_upload = directory / "workflow-second-λ.txt";
    Write(second_upload, "Second bytes 漢字");
    page.get_by_label("Upload evidence").set_input_files({upload_path.string(), second_upload.string()});
    Check(page.wait_for_function("document.querySelector('#upload-result').textContent==='workflow-upload.txt:Actual uploaded bytes λ 😀|workflow-second-λ.txt:Second bytes 漢字'").as_bool(),
          "multiple uploads preserve Unicode filenames and actual contents", report);
    page.get_by_label("Upload evidence").set_input_files({});
    Check(page.evaluate("document.querySelector('#upload').files.length===0").as_bool(), "native file upload can clear selection", report);
    const auto upload_directory = directory / "workflow-evidence";
    std::filesystem::create_directories(upload_directory / "sub-λ");
    Write(upload_directory / "sub-λ" / "proof.txt", "Directory bytes 😀");
    page.get_by_label("Evidence directory").set_input_files({upload_directory.string()});
    Check(page.wait_for_function("document.querySelector('#directory-result').textContent==='workflow-evidence/sub-λ/proof.txt:Directory bytes 😀'").as_bool(),
          "native directory upload preserves nested Unicode paths and real file contents", report);
    page.get_by_label("Evidence directory").set_input_files({});
    Check(page.evaluate("document.querySelector('#directory').files.length===0").as_bool(), "native directory upload can clear selection", report);
    {
      suitecut::AgentDriver driver(*browser);
      page.evaluate(R"JS((()=>{window.dragEvents=[];window.recordDragProof=true;
        for(const type of ['pointerdown','pointermove','pointerup','mousedown','mousemove','mouseup','dragstart','dragenter','dragover','drop','dragend'])
          document.addEventListener(type,e=>{if(!window.recordDragProof)return;let transfer=null;
            try{if(e.dataTransfer)transfer={types:Array.from(e.dataTransfer.types),text:e.dataTransfer.getData('text/plain')};}catch(error){transfer={error:String(error)}}
            window.dragEvents.push({type:e.type,target:e.target.id,isTrusted:e.isTrusted,buttons:e.buttons,button:e.button,
              x:e.clientX,y:e.clientY,pointerType:e.pointerType||null,dataTransfer:transfer});
            if(window.dragEvents.length>128)window.dragEvents.shift();},true);return true;})())JS");
      const auto drag_native = driver.call("drag", Json{{"source", Json{{"kind", "css"}, {"value", "#drag-source"}}},
        {"target", Json{{"kind", "css"}, {"value", "#drop-target"}}}, {"options", Json{{"steps", 8}}}});
      const auto drag = page.evaluate("(()=>{window.recordDragProof=false;return {text:document.querySelector('#drag-result').textContent,startedTrusted:window.dragStartedTrusted??null,droppedTrusted:window.dragDroppedTrusted??null,startedType:typeof window.dragStartedTrusted,droppedType:typeof window.dragDroppedTrusted,events:window.dragEvents}})()");
      report["drag"] = drag;
      report["drag"]["nativeOutcome"] = drag_native;
      const bool drag_ok = drag.at("text") == Json("native-drag-token") && drag.at("startedTrusted") == Json(true) && drag.at("droppedTrusted") == Json(true);
      if (!drag_ok) {
        const auto image = page.screenshot();
        Write(directory / "drag-failure.png", std::string(image.begin(), image.end()));
        report["dragFailureScreenshot"] = (directory / "drag-failure.png").string();
      }
      Check(drag_ok,
            "agent native drag delivers trusted HTML5 drag data to the actual drop target", report);
    }

    const auto pdf = page.pdf();
    Check(pdf.size() > 100 && std::string(pdf.begin(), pdf.begin() + 5) == "%PDF-", "native PDF produces real PDF bytes", report);
    Write(directory / "workflow.pdf", std::string(pdf.begin(), pdf.end()));

    page.start_har();
    page.set_extra_http_headers(Json{{"X-Native-Proof", "header-proof-42"}});
    Check(page.evaluate("fetchProof('/api/headers')") == Json("header accepted"), "native extra HTTP headers reach the actual server", report);
    const auto before_mock = server.api_requests();
    page.route(server.origin() + "/api/mock", Json{{"behavior", "mock"}, {"status", 200},
      {"headers", Json{{"Content-Type", "text/plain; charset=utf-8"}}}, {"body", "native mocked response λ"}});
    Check(page.evaluate("fetchProof('/api/mock')") == Json("native mocked response λ") &&
          server.api_requests() == before_mock, "native route fulfills real fetch without reaching the server", report);
    page.route(server.origin() + "/api/abort", Json{{"behavior", "abort"}});
    Check(page.evaluate("fetchProof('/api/abort').then(()=>false,()=>true)").as_bool() &&
          server.api_requests() == before_mock, "native route aborts a real fetch without a server request", report);
    page.unroute();
    Check(page.routes().as_array().empty() && page.evaluate("fetchProof('/api/value')") == Json("server value λ"),
          "native unroute restores the actual HTTP response", report);
    const auto requests = page.requests(), responses = page.responses();
    std::string request_id;
    for (const auto& response : responses.as_array())
      if (response.contains("response") && response.at("response").at("url") == Json(server.origin() + "/api/value"))
        request_id = response.at("requestId").as_string();
    Check(!requests.as_array().empty() && !request_id.empty(), "native network journals retain actual requests and responses", report);
    const auto response_body = page.response_body(request_id);
    Check(!response_body.at("base64Encoded").as_bool() && response_body.at("body") == Json("server value λ"),
          "native response body returns actual server bytes", report);
    const auto har = page.stop_har();
    Write(directory / "workflow.har", har.dump());
    bool har_response = false;
    for (const auto& entry : har.at("log").at("entries").as_array())
      if (entry.at("request").at("url") == Json(server.origin() + "/api/headers") && entry.at("response").at("status") == Json(200)) har_response = true;
    Check(har.at("log").at("version") == Json("1.2") && har_response, "native HAR records the actual HTTP exchange", report);
    const auto before_offline = server.api_requests();
    page.set_offline(true);
    Check(page.evaluate("fetchProof('/api/offline').then(()=>false,()=>true)").as_bool() && server.api_requests() == before_offline,
          "native offline mode blocks actual network access", report);
    page.set_offline(false);
    Check(page.evaluate("fetchProof('/api/value')") == Json("server value λ"), "native online mode restores HTTP access", report);
    page.set_http_credentials(Json{{"username", "user"}, {"password", "pass"}});
    Check(page.evaluate("fetchProof('/auth')") == Json("authorized proof"), "native credentials answer an actual HTTP Basic challenge", report);
    page.set_http_credentials(Json{});

    page.clear_console();
    page.evaluate("console.log('native console λ');setTimeout(()=>{window.errorFired=true;throw new Error('intentional native page error')},0)");
    page.wait_for_function("window.errorFired===true");
    Check(page.console_messages().dump().find("native console λ") != std::string::npos,
          "native console journal observes actual page output", report);
    Check(page.page_errors().dump().find("intentional native page error") != std::string::npos,
          "native page-error journal observes an actual uncaught exception", report);
    page.clear_console();
    Check(page.console_messages().as_array().empty() && page.page_errors().as_array().empty(), "native console clear retires both journals", report);
    const auto script = page.add_init_script("window.nativeInitProof='init λ';window.nativeInitWasEarly=document.readyState==='loading'");
    page.reload();
    Check(page.evaluate("window.nativeInitProof==='init λ'&&window.nativeInitWasEarly").as_bool() && !page.init_scripts().as_array().empty(),
          "native init script runs before document content on reload", report);
    page.remove_init_script(script); page.reload();
    Check(page.evaluate("typeof window.nativeInitProof==='undefined'").as_bool(), "native init script removal applies to the next document", report);
    page.emulate_media(Json{{"media", "screen"}, {"colorScheme", "dark"}});
    Check(page.evaluate("matchMedia('(prefers-color-scheme: dark)').matches").as_bool(), "native media emulation changes actual matchMedia", report);
    page.emulate_media(Json{{"media", Json{}}, {"colorScheme", Json{}}});

    page.set_download_directory((directory / "downloads").string());
    page.get_by_role("link", "Download receipt").click();
    Json downloads = Json::array{};
    const auto download_deadline = Clock::now() + 2s;
    while (downloads.as_array().empty() && Clock::now() < download_deadline) {
      downloads = page.downloads();
      if (downloads.as_array().empty()) std::this_thread::sleep_for(10ms);
    }
    Check(downloads.as_array().size() == 1 && downloads.at(0).at("suggestedFilename") == Json("receipt.txt"),
          "native download journal observes an actual HTTP attachment", report);
    const auto download_id = downloads.at(0).at("downloadId").as_string();
    const auto download_path = page.download_path(download_id);
    Check(page.download_status(download_id).at("state") == Json("completed") && Read(download_path) == "Native receipt λ\n",
          "native completed download exposes the actual downloaded bytes", report);
    const auto saved_receipt = directory / "saved-receipt.txt";
    page.save_download(download_id, saved_receipt.string());
    Check(Read(saved_receipt) == "Native receipt λ\n", "native download save copies the completed artifact", report);
    Check(server.error().empty(), "bounded native fixture server completed without errors", report);
    const auto stats = browser->transport()->stats();
    report["transportStats"] = Json{{"commands", stats.commands}, {"responses", stats.responses},
      {"pending", stats.pending}, {"droppedEvents", stats.dropped_events}, {"queuedEventBytes", stats.queued_event_bytes}};
    {
      const auto retained_transport = browser->transport();
      suitecut::AgentDriver closing_driver(*browser);
      const auto pending = closing_driver.call("submit", Json{{"family", "evaluate"},
        {"fields", Json{{"expression", "new Promise(resolve=>setTimeout(()=>resolve(42),700))"}}}, {"timeoutMs", 2000}});
      std::this_thread::sleep_for(30ms);
      browser->close();
      const auto closed_stats = retained_transport->stats();
      report["transportStatsAfterClose"] = Json{{"pending", closed_stats.pending},
        {"queuedEventBytes", closed_stats.queued_event_bytes}, {"droppedEvents", closed_stats.dropped_events}};
      Check(closed_stats.queued_event_bytes == 0, "browser close releases queued event bytes while transport and driver views remain alive", report);
      std::string close_error;
      try { (void)closing_driver.call("result", Json{{"id", pending.at("id")}, {"waitMs", 200}}); }
      catch (const std::exception& error) { close_error = error.what(); }
      Check(close_error.find("closed") != std::string::npos || close_error.find("cancel") != std::string::npos,
            "agent driver safely retires a pending result after its browser closes", report);
    }
    report["passed"] = true;
  } catch (const std::exception& error) { report["error"] = error.what(); result = 1; }
  if (browser) {
    try { browser->close(); }
    catch (const std::exception& error) { report["closeError"] = error.what(); report["passed"] = false; result = 1; }
  }
  report["elapsedMs"] = std::chrono::duration<double, std::milli>(Clock::now() - started).count();
  try { Write(output, report.dump() + "\n"); }
  catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
  std::cout << report.dump() << std::endl;
  return result;
}
