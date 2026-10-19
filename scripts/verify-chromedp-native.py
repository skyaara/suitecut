#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Run unchanged public chromedp evaluation tests through an authored native facade."""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile
from datetime import datetime, timezone
import urllib.request

ORACLE = os.environ.get("SUITECUT_NATIVE_GO_ORACLE", "chromedp")
assert ORACLE in ["chromedp", "rod"]
REVISION = "01a50b925312b71fc59f6eb0d8f489a5195ec448" if ORACLE == "rod" else "7d3ca7908248b2305a27d5d3c262df1ef52fb548"
REPOSITORY = "go-rod/rod" if ORACLE == "rod" else "chromedp/chromedp"
TEST_FILE = "page_eval_test.go" if ORACLE == "rod" else "eval_test.go"
ROOT = Path.cwd()
GO_ARCHIVE_SHA256 = "0335f314b6e7bfe08c3d0cfaa7c19db961b7b99fb20be62b0a826c992ad14e0f"
GO_DIRECTORY = ROOT / ".suitecut/chromedp-native/runner-go1.25.3"
RUN = ROOT / (".suitecut/" + ORACLE + "-native/runs") / (datetime.now(timezone.utc).isoformat().replace(":", "-") + "-" + str(os.getpid()))
RUN.mkdir(parents=True)

def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()

def inventory(directory):
    return [{"path": str(path), "sha256": digest(path)} for path in sorted(directory.rglob("*")) if path.is_file()]

GO_DIRECTORY.mkdir(parents=True, exist_ok=True)
go_archive = GO_DIRECTORY / "go1.25.3.linux-amd64.tar.gz"
if not go_archive.exists():
    with urllib.request.urlopen("https://go.dev/dl/go1.25.3.linux-amd64.tar.gz", timeout=60) as response, go_archive.open("wb") as target:
        shutil.copyfileobj(response, target)
assert digest(go_archive) == GO_ARCHIVE_SHA256, "Official generic Go archive SHA-256 must match the pinned release"
go = GO_DIRECTORY / "go/bin/go"
if not go.exists():
    with tarfile.open(go_archive) as archive:
        archive.extractall(GO_DIRECTORY, filter="data")

executable = Path(os.environ["SUITECUT_NATIVE_EXECUTABLE"])
sdk = Path(os.environ["SUITECUT_NATIVE_SDK_DIRECTORY"])
assert executable.is_absolute() and sdk.is_absolute()
(RUN / "runtime").mkdir()
shutil.copy2(executable, RUN / "runtime/suitecut-browser")
(RUN / "runtime/suitecut-browser").chmod(0o500)
shutil.copytree(sdk, RUN / "sdk")
resources = []
for name in ["Release", "Resources"]:
    directory = ROOT / ".suitecut/cef-sdk" / name
    resources.extend(inventory(directory))
    for path in directory.iterdir():
        (RUN / "runtime" / path.name).symlink_to(path)
pin = json.loads((ROOT / "native/browser/cef-build.json").read_text())
assert ('#define CEF_VERSION "' + pin["cefVersion"] + '"') in (ROOT / ".suitecut/cef-sdk/include/cef_version.h").read_text()
(RUN / "original").mkdir()
originals = []
for name in [TEST_FILE, "LICENSE"]:
    target = RUN / "original" / name
    with urllib.request.urlopen(f"https://raw.githubusercontent.com/{REPOSITORY}/{REVISION}/{name}", timeout=30) as response:
        target.write_bytes(response.read())
    originals.append({"path": str(target), "sha256": digest(target), "upstream": name})
fixture = RUN / "fixture"
fixture.mkdir()
shutil.copy2(RUN / "original" / TEST_FILE, fixture / "eval_test.go")
(fixture / "go.mod").write_text("module suitecut-native-chromedp-oracle\n\ngo 1.23\n\nrequire github.com/chromedp/cdproto/runtime v0.0.0\nreplace github.com/chromedp/cdproto/runtime => ./runtime-schema\n")
(fixture / "runtime-schema").mkdir()
(fixture / "runtime-schema/go.mod").write_text("module github.com/chromedp/cdproto/runtime\n\ngo 1.23\n")
(fixture / "runtime-schema/schema.go").write_text('''// SPDX-License-Identifier: MIT
// Authored test-only value projection, not a CDP client or driver implementation.
package runtime
type RemoteObject struct { Type string; ObjectID string }
''')
(fixture / "native_fixture_test.go").write_text('''// SPDX-License-Identifier: MIT
package chromedp
import (
 "bytes"
 "context"
 "encoding/json"
 "errors"
 "fmt"
 "io"
 "net/http"
 "os"
 "testing"
 "time"
 "github.com/chromedp/cdproto/runtime"
)
type nativeKey struct{}
type nativeSession struct { id string; client *http.Client }
type action func(context.Context) error
func request(client *http.Client, method, path string, payload interface{}) (json.RawMessage,error) {
 body,err:=json.Marshal(payload);if err!=nil{return nil,err}
 req,err:=http.NewRequest(method,os.Getenv("SUITECUT_NATIVE_HTTP")+path,bytes.NewReader(body));if err!=nil{return nil,err}
 req.Header.Set("Content-Type","application/json")
 response,err:=client.Do(req);if err!=nil{return nil,err};defer response.Body.Close()
 data,err:=io.ReadAll(io.LimitReader(response.Body,1048576));if err!=nil{return nil,err}
 var envelope struct{ Value json.RawMessage };if err=json.Unmarshal(data,&envelope);err!=nil{return nil,err}
 if response.StatusCode!=200 {return nil,fmt.Errorf("native HTTP %d: %s",response.StatusCode,envelope.Value)}
 return envelope.Value,nil
}
func testAllocate(t *testing.T, url string) (context.Context,context.CancelFunc) {
 t.Helper();client:=&http.Client{Timeout:15*time.Second}
 data,err:=request(client,"POST","/session",map[string]interface{}{"capabilities":map[string]interface{}{}});if err!=nil{t.Fatal(err)}
 var created struct{ SessionID string };if err=json.Unmarshal(data,&created);err!=nil{t.Fatal(err)}
 ctx,cancel:=context.WithCancel(context.WithValue(context.Background(),nativeKey{},nativeSession{created.SessionID,client}))
 return ctx,func(){_,err:=request(client,"DELETE","/session/"+created.SessionID,nil);cancel();if err!=nil{t.Error(err)}}
}
func Run(ctx context.Context, actions ...action) error {for _,a:=range actions{if err:=a(ctx);err!=nil{return err}};return nil}
func Evaluate(expression string, result interface{}) action {
 return func(ctx context.Context) error {
  session:=ctx.Value(nativeKey{}).(nativeSession)
  data,err:=request(session.client,"POST","/session/"+session.id+"/fixture/evaluate",map[string]string{"expression":expression});if err!=nil{return err}
  var observed struct{Type string;NativeHandle string;Value json.RawMessage};if err=json.Unmarshal(data,&observed);err!=nil{return err}
  if result==nil{return nil}
  if remote,ok:=result.(**runtime.RemoteObject);ok{*remote=&runtime.RemoteObject{Type:observed.Type,ObjectID:observed.NativeHandle};return nil}
  if raw,ok:=result.(*[]byte);ok{if observed.Type=="undefined"{*raw=nil}else{*raw=append([]byte(nil),observed.Value...)};return nil}
  if observed.Type=="undefined"{return errors.New("encountered an undefined value")}
  return json.Unmarshal(observed.Value,result)
 }
}
''')
selected = None
if ORACLE == "rod":
    source = (RUN / "original" / TEST_FILE).read_text()
    match = re.search(r"(?ms)^func TestConcurrentEval\(t \*testing\.T\) \{.*?(?=^func |\Z)", source)
    assert match, "The pinned original concurrent evaluation test must exist"
    selected = match.group(0).rstrip() + "\n"
    (RUN / "original/selected-TestConcurrentEval.go.txt").write_text(selected)
    (fixture / "eval_test.go").write_text('package rod_test\nimport ("testing";"time";"github.com/go-rod/rod/lib/utils")\n' + selected)
    (fixture / "go.mod").write_text("module suitecut-native-rod-oracle\n\ngo 1.23\n\nrequire github.com/go-rod/rod/lib/utils v0.0.0\nreplace github.com/go-rod/rod/lib/utils => ./utils-fixture\n")
    shutil.rmtree(fixture / "runtime-schema")
    (fixture / "utils-fixture").mkdir()
    (fixture / "utils-fixture/go.mod").write_text("module github.com/go-rod/rod/lib/utils\n\ngo 1.23\n")
    (fixture / "utils-fixture/utils.go").write_text('''// SPDX-License-Identifier: MIT
// Authored generic parallel fixture; no Rod/browser-driver implementation.
package utils
import "sync"
func All(functions ...func()) func() { return func() {var group sync.WaitGroup;for _,fn:=range functions{group.Add(1);go func(call func()){defer group.Done();call()}(fn)};group.Wait()} }
''')
    (fixture / "native_fixture_test.go").write_text('''// SPDX-License-Identifier: MIT
package rod_test
import (
 "bytes"
 "encoding/json"
 "fmt"
 "io"
 "net/http"
 "os"
 "reflect"
 "testing"
 "time"
)
type nativePage struct { id string; client *http.Client }
type assertionFixture struct { t *testing.T; page *nativePage }
type nativeNumber struct { value int }
func (number nativeNumber) Int() int { return number.value }
func request(client *http.Client, method, path string, payload interface{}) (json.RawMessage,error) {
 body,err:=json.Marshal(payload);if err!=nil{return nil,err}
 req,err:=http.NewRequest(method,os.Getenv("SUITECUT_NATIVE_HTTP")+path,bytes.NewReader(body));if err!=nil{return nil,err};req.Header.Set("Content-Type","application/json")
 response,err:=client.Do(req);if err!=nil{return nil,err};defer response.Body.Close()
 data,err:=io.ReadAll(io.LimitReader(response.Body,1048576));if err!=nil{return nil,err}
 var envelope struct{Value json.RawMessage};if err=json.Unmarshal(data,&envelope);err!=nil{return nil,err}
 if response.StatusCode!=200{return nil,fmt.Errorf("native HTTP %d: %s",response.StatusCode,envelope.Value)};return envelope.Value,nil
}
func setup(t *testing.T) *assertionFixture {
 t.Helper();client:=&http.Client{Timeout:15*time.Second};data,err:=request(client,"POST","/session",map[string]interface{}{"capabilities":map[string]interface{}{}});if err!=nil{t.Fatal(err)}
 var created struct{SessionID string};if err=json.Unmarshal(data,&created);err!=nil{t.Fatal(err)}
 page:=&nativePage{created.SessionID,client};t.Cleanup(func(){_,err:=request(client,"DELETE","/session/"+created.SessionID,nil);if err!=nil{t.Error(err)}})
 return &assertionFixture{t,page}
}
func (g *assertionFixture) blank() string {return "about:blank"}
func (g *assertionFixture) Eq(a,b interface{}) {g.t.Helper();if !reflect.DeepEqual(a,b){g.t.Errorf("expected equality: %v versus %v",a,b)}}
func (g *assertionFixture) Gt(a,b time.Duration) {g.t.Helper();if a<=b{g.t.Errorf("expected %s > %s",a,b)}}
func (g *assertionFixture) Lt(a,b time.Duration) {g.t.Helper();if a>=b{g.t.Errorf("expected %s < %s",a,b)}}
func (p *nativePage) MustNavigate(url string) *nativePage {if _,err:=request(p.client,"POST","/session/"+p.id+"/url",map[string]string{"url":url});err!=nil{panic(err)};return p}
func (p *nativePage) MustEval(js string) nativeNumber {
 data,err:=request(p.client,"POST","/session/"+p.id+"/fixture/evaluate",map[string]string{"expression":"("+js+")()"});if err!=nil{panic(err)}
 var observed struct{Type string;Value int};if err=json.Unmarshal(data,&observed);err!=nil{panic(err)};if observed.Type!="number"{panic("actual native result must be a number")};return nativeNumber{observed.Value}
}
''')
    subprocess.run([str(GO_DIRECTORY / "go/bin/gofmt"), "-w", str(fixture / "utils-fixture/utils.go")], check=True)
subprocess.run([str(GO_DIRECTORY / "go/bin/gofmt"), "-w", str(fixture / "native_fixture_test.go")], check=True)
if ORACLE == "chromedp":
    subprocess.run([str(GO_DIRECTORY / "go/bin/gofmt"), "-w", str(fixture / "runtime-schema/schema.go")], check=True)
shutil.copy2(ROOT / "scripts/native-webdriver-gateway.mjs", RUN / "gateway.mjs")
shutil.copy2(ROOT / "scripts/verify-chromedp-native.py", RUN / "harness.py")
shutil.copy2(ROOT / "native/browser/cef-build.json", RUN / "cef-build.json")
go_version = subprocess.check_output([str(go), "version"], text=True).strip()
inputs = originals + inventory(RUN / "original") + inventory(fixture) + inventory(RUN / "sdk") + inventory(GO_DIRECTORY) + resources + [
    {"path": str(RUN / "gateway.mjs"), "sha256": digest(RUN / "gateway.mjs")},
    {"path": str(RUN / "harness.py"), "sha256": digest(RUN / "harness.py")},
    {"path": str(RUN / "cef-build.json"), "sha256": digest(RUN / "cef-build.json")},
    {"path": str(RUN / "runtime/suitecut-browser"), "sha256": digest(RUN / "runtime/suitecut-browser")},
    {"path": str(ROOT / "scripts/verify-chromedp-native.py"), "sha256": digest(ROOT / "scripts/verify-chromedp-native.py")},
]
manifest = {"revision": REVISION, "cefVersion": pin["cefVersion"], "sourceExecutable": str(executable), "sourceSDK": str(sdk), "goVersion": go_version, "inputs": inputs, "adaptations": [
    "The complete original eval_test.go and license are byte-for-byte unchanged.",
    "An authored Go action facade uses only stdlib HTTP/JSON and a SHA-256-pinned generic Go 1.25.3 compiler/test runner; no chromedp/CDP/other browser-driver implementation is installed, imported or delegated to.",
    "The test-only runtime.RemoteObject DTO projects the actual typeof result from a retained native JS handle and its actual native guid; the handle is explicitly disposed after observation. It does not claim the CDP objectId protocol.",
    "Original scalar, undefined, raw JSON byte and remote-type assertions observe actual native CEF evaluation. The generic facade projects host error and JSON contracts without changing browser results.",
]}
if selected is not None:
    manifest["adaptations"] = [
        "The complete pinned original page_eval_test.go and license are retained unchanged. Only the complete TestConcurrentEval function is selected; its original body and assertions are unchanged, with generated import/fixture plumbing recorded separately.",
        "An authored Go page facade delegates actual navigation and actual evaluation to the native engine through stdlib HTTP/JSON. A generic authored goroutine utility implements only parallel test scheduling; no Rod/browser-driver implementation is installed, imported, read or delegated to.",
        "Two promises use the actual browser setTimeout timers. Their numeric results, order and wall-clock concurrency assertions remain unchanged; no mock clock, browser return value or native result is substituted.",
    ]
    manifest["selectedFunctionSHA256"] = hashlib.sha256(selected.encode()).hexdigest()
(RUN / "manifest.json").write_text(json.dumps(manifest,indent=2)+"\n")
print(f"Independent {ORACLE} native snapshot: {RUN}",flush=True)
stderr = (RUN / "gateway-stderr.log").open("w")
gateway = subprocess.Popen(["node", str(RUN / "gateway.mjs"), str(RUN)], stdout=subprocess.PIPE, stderr=stderr, text=True)
exit_code = 1
try:
    endpoint = json.loads(gateway.stdout.readline())["endpoint"]
    env = dict(os.environ, SUITECUT_NATIVE_HTTP=endpoint, GOPROXY="off", GOSUMDB="off", GOTOOLCHAIN="local", GOWORK="off")
    with (RUN / "runner.jsonl").open("w") as output:
        completed = subprocess.run([str(go), "test", "-json", "-count=1", "-timeout=60s", "-parallel=1", "."], cwd=fixture, env=env, stdout=output, stderr=subprocess.STDOUT)
    exit_code = completed.returncode
finally:
    if gateway.poll() is None:
        try:
            request = urllib.request.Request(endpoint+"/shutdown", data=b"{}", method="POST")
            with urllib.request.urlopen(request,timeout=15) as response:
                response.read()
            gateway.wait(timeout=15)
        finally:
            if gateway.poll() is None:
                gateway.kill()
                gateway.wait()
    stderr.close()
events = []
for line in (RUN / "runner.jsonl").read_text().splitlines():
    try:
        events.append(json.loads(line))
    except ValueError:
        pass
results = [{"test": event["Test"], "action":event["Action"], "elapsed":event.get("Elapsed")} for event in events if "Test" in event and event.get("Action") in ["pass","fail","skip"]]
leaf_results = [result for result in results if "/" in result["test"]] if ORACLE == "chromedp" else results
changed = [entry["path"] for entry in inputs if digest(Path(entry["path"])) != entry["sha256"]]
scope = "Selected complete original Rod TestConcurrentEval function: actual native parallel browser promises/timers; no other Rod behavior assessed" if ORACLE == "rod" else "Complete original chromedp evaluation file: 5 top-level tests with 12 native-browser subcases; no other chromedp behavior assessed"
summary = {"revision":REVISION, "runDirectory":str(RUN), "scope":scope, "runnerExitCode":exit_code, "inputsUnchanged":not changed, "changedInputs":changed,"stats":{status:sum(result["action"]==status for result in leaf_results) for status in ["pass","fail","skip"]}, "results":results}
(RUN / "summary.json").write_text(json.dumps(summary,indent=2)+"\n")
print(json.dumps(summary),flush=True)
sys.exit(exit_code or bool(changed))
