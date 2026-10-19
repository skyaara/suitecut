#!/usr/bin/env python3
"""Summarize matched current-SDK browser measurements without rewriting raw runs."""
import argparse
import csv
from datetime import datetime, timezone, timedelta
import json
import math
from pathlib import Path
import statistics


def stats(values):
    values = sorted(float(x) for x in values)
    return {"n": len(values), "p50Ms": statistics.median(values) if values else None,
            "p95Ms": values[math.ceil(len(values) * .95) - 1] if values else None,
            "minMs": min(values) if values else None, "maxMs": max(values) if values else None}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(".suitecut/external-native-perf"))
    parser.add_argument("--business-analysis", type=Path)
    args = parser.parse_args()
    root = args.root.resolve()
    load = lambda name: json.loads((root / name).read_text())
    business = json.loads((args.business_analysis or root / "business/combined/independent-analysis.json").read_text())
    controls = load("controls/full/results.json")
    resources = load("resources/result.json")
    journeys = load("journeys/full/summary.json")
    pairs = {(p["repetition"], p["family"], p["seed"]) for p in business["pairs"]
             if p.get("bothVerified") and p.get("identicalExportBytes")}
    episodes = business["episodes"]
    paired = [e for e in episodes if e["verified"] and (e["repetition"], e["family"], e["seed"]) in pairs]
    pair_episodes = {key: {e["backend"]: e for e in paired if (e["repetition"], e["family"], e["seed"]) == key} for key in pairs}
    report = {"createdIST": datetime.now(timezone(timedelta(hours=5, minutes=30))).isoformat(),
              "scope": "Native CEF vs the same SDK controlling installed Chrome through local CDP. No AI thinking, videos or official public benchmark score.",
              "statistics": "Medians use statistics.median (average of two middle observations when even). p95 uses nearest rank. Original producer summaries may use other median conventions; raw observations are unchanged.",
              "business": {"attempted": len(episodes), "verified": sum(e["verified"] for e in episodes),
                           "matchedSuccessfulPairs": len(pairs), "families": [], "backends": {}},
              "controls": {"cases": [], "warmups": sum(r["warmup"] for r in controls["records"])},
              "resources": {"stages": []}, "journeys": {"passed": journeys["passed"], "cases": []}}
    report["business"]["medianPairedTaskRatioCEFOverChrome"] = statistics.median(p["cef"]["taskMs"] / p["cdp"]["taskMs"] for p in pair_episodes.values())
    lifecycle = lambda e: e["startupMs"] + e["taskMs"] + e["teardownMs"]
    report["business"]["medianPairedLifecycleSubtotalRatioCEFOverChrome"] = statistics.median(lifecycle(p["cef"]) / lifecycle(p["cdp"]) for p in pair_episodes.values())
    report["business"]["timingBoundary"] = "Task includes actions, common observable per-click readiness, download save/read/hash; final fixture grading, state/export matching and independent analysis occur afterwards. OneShot is the timed startup+task+close subtotal, not the complete runner wall time. Startup includes first page/navigation."
    for backend in ("cef", "cdp"):
        all_episodes = [e for e in episodes if e["backend"] == backend]
        selected = [e for e in paired if e["backend"] == backend]
        actions = [a for e in selected for a in e["actions"]]
        report["business"]["backends"][backend] = {
            "attempted": len(all_episodes), "verified": sum(e["verified"] for e in all_episodes),
            "failed": [{"family": e["family"], "seed": e["seed"], "repetition": e["repetition"], "id": e["id"], "error": e.get("rawError")} for e in all_episodes if not e["verified"]],
            "matchedTimingSamples": len(selected), "startup": stats(e["startupMs"] for e in selected),
            "task": stats(e["taskMs"] for e in selected), "close": stats(e["teardownMs"] for e in selected),
            "oneShot": stats(e["startupMs"] + e["taskMs"] + e["teardownMs"] for e in selected),
            "ordinaryControls": stats(a["controlMs"] for a in actions if a["input"].get("action") in ("click", "fill", "check", "select"))}
    for family in sorted({e["family"] for e in episodes}):
        row = {"family": family, "backends": {}}
        for backend in ("cef", "cdp"):
            values = [e for e in paired if e["backend"] == backend and e["family"] == family]
            row["backends"][backend] = {"task": stats(e["taskMs"] for e in values),
                                       "oneShot": stats(e["startupMs"] + e["taskMs"] + e["teardownMs"] for e in values)}
        c, d = (row["backends"][b]["task"]["p50Ms"] for b in ("cef", "cdp"))
        row["cefOverChromeTaskMedian"] = c / d if c and d else None
        report["business"]["families"].append(row)
    measured = [r for r in controls["records"] if not r["warmup"]]
    report["controls"]["measuredAttempts"] = len(measured)
    report["controls"]["failures"] = sum(r["status"] != "passed" for r in measured)
    for name in sorted({r["name"] for r in measured}):
        row = {"name": name, "backends": {}}
        for backend in ("cef", "cdp"):
            all_values = [r for r in measured if r["name"] == name and r["backend"] == backend]
            passed = [r for r in all_values if r["status"] == "passed"]
            row["backends"][backend] = {"attempted": len(all_values), "failed": len(all_values) - len(passed),
                "verified": stats(r["elapsedThroughVerifiedCompletionMs"] for r in passed),
                "call": stats(r["actionMs"] for r in passed),
                "measuredBatchUnitsPerSecond": sum(r["completedUnits"] for r in passed) * 1000 / sum(r["elapsedThroughVerifiedCompletionMs"] for r in all_values)}
        c, d = (row["backends"][b]["verified"]["p50Ms"] for b in ("cef", "cdp"))
        row["cefOverChromeVerifiedMedian"] = c / d if c and d else None
        report["controls"]["cases"].append(row)
    primary = [r for r in resources["results"] if r["kind"] == "primary"]
    if not primary:
        primary = [r for r in resources["results"] if r["id"].startswith("rep")]
    for name in ("idle", "dense", "three-tabs", "vp9-opus-playback"):
        row = {"name": name, "backends": {}}
        for backend in ("cef", "cdp"):
            values = [s for r in primary if r["backend"] == backend for s in r["stages"] if s["name"] == name]
            assert len(values) == 2 and all(r["status"] == "PASS" for r in primary)
            median = lambda fn: statistics.median(fn(s) for s in values)
            row["backends"][backend] = {
                "n": len(values), "browserPSSMiB": median(lambda s: s["after"]["browser"]["pssKiB"] / 1024),
                "nodePSSMiB": median(lambda s: s["after"]["node"]["pssKiB"] / 1024),
                "totalPSSMiB": median(lambda s: (s["after"]["browser"]["pssKiB"] + s["after"]["node"]["pssKiB"]) / 1024),
                "browserCPUms": median(lambda s: s["dwellBrowserCPUms"]),
                "nodeCPUms": median(lambda s: s["dwellNodeCPUms"]),
                "totalCPUms": median(lambda s: s["dwellBrowserCPUms"] + s["dwellNodeCPUms"]),
                "dwellMs": median(lambda s: s["actualDwellMs"]),
                "nativeFrameMiB": median(lambda s: (s["after"]["nativeObservedPayloads"]["framePayloadBytes"] - s["before"]["nativeObservedPayloads"]["framePayloadBytes"]) / 2**20)}
        report["resources"]["stages"].append(row)
    for item in journeys["summaries"]:
        row = {"name": item["name"], "settings": item["settings"], "backends": {}}
        for backend in ("cef", "cdp"):
            row["backends"][backend] = stats(item["backends"][backend]["samplesMs"])
        report["journeys"]["cases"].append(row)
    report["limitations"] = [
        "Primary business task timings include common per-action observable readiness and download save/read/hash; final grading/semantic checks are outside the timed interval. Native locator clicks and screenshots enforce stronger readiness/navigation semantics than CDP. Control verified-completion timings include their per-case outcome checks.",
        "CPU sampling is a lower bound; memory values are sampled PSS, not guaranteed instantaneous peaks or proof of leak freedom.",
        "Native exports raw captured pixels; external CDP does not provide the same capture/viewer feature. Both observed graphics paths use software rendering here.",
        "Successful task timings use only matched independently verified export pairs. Every failed attempt remains in reliability denominators; failures are not assigned zero latency.",
        "Supplemental collaboration retained the original Chrome no-sandbox/disable-dev-shm flags; its four scenario timings are separate from the normally sandboxed primary comparisons.",
        "Resource Chrome runs additionally used --disable-dev-shm-usage (sandbox retained); primary business/control runs used no requested Chrome flags. The resource comparison therefore also includes shared-memory backing configuration differences.",
        "Business repetition 0 and repetitions 1-2 retain the same semantic actions/readiness policy, but later repetitions add per-action timestamp bookkeeping and post-failure DOM/screenshot diagnostics. Both exact harness versions are archived; the harness itself is not byte-identical across repetitions.",
        "An observational close probe found more than 230ms of Chrome descendant draining spent on already-exited zombies under this supervisor topology. Lifecycle results include that host-specific wait.",
        "One resource renderer exited SIGTERM; all observed descendants were retired, with no supervisor-requested kill or timeout. The strict resource cleanup flag is retained.",
        "Custom local deterministic fixtures include deliberate 140ms application waits; no AI reasoning, remote network performance or official benchmark ranking is measured."]
    for filename, key in (("close-diagnostic-result.json", "closeDiagnostic"), ("integrity.json", "finalIntegrity")):
        if (root / filename).exists():
            report[key] = load(filename)
    (root / "comparison.json").write_text(json.dumps(report, indent=2) + "\n")
    with (root / "business.csv").open("w", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(["family", "matched_samples_per_backend", "cef_task_median_ms", "chrome_task_median_ms", "cef_chrome_ratio", "cef_one_shot_median_ms", "chrome_one_shot_median_ms"])
        for row in report["business"]["families"]:
            c, d = (row["backends"][b] for b in ("cef", "cdp"))
            writer.writerow([row["family"], c["task"]["n"], c["task"]["p50Ms"], d["task"]["p50Ms"], row["cefOverChromeTaskMedian"], c["oneShot"]["p50Ms"], d["oneShot"]["p50Ms"]])
    with (root / "controls.csv").open("w", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(["case", "samples_per_backend", "cef_verified_median_ms", "chrome_verified_median_ms", "cef_verified_p95_ms", "chrome_verified_p95_ms", "cef_chrome_ratio"])
        for row in report["controls"]["cases"]:
            c, d = (row["backends"][b]["verified"] for b in ("cef", "cdp"))
            writer.writerow([row["name"], c["n"], c["p50Ms"], d["p50Ms"], c["p95Ms"], d["p95Ms"], row["cefOverChromeVerifiedMedian"]])
    with (root / "journeys.csv").open("w", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(["case", "cef_median_ms", "chrome_median_ms"])
        for row in report["journeys"]["cases"]:
            writer.writerow([row["name"], row["backends"]["cef"]["p50Ms"], row["backends"]["cdp"]["p50Ms"]])
    print(json.dumps({"business": report["business"]["backends"], "measuredControls": len(measured), "journeyCases": len(report["journeys"]["cases"]), "report": str(root / "comparison.json")}, indent=2))


if __name__ == "__main__":
    main()
