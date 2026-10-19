#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Run unchanged pinned CEF parser/string tests against the pinned native SDK.

This scope validates CEF values, URLs and strings, not SuiteCut browser actions.
Only generic GoogleTest and an authored executable entry point are supplied.
"""

import argparse
import datetime
import hashlib
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tarfile
import urllib.request


CEF_REVISION = "47dbc1df023e99f006fb9e850a8d83cc19b4fdd9"
GTEST_REVISION = "52eb8108c5bdec04579160ae17225d66034bd723"
CEF_VERSION = "156.0.7+g47dbc1d+chromium-156.0.8078.17"
INPUTS = {
    "parser_unittest.cc": (
        f"https://raw.githubusercontent.com/chromiumembedded/cef/{CEF_REVISION}/tests/ceftests/parser_unittest.cc",
        "2b877ac82a2803136c8708983051aee72930ad53e7792343016f4ecb196637eb",
    ),
    "string_unittest.cc": (
        f"https://raw.githubusercontent.com/chromiumembedded/cef/{CEF_REVISION}/tests/ceftests/string_unittest.cc",
        "fb3b1d23af0c784613cd41d5cccd7fa0586b1fc82035db44814d2012683c58cf",
    ),
    "CEF-LICENSE.txt": (
        f"https://raw.githubusercontent.com/chromiumembedded/cef/{CEF_REVISION}/LICENSE.txt",
        "058c3827ffb827ff3edda471ae7e1bb1d1aa5931985f0126043ccd33409e792f",
    ),
    "googletest.tar.gz": (
        f"https://codeload.github.com/google/googletest/tar.gz/{GTEST_REVISION}",
        "745c55415660044610f7fcd3af7a6420d5de16a7dbb9ebfe2e131275676232be",
    ),
}


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def run(command, directory, log, timeout=240):
    with log.open("wb") as output:
        result = subprocess.run(
            command, cwd=directory, stdout=output, stderr=subprocess.STDOUT,
            check=False, timeout=timeout,
        )
    if result.returncode:
        raise RuntimeError(f"Command failed ({result.returncode}); see {log}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cef-root", type=pathlib.Path, required=True)
    parser.add_argument("--cef-wrapper", type=pathlib.Path, required=True)
    parser.add_argument("--cmake", default="cmake")
    options = parser.parse_args()
    if sys.platform != "linux":
        raise RuntimeError("This pinned CEF upstream harness is verified on Linux only")
    root = pathlib.Path.cwd()
    cef_root = options.cef_root.resolve(strict=True)
    wrapper = options.cef_wrapper.resolve(strict=True)
    header = cef_root / "include/cef_version.h"
    if f'#define CEF_VERSION "{CEF_VERSION}"' not in header.read_text():
        raise RuntimeError("CEF SDK does not match the exact pinned version")
    timestamp = datetime.datetime.now(datetime.timezone.utc).isoformat().replace(":", "-")
    directory = root / ".suitecut/cef-upstream/runs" / f"{timestamp}-{os.getpid()}"
    directory.mkdir(parents=True)
    print(f"Original CEF parser/string test scope: {directory}", flush=True)
    cache = root / ".suitecut/cef-upstream/cache"
    cache.mkdir(parents=True, exist_ok=True)
    manifest = {
        "cefRevision": CEF_REVISION, "cefVersion": CEF_VERSION,
        "googleTestRevision": GTEST_REVISION,
        "scope": "unchanged CEF parser/string unit tests; browser action parity unassessed",
        "platform": "Linux; original Windows-only WideToUTF8 test is not compiled",
        "adaptations": [
            "Original test files are compiled byte-for-byte, with every assertion retained.",
            "Original GoogleTest include path resolves through an authored symlink.",
            "Authored main initializes the genuine CEF API hash and generic GoogleTest.",
            "C++ wrapper ABI uses -fno-rtti, matching the pinned CEF wrapper.",
        ],
        "upstreamInputs": {}, "sdkSHA256": {}, "generatedSHA256": {},
    }
    for name, (url, expected) in INPUTS.items():
        path = cache / name
        if not path.exists() or sha256(path) != expected:
            with urllib.request.urlopen(url, timeout=30) as response:
                data = response.read(2 * 1024 * 1024 + 1)
            if len(data) > 2 * 1024 * 1024 or hashlib.sha256(data).hexdigest() != expected:
                raise RuntimeError(f"Pinned upstream input failed checksum: {name}")
            pending = cache / f".{name}.{os.getpid()}.pending"
            pending.write_bytes(data)
            pending.replace(path)
        shutil.copyfile(path, directory / name)
        if sha256(directory / name) != expected:
            raise RuntimeError(f"Copied upstream input failed checksum: {name}")
        manifest["upstreamInputs"][name] = {"url": url, "sha256": expected}
    with tarfile.open(directory / "googletest.tar.gz") as archive:
        archive.extractall(directory, filter="data")
    (directory / f"googletest-{GTEST_REVISION}").rename(directory / "googletest")
    link = directory / "tests/gtest/include/gtest"
    link.parent.mkdir(parents=True)
    link.symlink_to(directory / "googletest/googletest/include/gtest")
    sdk = directory / "sdk"
    sdk.mkdir()
    shutil.copytree(cef_root / "include", sdk / "include")
    (sdk / "Release").mkdir()
    # The large, pinned vendor library is read through its original path. Its
    # complete digest is guarded before and after; it is never rewritten here.
    (sdk / "Release/libcef.so").symlink_to(cef_root / "Release/libcef.so")
    shutil.copyfile(wrapper, sdk / "libcef_dll_wrapper.a")
    for path in sorted((sdk / "include").rglob("*")):
        if path.is_file():
            manifest["sdkSHA256"][str(path.relative_to(sdk))] = sha256(path)
    for path in (sdk / "Release/libcef.so", sdk / "libcef_dll_wrapper.a"):
        manifest["sdkSHA256"][str(path.relative_to(sdk))] = sha256(path)
    (directory / "main.cc").write_text('''#include "include/cef_api_hash.h"
#include "include/cef_version_info.h"
#include "gtest/gtest.h"
int main(int argc, char** argv) {
  if (!cef_api_hash(CEF_API_VERSION, 0)) return 2;
  const int expected[] = {CEF_VERSION_MAJOR, CEF_VERSION_MINOR, CEF_VERSION_PATCH,
    CEF_COMMIT_NUMBER, CHROME_VERSION_MAJOR, CHROME_VERSION_MINOR,
    CHROME_VERSION_BUILD, CHROME_VERSION_PATCH};
  for (int index = 0; index < 8; ++index)
    if (cef_version_info(index) != expected[index]) return 3;
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
''')
    (directory / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.20)
project(suitecut_cef_upstream LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
add_subdirectory(googletest)
add_executable(cef-original parser_unittest.cc string_unittest.cc main.cc)
target_include_directories(cef-original PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/sdk" "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(cef-original PRIVATE -fno-rtti)
target_link_libraries(cef-original PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/sdk/libcef_dll_wrapper.a" "${CMAKE_CURRENT_SOURCE_DIR}/sdk/Release/libcef.so" gtest dl)
''')
    for name in ("main.cc", "CMakeLists.txt"):
        manifest["generatedSHA256"][name] = sha256(directory / name)
    (directory / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    run([options.cmake, "-S", ".", "-B", "build", "-DCMAKE_BUILD_TYPE=Release"], directory, directory / "configure.log")
    run([options.cmake, "--build", "build", "--parallel", "2"], directory, directory / "build.log")
    executable = directory / "build/cef-original"
    with (directory / "run.log").open("wb") as log:
        result = subprocess.run(
            [str(executable), f"--gtest_output=json:{directory}/results.json"],
            cwd=directory, stdout=log, stderr=subprocess.STDOUT, timeout=60,
            check=False,
        )
    unchanged = all(sha256(sdk / name) == digest for name, digest in manifest["sdkSHA256"].items())
    unchanged = unchanged and all(
        sha256(directory / name) == expected for name, (_, expected) in INPUTS.items()
    )
    results = json.loads((directory / "results.json").read_text())
    summary = {
        "completed": True, "scope": manifest["scope"],
        "sourceManifest": str(directory / "manifest.json"),
        "testExecutableSHA256": sha256(executable), "sdkInputsUnchanged": unchanged,
        "exitCode": result.returncode,
        **{key: results[key] for key in ("tests", "failures", "disabled", "errors", "time")},
    }
    (directory / "report.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary, indent=2), flush=True)
    if not unchanged or result.returncode or results["tests"] != 40 or any(
        results[key] for key in ("failures", "disabled", "errors")
    ):
        raise RuntimeError(f"Original CEF test scope failed; see {directory}")


if __name__ == "__main__":
    main()
