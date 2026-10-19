#!/usr/bin/env python3
"""Check isolated vendor patches against pinned files without changing sources."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import stat
import subprocess
import sys
import tempfile


class BundleError(ValueError):
    pass


def unique_object(pairs):
    value = {}
    for key, item in pairs:
        if key in value:
            raise BundleError(f"Duplicate manifest key: {key}")
        value[key] = item
    return value


def relative_path(value: str) -> Path:
    if not isinstance(value, str) or not value or "\\" in value or "\0" in value:
        raise BundleError(f"Invalid relative path: {value!r}")
    path = PurePosixPath(value)
    if path.is_absolute() or any(part in ("", ".", "..", ".git") for part in value.split("/")):
        raise BundleError(f"Unsafe relative path: {value!r}")
    return Path(*path.parts)


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha_pin(value):
    if value is not None and (not isinstance(value, str) or not re.fullmatch(r"[0-9a-f]{64}", value)):
        raise BundleError("File and patch pins must be lowercase SHA-256 strings or null")
    return value


def original_file(root: Path, name: str):
    path = root / relative_path(name)
    # Reject child symlinks, including dangling ones. Component roots themselves
    # are resolved once so callers can select a normal checkout through a link.
    current = root
    for part in relative_path(name).parts:
        current = current / part
        if current.is_symlink():
            raise BundleError(f"Symlink source path is unsupported: {name}")
    try:
        metadata = path.stat()
    except FileNotFoundError:
        return None, None
    if not stat.S_ISREG(metadata.st_mode):
        raise BundleError(f"Pinned source is not a regular file: {name}")
    return path.read_bytes(), metadata.st_mode


def git_apply(shadow: Path, patch: Path, *flags: str) -> bytes:
    # A caller's Git worktree/index variables must not redirect a shadow apply
    # into the real checkout. No index, hooks or network operation is requested.
    environment = dict(os.environ)
    for key in list(environment):
        if key.startswith("GIT_"):
            del environment[key]
    environment["GIT_CONFIG_NOSYSTEM"] = "1"
    environment["GIT_CONFIG_GLOBAL"] = os.devnull
    result = subprocess.run(
        ["git", "--no-pager", "-C", str(shadow), "apply", *flags, "--", str(patch)],
        env=environment, capture_output=True, timeout=60, check=False,
    )
    if result.returncode:
        detail = result.stderr.decode("utf-8", errors="replace").strip()
        raise BundleError(f"git apply failed for {patch.name}: {detail}")
    return result.stdout


def patch_paths(shadow: Path, patch: Path, payload: bytes) -> set[str]:
    # Renames/copies and symlink/binary changes need a larger path/mode contract;
    # this helper intentionally accepts ordinary text patches only.
    text = payload.decode("utf-8")
    if re.search(r"^(rename from |rename to |copy from |copy to |GIT binary patch$|Binary files )", text, re.M):
        raise BundleError(f"Rename, copy or binary patch unsupported: {patch.name}")
    if re.search(r"^(?:(?:new file|old|new) mode 120000|index [^\n]* 120000)$", text, re.M):
        raise BundleError(f"Symlink patch unsupported: {patch.name}")
    paths = set()
    for row in git_apply(shadow, patch, "--numstat", "-z").split(b"\0"):
        if not row:
            continue
        fields = row.split(b"\t", 2)
        if len(fields) != 3 or not fields[0].isdigit() or not fields[1].isdigit():
            raise BundleError(f"Unexpected text patch statistics: {patch.name}")
        name = fields[2].decode("utf-8")
        relative_path(name)
        paths.add(name)
    if not paths:
        raise BundleError(f"Patch changes no pinned text files: {patch.name}")
    return paths


def check_bundle(manifest_path: Path, selected_roots: list[str]):
    manifest_path = manifest_path.resolve(strict=True)
    bundle = manifest_path.parent
    manifest = json.loads(manifest_path.read_text(), object_pairs_hook=unique_object)
    if not isinstance(manifest, dict) or type(manifest.get("version")) is not int or manifest["version"] != 1:
        raise BundleError("Expected a version 1 bundle manifest")
    pins = manifest.get("pins")
    if not isinstance(pins, dict) or any(not isinstance(pins.get(key), str) or not pins[key] for key in ("cef", "chromium")):
        raise BundleError("Manifest pins must name cef and chromium source revisions")
    components = manifest.get("components")
    patches = manifest.get("patches")
    if not isinstance(components, dict) or not components or not isinstance(patches, list) or not patches:
        raise BundleError("Manifest requires components and an ordered nonempty patches list")
    roots = {}
    for value in selected_roots:
        component, separator, location = value.partition("=")
        if not separator or not component or not location or component in roots:
            raise BundleError(f"Invalid or duplicate --root: {value}")
        root = Path(location).resolve(strict=True)
        if not root.is_dir():
            raise BundleError(f"Component root is not a directory: {component}")
        roots[component] = root
    if set(roots) != set(components):
        raise BundleError("Provide exactly one --root for each manifest component")

    with tempfile.TemporaryDirectory(prefix="suitecut-cef-check-") as temporary:
        temporary = Path(temporary)
        shadows = {}
        for index, (component, maps) in enumerate(components.items()):
            if not isinstance(maps, dict) or not isinstance(maps.get("base"), dict) or not isinstance(maps.get("output"), dict):
                raise BundleError(f"Missing base/output file maps: {component}")
            if not maps["base"] or set(maps["base"]) != set(maps["output"]):
                raise BundleError(f"Base/output maps must pin the same nonempty file set: {component}")
            shadow = temporary / f"component-{index}"
            shadow.mkdir()
            shadows[component] = shadow
            for name, expected in maps["base"].items():
                relative_path(name)
                sha_pin(expected)
                sha_pin(maps["output"][name])
                payload, mode = original_file(roots[component], name)
                actual = None if payload is None else digest(payload)
                if actual != expected:
                    raise BundleError(f"Base pin mismatch: {component}:{name}")
                if payload is not None:
                    copied = shadow / relative_path(name)
                    copied.parent.mkdir(parents=True, exist_ok=True)
                    copied.write_bytes(payload)
                    copied.chmod(0o755 if mode & 0o111 else 0o644)

        identifiers = set()
        checked = []
        for index, patch in enumerate(patches):
            if not isinstance(patch, dict):
                raise BundleError("Each patch entry must be an object")
            identifier, component = patch.get("id"), patch.get("component")
            if not isinstance(identifier, str) or not identifier or identifier in identifiers:
                raise BundleError("Patch IDs must be unique nonempty strings")
            identifiers.add(identifier)
            if not isinstance(component, str) or component not in components:
                raise BundleError(f"Unknown component for patch {identifier}")
            source = (bundle / relative_path(patch.get("patch"))).resolve(strict=True)
            if not source.is_relative_to(bundle) or not source.is_file():
                raise BundleError(f"Patch must be a file within the bundle: {identifier}")
            expected = sha_pin(patch.get("sha256"))
            payload = source.read_bytes()
            if expected is None or digest(payload) != expected:
                raise BundleError(f"Patch hash mismatch: {identifier}")
            # Apply the exact bytes that were hashed, avoiding a second read of
            # a patch file that could change after validation.
            copied = temporary / f"patch-{index}.patch"
            copied.write_bytes(payload)
            paths = patch_paths(shadows[component], copied, payload)
            if not paths.issubset(components[component]["base"]):
                raise BundleError(f"Patch touches unpinned source paths: {identifier}")
            git_apply(shadows[component], copied, "--check")
            git_apply(shadows[component], copied)
            checked.append(identifier)

        for component, maps in components.items():
            shadow = shadows[component]
            for name, expected in maps["output"].items():
                payload, _ = original_file(shadow, name)
                actual = None if payload is None else digest(payload)
                if actual != expected:
                    raise BundleError(f"Output pin mismatch: {component}:{name}")
            actual_names = {path.relative_to(shadow).as_posix() for path in shadow.rglob("*") if path.is_file() or path.is_symlink()}
            expected_names = {name for name, value in maps["output"].items() if value is not None}
            if actual_names != expected_names:
                raise BundleError(f"Unpinned output files in shadow: {component}")

    return {
        "status": "PASS", "mode": "check-only", "patches": checked,
        "pins": pins, "components": list(components),
        "sourceDirectoriesModified": False,
        "vendorCompilation": "UNRUN", "runtimeVerification": "UNRUN",
        "qualification": "Pinned-file content and ordered patch applicability only; no vendor build or feature capability claim",
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True, help="Version 1 bundle JSON manifest")
    parser.add_argument("--root", action="append", required=True, metavar="COMPONENT=SOURCE_DIR", help="Pinned source root for each component; repeat as needed")
    options = parser.parse_args()
    try:
        report = check_bundle(options.manifest, options.root)
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        print(json.dumps({"status": "FAIL", "error": str(error), "vendorCompilation": "UNRUN"}), file=sys.stderr)
        return 1
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
