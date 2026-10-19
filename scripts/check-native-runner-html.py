# SPDX-License-Identifier: MIT
"""Decode the native HTML independently; do not reconstruct it from templates."""
import argparse
from html.parser import HTMLParser
import json
from pathlib import Path
import subprocess


class Report(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.tests = []
        self.generations = []
        self.scripts = []
        self.workers = []
        self.original_json = []
        self.streams = []
        self.summary = {}
        self.in_summary = False
        self.collect = None
        self.fragments = []
        self.term = None
        self.stream = None
        self.csp = None
        self.visible_text = []

    def handle_starttag(self, tag, attrs):
        values = dict(attrs)
        if tag in ("script", "img", "iframe", "object", "link", "a"):
            self.scripts.append((tag, values))
        assert not any(key.startswith("on") for key in values)
        if tag == "meta" and values.get("http-equiv") == "Content-Security-Policy":
            self.csp = values["content"]
        if "data-test-id" in values:
            self.tests.append(int(values["data-test-id"]))
        if "data-generation" in values:
            self.generations.append(int(values["data-generation"]))
        if "data-worker-index" in values:
            self.workers.append(int(values["data-worker-index"]))
        if "data-stream" in values:
            self.stream = values["data-stream"]
        if tag == "dl" and values.get("id") == "summary":
            self.in_summary = True
        if self.in_summary and tag in ("dt", "dd"):
            self.collect = tag
            self.fragments = []
        if tag == "pre" and (values.get("class") == "original-json" or self.stream):
            self.collect = "original" if values.get("class") == "original-json" else "hex"
            self.fragments = []

    def handle_data(self, data):
        self.visible_text.append(data)
        if self.collect:
            self.fragments.append(data)

    def handle_endtag(self, tag):
        if self.in_summary and tag == "dl":
            self.in_summary = False
        if self.collect == "dt" and tag == "dt":
            self.term = "".join(self.fragments)
            self.collect = None
        elif self.collect == "dd" and tag == "dd":
            self.summary[self.term] = float("".join(self.fragments))
            self.collect = None
        elif tag == "pre" and self.collect in ("original", "hex"):
            text = "".join(self.fragments)
            if self.collect == "original":
                self.original_json.append(json.loads(text))
            else:
                self.streams.append((self.stream, bytes.fromhex(text)))
                self.stream = None
            self.collect = None


def check(directory):
    expected = json.loads((directory / "report.json").read_text())
    decoded = Report()
    decoded.feed((directory / "report.html").read_text())
    assert not decoded.scripts
    assert decoded.csp == "default-src 'none'; style-src 'unsafe-inline'; base-uri 'none'; form-action 'none'"
    summary = expected["summary"]
    for label, key in (("Tests", "tests"), ("Attempts", "attempts"), ("Expected", "expected"),
                       ("Unexpected", "unexpected"), ("Skipped", "skipped"), ("Flaky", "flaky"), ("Did not run", "didNotRun"),
                       ("Global errors", "globalErrors"), ("Total attempt duration (ms)", "totalAttemptDurationMs")):
        assert decoded.summary[label] == summary[key], (label, decoded.summary, summary)
    assert sorted(decoded.tests) == sorted({int(row["testId"]) for row in expected["attempts"]} | {int(row["testId"]) for row in expected["unrun"]})
    assert decoded.generations == [int(row["generation"]) for row in expected["attempts"]]
    assert decoded.workers == [row["workerIndex"] for row in expected["workers"]]
    def needs_display_replacement(text):
        return any((ord(char) < 32 and ord(char) not in (9, 10, 13))
                   or 0xd800 <= ord(char) <= 0xdfff or ord(char) in (0xfffe, 0xffff)
                   for char in text)
    originals = []
    for test_id in sorted({int(row["testId"]) for row in expected["attempts"]}):
        for row in expected["attempts"]:
            if int(row["testId"]) == test_id:
                originals.extend(row[key] for key in ("title", "error") if needs_display_replacement(row[key]))
    for row in expected["unrun"]:
        originals.extend(row[key] for key in ("title", "reason") if needs_display_replacement(row[key]))
    originals.extend(error for error in expected["globalErrors"] if needs_display_replacement(error))
    assert decoded.original_json == originals
    visible = "".join(decoded.visible_text)
    fields = [row[key] for row in expected["attempts"] for key in ("title", "error")]
    fields += [row[key] for row in expected["unrun"] for key in ("title", "reason")]
    fields += expected["globalErrors"]
    for text in fields:
        rendered = "".join("\ufffd" if needs_display_replacement(char) else char for char in text)
        assert rendered in visible, ("missing actual metadata text", repr(text))
    assert decoded.streams == [(name, bytes.fromhex(worker[name]["data"]))
                               for worker in expected["workers"] for name in ("stdout", "stderr")]
    return {"independentHTMLDecode": "PASS", "tests": len(decoded.tests),
            "attempts": len(decoded.generations), "workers": len(decoded.workers),
            "rawStreams": len(decoded.streams), "authoredExecutableElements": len(decoded.scripts),
            "losslessOriginalUTF16Fields": len(decoded.original_json)}


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--directory", type=Path, required=True)
    args = parser.parse_args()
    subprocess.run([str(args.executable.resolve()), str(args.directory.resolve())], check=True)
    result = check(args.directory)
    (args.directory / "decode-report.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result))
