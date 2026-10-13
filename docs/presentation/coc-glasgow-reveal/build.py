#!/usr/bin/env python3
"""Generate the reveal.js edition of the Community Over Code Glasgow talk.

The source of truth for slide content is ../coc-glasgow-slides.html (the
standalone deck, kept as the fallback). This script lifts its slides, its
component CSS and its speaker notes into a reveal.js deck that uses the
official CoC Glasgow template theme (vendor/reveal/theme/cocglasgow.css).

    python build.py            # writes index.html
    python build.py --check    # also fails if index.html was out of date

Edit slide content in the standalone deck and re-run this script; edit the
CoC look in frame-reset.css / coc.css; edit the title and closing slides here.
"""

import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
SRC = HERE.parent / "coc-glasgow-slides.html"
OUT = HERE / "index.html"

# Rules of the standalone deck that belong to its own viewer (stage, scaling,
# progress bar, help, notes overlay) or to its title layout, replaced here.
DROPPED = ("body", "#stage", "#canvas", "#bar", "#help", "#notes",
           ".notes-src", ".title-slide", ".slide::after")

FOOTER = (
    '<div class="coc-footer">'
    '<span>October 11&ndash;14, 2026</span>'
    '<img src="img/footer.png" alt="Community Over Code">'
    '<span>Glasgow, Scotland</span>'
    '</div>'
)

TITLE = """\
<div class="frame coc-dark">
  <div class="coc-dark-body">
    <p class="kicker">Community Over Code &middot; Glasgow 2026</p>
    <div class="title-core">
      <h1>WireGuard for Apache NuttX</h1>
      <p class="subtitle">Bringing Secure Remote Access to Embedded Devices</p>
    </div>
    <p class="speaker-block"><b>Satoru Akita</b><span>Sony Semiconductor Solutions</span></p>
  </div>
  <a class="qr-card title-qr" href="{URL}" target="_blank" rel="noopener noreferrer"><img src="../assets/slides-qr.svg" alt="QR code for the slides"><b>SLIDES</b></a>
  <img class="skyline" src="img/front.png" alt="">
</div>"""

CLOSING = """\
<div class="frame coc-dark">
  <div class="coc-dark-body">
    <p class="kicker">Community Over Code &middot; Glasgow 2026</p>
    <h1>Thank You &mdash; Questions?</h1>
    <p class="subtitle">Questions welcome &mdash; the kernel driver, the bugs, or the hardware.</p>
    <div class="facts">
      <div><b><a href="{URL}" target="_blank" rel="noopener noreferrer">wwlapaki310.github.io/gsoc2026-nuttx-wireguard/&hellip;/coc-glasgow-reveal/</a></b><span>SLIDES &amp; RESOURCES</span></div>
      <div><b><a href="https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard" target="_blank" rel="noopener noreferrer">github.com/wwlapaki310/gsoc2026-nuttx-wireguard</a></b><span>CODE &amp; RECORDS</span></div>
    </div>
    <p class="speaker">Satoru Akita &middot; Sony Semiconductor Solutions</p>
  </div>
  <a class="qr-card closing-qr" href="{URL}" target="_blank" rel="noopener noreferrer"><img src="../assets/slides-qr.svg" alt="QR code for the slides"><b>SCAN TO VIEW THE SLIDES</b></a>
  <img class="skyline" src="img/front.png" alt="">
</div>"""

# One address for both QR codes: keep it in sync with make_qr.py.
URL = "https://wwlapaki310.github.io/gsoc2026-nuttx-wireguard/docs/presentation/coc-glasgow-reveal/index.html"
TITLE = TITLE.replace("{URL}", URL)
CLOSING = CLOSING.replace("{URL}", URL)


def lift_css(css):
    """Scope the standalone deck's CSS under .reveal .frame."""
    css = re.sub(r"/\*.*?\*/", "", css, flags=re.S)
    # The only @media block animates the standalone progress bar.
    css = re.sub(r"@media[^{]*\{(?:[^{}]*\{[^}]*\})*\s*\}", "", css)
    out = []
    for selector, body in re.findall(r"([^{}]+)\{([^}]*)\}", css):
        mapped = []
        for sel in (s.strip() for s in selector.split(",")):
            if sel == ":root":
                mapped.append(sel)
            elif sel == "*":
                mapped += [".reveal .frame", ".reveal .frame *"]
            elif sel.startswith(DROPPED):
                continue
            elif sel in (".slide", ".slide.on"):
                mapped.append(".reveal .frame")
            else:
                mapped.append(".reveal .frame " + sel)
        if mapped:
            out.append("  %s {%s}" % (",\n  ".join(mapped), body.rstrip()))
    return "\n".join(out)


def notes(inner):
    m = re.search(r'<div class="notes-src">(.*?)</div>', inner, re.S)
    if not m:
        return ""
    paras = [p.strip() for p in m.group(1).split("||") if p.strip()]
    # Essential text is read as it is. "[opt]" paragraphs are read only if
    # there is time, "[note]" paragraphs are never read (sources, steps).
    labels = {"[opt]": "OPTIONAL: ", "[note]": "NOTE, do not read: "}
    shown = []
    for p in paras:
        for tag, label in labels.items():
            if p.startswith(tag):
                p = label + p[len(tag):].strip()
                break
        shown.append(p)
    return ('    <aside class="notes">\n%s\n    </aside>\n'
            % "\n".join("      <p>%s</p>" % p for p in shown))


def convert(cls, inner):
    note = notes(inner)
    if "title-slide" in cls:
        frame = TITLE
    elif "Questions?</p>" in inner:
        frame = CLOSING
    else:
        body = re.sub(r'\s*<div class="notes-src">.*?</div>', "", inner,
                      flags=re.S)
        body = body.replace('"assets/', '"../assets/').rstrip()
        extra = " ".join(c for c in cls.split() if c != "slide")
        frame = ('<div class="frame%s">%s\n    %s\n  </div>'
                 % (" " + extra if extra else "", body, FOOTER))
    attrs = (' data-background-color="#271122"'
             if "coc-dark" in frame else "")
    return "  <section%s>\n  %s\n%s  </section>" % (attrs, frame, note)


def build():
    src = SRC.read_text(encoding="utf-8")
    css = re.search(r"<style>(.*?)</style>", src, re.S).group(1)
    slides = re.findall(r'\n  <section class="([^"]*)">(.*?)\n  </section>',
                        src, re.S)
    if not slides:
        sys.exit("no slides found in %s" % SRC)
    title = re.search(r"<title>(.*?)</title>", src).group(1)
    # Slides marked "appendix" are not part of the main flow: they become a
    # vertical stack under the last main slide, reached with the down arrow.
    groups = []
    for c, i in slides:
        if "appendix" in c.split() and groups:
            groups[-1].append(convert(c, i))
        else:
            groups.append([convert(c, i)])
    sections = "\n\n".join(
        g[0] if len(g) == 1 else
        "  <section>\n%s\n  </section>" % "\n\n".join(g) for g in groups)
    template = (HERE / "template.html").read_text(encoding="utf-8")
    return (template.replace("{{TITLE}}", title)
            .replace("{{SOURCE}}", SRC.name)
            .replace("{{CSS}}", lift_css(css))
            .replace("{{SLIDES}}", sections)), len(slides)


def main():
    html, count = build()
    old = OUT.read_text(encoding="utf-8") if OUT.exists() else None
    if "--check" in sys.argv and old != html:
        sys.exit("%s is out of date; run build.py" % OUT.name)
    OUT.write_text(html, encoding="utf-8", newline="\n")
    print("wrote %s (%d slides)" % (OUT.name, count))


if __name__ == "__main__":
    main()
