#!/usr/bin/env python3
"""Renumber the slides of ../coc-glasgow-slides.html.

    python renumber.py

Every slide gets "N / TOTAL" in its corner. Slides inside a part also get
the part and their place in it, e.g. "PART 2 · 3/4". A part starts at an
agenda slide whose highlighted item (class "agenda-item current") gives
the part number; that agenda slide shows "PART k" only, and the slides
after it count 1..n. Run it after adding, removing or moving slides, then
run build.py.
"""

import re
from pathlib import Path

DECK = Path(__file__).resolve().parent.parent / "coc-glasgow-slides.html"
NUM = re.compile(r'<div class="num">.*?</div>')


def main():
    src = DECK.read_text(encoding="utf-8")
    crlf = "\r\n" in src
    s = src.replace("\r\n", "\n")
    head, *slides = re.split(r"(?=\n  <section\b)", s)
    total = len(slides)

    # Part of each slide: None before the first divider.
    parts, part = [], None
    for sec in slides:
        m = re.search(r'agenda-item current">\s*<div class="agenda-num">(\d+)', sec)
        if m:
            part = int(m.group(1))
            parts.append((part, True))
        else:
            parts.append((part, False))
    # The closing slides after the last part (none today) would stay in it.

    sizes = {}
    for p, is_div in parts:
        if p is not None and not is_div:
            sizes[p] = sizes.get(p, 0) + 1

    out, seen = [], {}
    for i, (sec, (p, is_div)) in enumerate(zip(slides, parts), 1):
        if p is None:
            label = ""
        elif is_div:
            label = '<span class="part">PART %d</span>' % p
        else:
            seen[p] = seen.get(p, 0) + 1
            label = '<span class="part">PART %d &middot; %d/%d</span>' % (
                p, seen[p], sizes[p])
        num = '<div class="num">%s%d / %d</div>' % (label, i, total)
        sec, n = NUM.subn(num, sec, count=1)
        if n != 1:
            raise SystemExit("slide %d has no num" % i)
        out.append(sec)

    s = head + "".join(out)
    DECK.write_text(s.replace("\n", "\r\n") if crlf else s, encoding="utf-8",
                    newline="")
    print("renumbered %d slides, parts: %s" % (total, sizes))


if __name__ == "__main__":
    main()
