#!/usr/bin/env python3
"""Render index.html in headless Chrome/Edge and report layout problems.

    python check.py                 # overflow report for every slide
    python check.py --shots DIR     # also save one PNG per slide into DIR

A slide is reported when an element leaves the 1280x720 frame or runs
under the CoC footer band, or when a .body block overflows its box.
"""

import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
CANDIDATES = [
    os.environ.get("BROWSER", ""),
    r"C:\Program Files\Google\Chrome\Application\chrome.exe",
    r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
    "google-chrome", "chromium", "chromium-browser", "microsoft-edge",
]


def browser():
    for c in CANDIDATES:
        if c and (Path(c).exists() or shutil.which(c)):
            return c
    sys.exit("no Chrome/Edge found; set BROWSER")


def run(args):
    base = [browser(), "--headless=new", "--disable-gpu", "--hide-scrollbars",
            "--allow-file-access-from-files", "--virtual-time-budget=8000"]
    return subprocess.run(base + args, capture_output=True, text=True,
                          encoding="utf-8", errors="replace")


def main():
    url = (HERE / "index.html").as_uri()
    dom = run(["--window-size=1280,720", "--dump-dom", url + "?check"]).stdout
    # Anchor on the JSON array: the marker strings also appear in the script.
    m = re.search(r"CHECK-REPORT(\[.*?\])END-REPORT", dom, re.S)
    if not m:
        sys.exit("no report in rendered DOM (did the deck fail to load?)")
    report = json.loads(m.group(1))
    count = len(re.findall(r"<section", dom))
    for entry in report:
        print("slide %d:" % entry["slide"])
        for issue in entry["issues"]:
            print("   ", issue)
    print("%d slides, %d with layout issues" % (count, len(report)))

    if "--shots" in sys.argv:
        out = Path(sys.argv[sys.argv.index("--shots") + 1])
        out.mkdir(parents=True, exist_ok=True)
        for i in range(count):
            png = out / ("slide-%02d.png" % (i + 1))
            run(["--window-size=1280,720", "--screenshot=%s" % png,
                 "%s#/%d" % (url, i)])
        print("screenshots in", out)
    sys.exit(1 if report else 0)


if __name__ == "__main__":
    main()
