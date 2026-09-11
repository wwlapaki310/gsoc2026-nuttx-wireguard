#!/usr/bin/env python3
"""Render each slide of an HTML deck in docs/presentation/ to a PNG.

The decks in this repository are single-file HTML: a fixed 1280x720 ``#canvas``
holding one ``<section class="slide">`` per slide, scaled to the window by
script and addressed by URL fragment (``deck.html#3`` opens slide 3).  This
renders them with headless Chrome, one PNG per slide, at 1:1 pixel size.

To keep the capture clean the deck is copied next to the original as a
throwaway ``.render-*.tmp.html`` (so relative ``assets/`` paths still resolve)
with a small stylesheet injected: the presenter chrome is hidden and the
canvas scaling is pinned to 1.0 so the window is exactly the slide.

Usage
-----
    python scripts/render-slides.py docs/presentation/returns-slides.html
    python scripts/render-slides.py docs/presentation/slides.html --slides 1-5,9
    python scripts/render-slides.py <deck> --scale 1 --out /tmp/shots

Requires Chrome, Chromium or Edge; pass ``--browser`` if it is not found.
"""

import argparse
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

SLIDE_RE = re.compile(r'<section[^>]*class="[^"]*\bslide\b', re.I)

# Hide the presenter chrome and stop the deck from scaling itself, so the
# window maps 1:1 onto the 1280x720 canvas.  Stylesheet !important beats the
# inline transform that the deck's fit() writes.
OVERRIDES = """
<style id="render-overrides">
  #help, #notes { display: none !important; }
  #stage {
    position: static !important;
    display: block !important;
    place-items: initial !important;
    inset: auto !important;
  }
  #canvas { transform: none !important; margin: 0 !important; }
  html, body { margin: 0 !important; padding: 0 !important; overflow: hidden !important; }
</style>
"""

# The slides clip silently (`.slide` is overflow:hidden), so content that does
# not fit just disappears off the bottom edge.  --check-overflow paints a
# magenta band and a pixel count over any slide that overflows, which then
# shows up in the PNG.
OVERFLOW_CHECK = """
<script id="render-overflow-check">
window.addEventListener("load", function () {
  setTimeout(function () {
    var slide = document.querySelector(".slide.on") || document.querySelector(".slide");
    if (!slide) { return; }
    var over = slide.scrollHeight - slide.clientHeight;
    var wide = slide.scrollWidth - slide.clientWidth;
    if (over <= 1 && wide <= 1) { return; }
    var flag = document.createElement("div");
    flag.style.cssText = "position:absolute;left:0;right:0;bottom:0;z-index:99;"
      + "padding:8px 14px;background:#d9008a;color:#fff;font:700 15px/1.3 monospace;"
      + "letter-spacing:.06em";
    flag.textContent = "OVERFLOW  +" + Math.max(over, 0) + "px tall  +"
      + Math.max(wide, 0) + "px wide";
    slide.appendChild(flag);
    slide.style.outline = "4px solid #d9008a";
    slide.style.outlineOffset = "-4px";
  }, 150);
});
</script>
"""

BROWSER_CANDIDATES = [
    r"C:\Program Files\Google\Chrome\Application\chrome.exe",
    r"C:\Program Files (x86)\Google\Chrome\Application\chrome.exe",
    r"C:\Program Files\Microsoft\Edge\Application\msedge.exe",
    r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
    "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
    "/Applications/Chromium.app/Contents/MacOS/Chromium",
    "/usr/bin/google-chrome",
    "/usr/bin/chromium",
    "/usr/bin/chromium-browser",
]


def find_browser(explicit=None):
    if explicit:
        if not os.path.exists(explicit):
            sys.exit("browser not found: %s" % explicit)
        return explicit
    for name in ("google-chrome", "chromium", "chromium-browser", "chrome", "msedge"):
        found = shutil.which(name)
        if found:
            return found
    for path in BROWSER_CANDIDATES:
        if os.path.exists(path):
            return path
    sys.exit("no Chrome/Chromium/Edge found - pass --browser <path>")


def parse_slide_spec(spec, total):
    """'1-3,7' -> [1, 2, 3, 7]; None -> every slide."""
    if not spec:
        return list(range(1, total + 1))
    wanted = []
    for part in spec.split(","):
        part = part.strip()
        if not part:
            continue
        if "-" in part:
            lo, hi = part.split("-", 1)
            wanted.extend(range(int(lo), int(hi) + 1))
        else:
            wanted.append(int(part))
    bad = [n for n in wanted if n < 1 or n > total]
    if bad:
        sys.exit("slide out of range (deck has %d): %s" % (total, bad))
    return sorted(set(wanted))


def inject(html, check_overflow=False):
    """Put the render overrides last in <head> so they win."""
    extra = OVERRIDES + (OVERFLOW_CHECK if check_overflow else "")
    at = html.lower().rfind("</head>")
    if at != -1:
        return html[:at] + extra + html[at:]
    return extra + html


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("deck", help="path to the deck HTML")
    ap.add_argument("--out", help="output directory "
                                  "(default: <deck dir>/renders/<deck name>/)")
    ap.add_argument("--slides", help="subset to render, e.g. '1-3,7' (default: all)")
    ap.add_argument("--width", type=int, default=1280, help="canvas width (default 1280)")
    ap.add_argument("--height", type=int, default=720, help="canvas height (default 720)")
    ap.add_argument("--scale", type=float, default=2.0,
                    help="device pixel ratio; 2 gives 2560x1440 PNGs (default 2)")
    ap.add_argument("--wait", type=int, default=5000,
                    help="ms of virtual time to let webfonts and images settle (default 5000)")
    ap.add_argument("--browser", help="path to Chrome/Chromium/Edge")
    ap.add_argument("--check-overflow", action="store_true",
                    help="mark slides whose content does not fit inside 1280x720")
    ap.add_argument("--keep-temp", action="store_true", help="leave the injected copy in place")
    args = ap.parse_args()

    deck = pathlib.Path(args.deck).resolve()
    if not deck.is_file():
        sys.exit("no such deck: %s" % deck)

    html = deck.read_text(encoding="utf-8")
    total = len(SLIDE_RE.findall(html))
    if not total:
        sys.exit("no '<section class=\"slide\">' found in %s" % deck.name)
    wanted = parse_slide_spec(args.slides, total)

    out_dir = pathlib.Path(args.out) if args.out else deck.parent / "renders" / deck.stem
    out_dir.mkdir(parents=True, exist_ok=True)

    browser = find_browser(args.browser)

    # The copy has to sit beside the original: the decks reference assets/ by
    # relative path, and a file:// page resolves those against its own folder.
    tmp_html = deck.with_name(".render-%s.tmp.html" % deck.stem)
    tmp_html.write_text(inject(html, args.check_overflow), encoding="utf-8")
    profile = tempfile.mkdtemp(prefix="render-slides-")

    base = tmp_html.as_uri()
    written = []
    try:
        for n in wanted:
            png = out_dir / ("slide-%02d.png" % n)
            cmd = [
                browser,
                "--headless=new",
                "--disable-gpu",
                "--hide-scrollbars",
                "--no-first-run",
                "--no-default-browser-check",
                "--disable-extensions",
                "--user-data-dir=%s" % profile,
                "--force-device-scale-factor=%s" % args.scale,
                "--window-size=%d,%d" % (args.width, args.height),
                "--virtual-time-budget=%d" % args.wait,
                "--screenshot=%s" % png,
                "%s#%d" % (base, n),
            ]
            proc = subprocess.run(cmd, capture_output=True, text=True)
            if not png.is_file():
                sys.stderr.write(proc.stderr[-2000:] + "\n")
                sys.exit("slide %d failed to render" % n)
            written.append(png)
            print("%s  (%d KiB)" % (png, png.stat().st_size // 1024))
    finally:
        if not args.keep_temp:
            tmp_html.unlink(missing_ok=True)
        shutil.rmtree(profile, ignore_errors=True)

    print("\n%d/%d slides -> %s" % (len(written), total, out_dir))


if __name__ == "__main__":
    main()
