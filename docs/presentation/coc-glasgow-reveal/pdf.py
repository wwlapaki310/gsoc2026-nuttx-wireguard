#!/usr/bin/env python3
"""Make PDFs of the Glasgow deck and its talk scripts (headless Chrome/Edge).

    python pdf.py                   # all of them
    python pdf.py slides            # just the deck
    python pdf.py scripts           # just the scripts and the Q&A

Deck: every slide of index.html is captured at 2x (2560x1440) and the PNGs
are laid out one per 1280x720 page, then printed to
../coc-glasgow-slides.pdf. reveal.js's own ?print-pdf layout comes out
blank in headless Chrome (see README), so screenshots are used instead.
The speaker notes are not in this PDF; they are the scripts below.

Scripts: ../talkscript/coc-glasgow-{script-en,script-ja,qa}.md are turned
into A4 PDFs next to them. Needs the "markdown" package
(python -m pip install --user markdown).
"""

import base64
import re
import sys
import tempfile
from pathlib import Path

from check import browser, run

HERE = Path(__file__).resolve().parent
DECK_PDF = HERE.parent / "coc-glasgow-slides.pdf"
TALK = HERE.parent / "talkscript"
SCRIPTS = ["coc-glasgow-script-en.md", "coc-glasgow-script-ja.md",
           "coc-glasgow-qa.md"]

SCRIPT_CSS = """
@page { size: A4; margin: 16mm 15mm; }
body { font-family: "Segoe UI", "Yu Gothic UI", "Meiryo", sans-serif;
       font-size: 10.5pt; line-height: 1.6; color: #111; }
h1 { font-size: 18pt; border-bottom: 2px solid #333; padding-bottom: 4px; }
h2 { font-size: 14pt; margin-top: 1.4em; border-bottom: 1px solid #999;
     break-after: avoid; }
h3 { font-size: 12pt; margin-top: 1.2em; break-after: avoid; }
table { border-collapse: collapse; margin: .6em 0; }
th, td { border: 1px solid #999; padding: 3px 6px; vertical-align: top; }
th { background: #eee; }
code { font-family: Consolas, "MS Gothic", monospace; font-size: 9.5pt;
       background: #f3f3f3; padding: 0 2px; }
pre { background: #f3f3f3; padding: 6px 8px; white-space: pre-wrap; }
pre code { background: none; }
blockquote { border-left: 3px solid #aaa; margin-left: 0; padding-left: 10px;
             color: #333; }
a { color: #0645ad; text-decoration: none; }
"""


def print_pdf(html_path, pdf_path):
    r = run(["--no-pdf-header-footer", "--print-to-pdf=%s" % pdf_path,
             html_path.as_uri()])
    if not pdf_path.exists():
        sys.exit("printing %s failed:\n%s" % (html_path, r.stderr))
    print("wrote %s (%d KB)" % (pdf_path, pdf_path.stat().st_size // 1024))


def slides(tmp):
    url = (HERE / "index.html").as_uri()
    dom = run(["--window-size=1280,720", "--dump-dom", url]).stdout
    count = len(re.findall(r"<section", dom))
    if count == 0:
        sys.exit("index.html rendered no slides")

    pages = []
    for i in range(count):
        png = tmp / ("slide-%02d.png" % (i + 1))
        run(["--window-size=1280,720", "--force-device-scale-factor=2",
             "--screenshot=%s" % png, "%s#/%d" % (url, i)])
        if not png.exists():
            sys.exit("screenshot of slide %d failed" % (i + 1))
        data = base64.b64encode(png.read_bytes()).decode()
        pages.append('<img src="data:image/png;base64,%s">' % data)
        print("slide %d/%d" % (i + 1, count), end="\r")

    html = tmp / "deck.html"
    html.write_text(
        "<!doctype html><meta charset=utf-8><style>"
        "@page{size:1280px 720px;margin:0}html,body{margin:0}"
        "img{display:block;width:1280px;height:720px;break-after:page}"
        "</style>" + "".join(pages), encoding="utf-8")
    print_pdf(html, DECK_PDF)


LIST_ITEM = re.compile(r"^\s*(?:[-*+]|\d+\.)\s")


def list_breaks(text):
    """Python-Markdown needs a blank line before a list (GitHub does not).

    The scripts start lists right under a paragraph line, which would
    otherwise be run into the paragraph.
    """
    out = []
    for line in text.splitlines():
        prev = out[-1] if out else ""
        if (LIST_ITEM.match(line) and prev.strip() and not LIST_ITEM.match(prev)
                and not prev.lstrip().startswith(("|", "#", ">"))
                and not prev.startswith(" ")):
            out.append("")
        out.append(line)
    return "\n".join(out) + "\n"


def scripts(tmp):
    import markdown

    for name in SCRIPTS:
        src = TALK / name
        body = markdown.markdown(list_breaks(src.read_text(encoding="utf-8")),
                                 extensions=["tables", "fenced_code",
                                             "sane_lists"])
        title = re.search(r"<h1>(.*?)</h1>", body)
        html = tmp / (src.stem + ".html")
        html.write_text(
            "<!doctype html><html lang=%s><meta charset=utf-8><title>%s"
            "</title><style>%s</style><body>%s</body></html>"
            % ("ja" if "-ja" in name or "qa" in name else "en",
               re.sub("<[^>]+>", "", title.group(1)) if title else src.stem,
               SCRIPT_CSS, body),
            encoding="utf-8")
        print_pdf(html, src.with_suffix(".pdf"))


def main():
    what = sys.argv[1:] or ["slides", "scripts"]
    browser()
    with tempfile.TemporaryDirectory() as d:
        tmp = Path(d)
        if "slides" in what:
            slides(tmp)
        if "scripts" in what:
            scripts(tmp)


if __name__ == "__main__":
    main()
