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
    <h1>WireGuard for Apache NuttX</h1>
    <p class="subtitle">A WireGuard VPN that works as a normal network device, <code>wg0</code> &mdash; on real boards, and inside a real NuttX kernel build.</p>
    <div class="facts">
      <div><b>ESP32-S3 &amp; SPRESENSE</b><span>TUNNEL UP OVER REAL WI-FI</span></div>
      <div><b>Real WireGuard peers</b><span>LINUX KERNEL, WINDOWS CLIENT</span></div>
      <div><b>rv-virt knetnsh64</b><span>REAL KERNEL BUILD (BUILD_KERNEL)</span></div>
      <div><b>NuttX 13.0.1 / master / 12.7.0</b><span>APPS V0.1.1 &middot; ONE SOURCE</span></div>
    </div>
    <p class="speaker">Satoru Akita &middot; Sony Semiconductor Solutions</p>
  </div>
  <img class="skyline" src="img/front.png" alt="">
</div>"""

CLOSING = """\
<div class="frame coc-dark">
  <div class="coc-dark-body">
    <p class="kicker">Community Over Code &middot; Glasgow 2026</p>
    <h1>Thank you</h1>
    <p class="subtitle">Questions welcome &mdash; the kernel driver, the bugs, the ioctl ABI, or the hardware.</p>
    <div class="facts">
      <div><b>youtu.be/1kyX2av5WG4</b><span>DEMO</span></div>
      <div><b>apache/nuttx &middot; apache/nuttx-apps</b><span>GOING UPSTREAM</span></div>
    </div>
    <p class="speaker">Satoru Akita &middot; Sony Semiconductor Solutions</p>
  </div>
  <img class="skyline" src="img/front.png" alt="">
</div>"""


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
    return ('    <aside class="notes">\n%s\n    </aside>\n'
            % "\n".join("      <p>%s</p>" % p for p in paras))


def convert(cls, inner):
    note = notes(inner)
    if "title-slide" in cls:
        frame = TITLE
    elif '<p class="eyebrow">Thank you</p>' in inner:
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
    sections = "\n\n".join(convert(c, i) for c, i in slides)
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
