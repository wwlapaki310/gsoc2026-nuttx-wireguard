#!/usr/bin/env python3
"""Build the CoC-styled (reveal.js) edition of the OTFSG Tokyo sharing deck.

The slide content lives in otfsg-tac-slides.src.html (a standalone deck that
also works on its own). This script lifts its slides, CSS and speaker notes
into a reveal.js deck that reuses the Community Over Code Glasgow theme, the
shared frame CSS, the fonts and the images of ../presentation/coc-glasgow-reveal/,
the same way that folder's build.py does for the Glasgow talk.

    python build_reveal.py      # writes otfsg-tac-slides.html
"""

import importlib.util
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REVEAL = HERE.parent / "presentation" / "coc-glasgow-reveal"
SRC = HERE / "otfsg-tac-slides.src.html"
OUT = HERE / "otfsg-tac-slides.html"
REL = "../presentation/coc-glasgow-reveal/"   # where vendor/, img/, css live
ASSETS = "../presentation/assets/"

spec = importlib.util.spec_from_file_location("glasgow_build", REVEAL / "build.py")
gb = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gb)

SLIDES_URL = gb.URL  # the Glasgow talk slides, used by the QR codes
DECK_URL = "https://wwlapaki310.github.io/gsoc2026-nuttx-wireguard/docs/blog/otfsg-tac-slides.html"  # this deck, title QR
GITHUB = "https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard"
LINK = 'target="_blank" rel="noopener noreferrer"'

gb.FOOTER = (  # this event, not the Glasgow dates
    '<div class="coc-footer">'
    '<span>2026/11/04 &middot; Tokyo</span>'
    '<img src="img/footer.png" alt="Community Over Code">'
    '<span>LY Corporation Office</span>'
    '</div>'
)

gb.TITLE = """\
<div class="frame coc-dark">
  <div class="coc-dark-body">
    <p class="kicker">OTFSG Tokyo &middot; 2026/11/04 &middot; LY Corporation Office</p>
    <div class="title-core">
      <h1>個人のOSS活動から、<br>世界のコミュニティに飛び込む</h1>
      <p class="subtitle">渡航支援制度（TAC）を利用して登壇した<br>Community Over Code Glasgow 2026 体験記</p>
    </div>
    <p class="speaker-block"><b>Satoru Akita</b><span>Sony Semiconductor Solutions</span></p>
  </div>
  <a class="qr-card title-qr" href="%s" %s><img src="otfsg-slides-qr.svg" alt="QR code for these slides"><b>SLIDES</b></a>
  <img class="skyline" src="img/front.png" alt="">
</div>""" % (DECK_URL, LINK)

gb.CLOSING = """\
<div class="frame coc-dark">
  <div class="coc-dark-body">
    <p class="kicker">OTFSG Tokyo &middot; Community Over Code Glasgow 2026</p>
    <h1>Thank You</h1>
    <p class="subtitle">ご清聴ありがとうございました。</p>
    <div class="facts">
      <div><b><a href="%s" %s>github.com/wwlapaki310/gsoc2026-nuttx-wireguard</a></b><span>CODE &amp; RECORDS</span></div>
      <div><b><a href="%s" %s>wwlapaki310.github.io/&hellip;/coc-glasgow-reveal/</a></b><span>GLASGOW TALK SLIDES</span></div>
    </div>
    <p class="thanks">Alin Jerpeleaさん／Apache NuttXコミュニティ／TAC／Community Over Codeの運営・スポンサー・現地で交流してくださった皆さん／OTFSG Tokyoの皆さん</p>
    <p class="speaker">Satoru Akita &middot; Sony Semiconductor Solutions</p>
  </div>
  <a class="qr-card closing-qr" href="%s" %s><img src="%sslides-qr.svg" alt="QR code for the Glasgow talk slides"><b>GLASGOW TALK SLIDES</b></a>
  <img class="skyline" src="img/front.png" alt="">
</div>""" % (GITHUB, LINK, SLIDES_URL, LINK, SLIDES_URL, LINK, ASSETS)


def build():
    src = SRC.read_text(encoding="utf-8")
    css = re.search(r"<style>(.*?)</style>", src, re.S).group(1)
    slides = re.findall(r'\n  <section class="([^"]*)">(.*?)\n  </section>',
                        src, re.S)
    if not slides:
        sys.exit("no slides found in %s" % SRC)
    title = re.search(r"<title>(.*?)</title>", src).group(1)
    sections = "\n\n".join(gb.convert(c, i) for c, i in slides)
    tpl = (REVEAL / "template.html").read_text(encoding="utf-8")
    tpl = (tpl.replace('lang="en"', 'lang="ja"')
           .replace("{{SOURCE}}", SRC.name)
           .replace("{{TITLE}}", title)
           .replace("{{CSS}}", gb.lift_css(css))
           .replace("{{SLIDES}}", sections)
           .replace('href="vendor/', 'href="%svendor/' % REL)
           .replace('src="vendor/', 'src="%svendor/' % REL)
           .replace('href="frame-reset.css"', 'href="%sframe-reset.css"' % REL)
           .replace('href="coc.css"',
                    'href="%scoc.css">\n<link rel="stylesheet" href="otfsg-tac-reveal.css"' % REL)
           .replace('src="../assets/', 'src="%s' % ASSETS)
           .replace('src="img/', 'src="%simg/' % REL))
    return tpl, len(slides)


if __name__ == "__main__":
    html, n = build()
    OUT.write_text(html, encoding="utf-8", newline="\n")
    print("wrote %s (%d slides)" % (OUT.name, n))
