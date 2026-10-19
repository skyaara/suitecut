#!/usr/bin/env python3
"""Build the pinned Linux web-codec CEF separately; require real playback proof."""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import urllib.request

REPO = Path(__file__).resolve().parent.parent
PROFILE_PATH = REPO / "native/browser/cef-codecs.json"
PROFILE = json.loads(PROFILE_PATH.read_text())
CEF = json.loads((REPO / "native/browser/cef-build.json").read_text())


def digest(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def gn(value):
    return json.dumps(value, separators=(",", ":"))


def git(root, *args):
    return subprocess.check_output(["git", "-C", str(root), *args], text=True).strip()


def resources(root):
    parent = root
    while not parent.exists():
        parent = parent.parent
    memory = re.search(r"^MemTotal:\s+(\d+)\s+kB", Path("/proc/meminfo").read_text(), re.M)
    return {"freeDiskGiB": shutil.disk_usage(parent).free / 2**30,
            "memoryGiB": int(memory[1]) / 2**20 if memory else 0}


def check_sources(source):
    if git(source / "cef", "rev-parse", "HEAD") != PROFILE["cefCommit"]:
        raise ValueError("CEF source commit does not match the codec profile")
    if git(source, "rev-parse", "HEAD") != git(source, "rev-parse", PROFILE["chromiumRef"]):
        raise ValueError("Chromium source is not the pinned tag")
    if git(source / "third_party/ffmpeg", "rev-parse", "HEAD") != PROFILE["ffmpegCommit"]:
        raise ValueError("FFmpeg source commit differs from Chromium's pinned dependency")
    for entry in PROFILE["sourcePins"]:
        if digest(source / entry["path"]) != entry["sha256"]:
            raise ValueError(f"Pinned media source changed: {entry['path']}")


def check_sdk(sdk):
    header = (sdk / "include/cef_version.h").read_text()
    version = re.search(r'^#define CEF_VERSION "([^"]+)"', header, re.M)
    if not version or version[1] != CEF["cefVersion"]:
        raise ValueError("CEF distribution version differs from the repository pin")
    if not (sdk / "Release/libcef.so").is_file():
        raise ValueError("Missing CEF Release/libcef.so")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-root", required=True, type=Path)
    parser.add_argument("--stage", choices=["plan", "checkout", "compile", "runtime", "verify", "all"], default="plan")
    parser.add_argument("--jobs", type=int, default=12)
    parser.add_argument("--cef-sdk", type=Path, help="Exact-version prebuilt SDK for runtime stage; actual codecs are still gated")
    parser.add_argument("--libyuv", type=Path, default=REPO / ".suitecut/libyuv")
    parser.add_argument("--sdk-entry", type=Path, default=REPO / "packages/browser-control/dist/browser.js")
    parser.add_argument("--report", type=Path, help="New JSON receipt path; existing receipts are never replaced")
    args = parser.parse_args()
    if not args.build_root.is_absolute() or args.jobs < 1 or args.jobs > 256:
        parser.error("--build-root must be absolute; --jobs must be between 1 and 256")
    root = args.build_root.resolve()
    if root == Path("/") or root == REPO or root in REPO.parents or root == (REPO / ".suitecut/cef-sdk").resolve():
        parser.error("Use a dedicated build directory, separate from the repository and stock SDK")
    if platform.system() != "Linux" or platform.machine() not in ("x86_64", "amd64"):
        parser.error("This profile requires Linux x86-64")
    args.libyuv = args.libyuv.resolve()
    args.sdk_entry = args.sdk_entry.resolve()
    source = root / "chromium/src"
    gn_defines = " ".join(f"{key}={gn(value)}" for key, value in PROFILE["gnArgs"].items())
    environment = dict(os.environ, GN_DEFINES=gn_defines)
    environment["PATH"] = str(root / "depot_tools") + os.pathsep + environment.get("PATH", "")
    observed = resources(root)
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    report = {"status": "PLAN", "profile": PROFILE, "profileSHA256": digest(PROFILE_PATH),
              "cefVersion": CEF["cefVersion"], "buildRoot": str(root), "stage": args.stage,
              "observedResources": observed, "gnDefines": gn_defines, "commands": [],
              "scope": "Common upstream Linux web codecs; not universal codec or DRM support. Experimental CEF patches are not applied."}

    def run(argv, cwd=REPO):
        item = {"argv": [str(x) for x in argv], "cwd": str(cwd)}
        report["commands"].append(item)
        log = root / f"{stamp}-{len(report['commands']):02d}.log"
        item["log"] = str(log)
        print(f"Running {item['argv'][0]} (log: {log})", flush=True)
        with log.open("xb") as output:
            result = subprocess.run(item["argv"], cwd=cwd, env=environment, stdout=output, stderr=subprocess.STDOUT, check=False)
        item["exitCode"] = result.returncode
        if result.returncode:
            raise ValueError(f"Command failed ({result.returncode}); see {log}")

    def execute():
        if args.stage == "plan":
            report["expectedRuntime"] = str(root / "native/suitecut-browser")
            report["buildCommand"] = [sys.executable, str(Path(__file__).resolve()), "--build-root", str(root), "--stage", "all"]
            report["acceptanceCommand"] = ["node", str(REPO / "scripts/verify-browser-codec-runtime.mjs"), "--executable", str(root / "native/suitecut-browser"), "--output", str(root / "acceptance-NEW")]
            return
        if args.stage in ("checkout", "compile", "all"):
            insufficient = [key for key in ("freeDiskGiB", "memoryGiB") if observed[key] < PROFILE["buildBudget"][key]]
            if insufficient:
                report["status"] = "BLOCKED_BUILD_RESOURCES"
                raise ValueError("Below configured CEF build budget: " + ", ".join(insufficient) + "; no checkout or compilation started")
        marker = root / ".suitecut-codec-build.json"
        identity = {"profileSHA256": report["profileSHA256"], "cefVersion": CEF["cefVersion"]}
        if marker.exists():
            if json.loads(marker.read_text()) != identity:
                raise ValueError("Dedicated build root belongs to a different profile/version")
        else:
            if root.exists() and any(root.iterdir()):
                raise ValueError("Build root is not empty and has no matching ownership marker")
            root.mkdir(parents=True, exist_ok=True)
            marker.write_text(json.dumps(identity, indent=2) + "\n")
        if args.stage in ("checkout", "all"):
            bootstrap = root / "automate-git.py"
            pin = PROFILE["automateScript"]
            if not bootstrap.exists():
                with urllib.request.urlopen(pin["url"], timeout=60) as response:
                    payload = response.read(1024 * 1024 + 1)
                if hashlib.sha256(payload).hexdigest() != pin["sha256"]:
                    raise ValueError("CEF automate script checksum mismatch")
                with bootstrap.open("xb") as output:
                    output.write(payload)
            if digest(bootstrap) != pin["sha256"]:
                raise ValueError("Existing automate script checksum mismatch")
            run([sys.executable, bootstrap, f"--download-dir={root}", f"--branch={PROFILE['cefBranch']}", f"--checkout={PROFILE['cefCommit']}", f"--chromium-checkout={PROFILE['chromiumRef']}", "--no-chromium-history", "--x64-build", "--no-build", "--no-distrib"])
            check_sources(source)
        if args.stage in ("compile", "all"):
            check_sources(source)
            run(["bash", "cef_create_projects.sh"], source / "cef")
            generated = source / "out/Release_GN_x64/args.gn"
            text = generated.read_text()
            for key, value in PROFILE["gnArgs"].items():
                if not re.search(rf"^\s*{re.escape(key)}\s*=\s*{re.escape(gn(value))}\s*(?:#.*)?$", text, re.M):
                    raise ValueError(f"Generated args.gn did not retain required option: {key}")
            report["generatedArgsSHA256"] = digest(generated)
            shutil.copyfile(generated, root / f"{stamp}-args.gn")
            run(["autoninja", "-C", "out/Release_GN_x64", "-j", str(args.jobs), "cefclient", "ceftests", "chrome_sandbox"], source)
            run([sys.executable, "tools/make_distrib.py", f"--output-dir={root / 'distribution'}", "--ninja-build", "--x64-build", "--minimal", "--no-archive"], source / "cef")
        if args.stage in ("runtime", "all"):
            if args.cef_sdk:
                sdk = args.cef_sdk.resolve()
            else:
                candidates = list((root / "distribution").glob("cef_binary_*/include/cef_version.h"))
                if len(candidates) != 1:
                    raise ValueError("Select exactly one rebuilt distribution using --cef-sdk")
                sdk = candidates[0].parent.parent
            check_sdk(sdk)
            libyuv_pin = json.loads((REPO / "native/browser/libyuv-build.json").read_text())["commit"]
            if git(args.libyuv, "rev-parse", "HEAD") != libyuv_pin:
                raise ValueError("libyuv source commit differs from the repository pin")
            if not args.sdk_entry.is_file():
                raise ValueError("Build the standalone SDK before building the runtime acceptance target")
            report["cefSDK"] = str(sdk)
            report["libcefSHA256"] = digest(sdk / "Release/libcef.so")
            run(["cmake", "-S", REPO / "native/browser", "-B", root / "native", "-G", "Ninja", f"-DCEF_ROOT={sdk}", f"-DLIBYUV_ROOT={args.libyuv}", "-DPROJECT_ARCH=x86_64", "-DCMAKE_BUILD_TYPE=Release"])
            run(["cmake", "--build", root / "native", "--target", "suitecut-browser", "--parallel", str(min(args.jobs, 4))])
        if args.stage in ("verify", "all"):
            acceptance = root / f"acceptance-{stamp}"
            run(["node", REPO / "scripts/verify-browser-codec-runtime.mjs", "--executable", root / "native/suitecut-browser", "--sdk", args.sdk_entry, "--output", acceptance, "--profile", PROFILE["acceptanceProfile"]])
            proof = json.loads((acceptance / "acceptance.json").read_text())
            if proof.get("status") != "PASS":
                raise ValueError("Codec runtime did not pass actual playback acceptance")
            report["acceptance"] = str(acceptance / "acceptance.json")
            report["status"] = "PASS_WEB_CODEC_RUNTIME"
        else:
            report["status"] = "STAGE_COMPLETE_PLAYBACK_NOT_VERIFIED"

    try:
        execute()
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        if report["status"] != "BLOCKED_BUILD_RESOURCES":
            report["status"] = "FAILED"
        report["error"] = str(error)
    print(json.dumps(report, indent=2))
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        with args.report.open("x") as output:
            json.dump(report, output, indent=2)
            output.write("\n")
    return 0 if report["status"] in ("PLAN", "STAGE_COMPLETE_PLAYBACK_NOT_VERIFIED", "PASS_WEB_CODEC_RUNTIME") else 1


if __name__ == "__main__":
    raise SystemExit(main())
