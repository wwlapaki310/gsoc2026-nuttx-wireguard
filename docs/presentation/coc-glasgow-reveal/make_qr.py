#!/usr/bin/env python3
"""Make the QR code of the slides URL for the title and closing slides.

    python make_qr.py            # writes ../assets/slides-qr.svg (needs: pip install qrcode)
    python make_qr.py --verify   # also decodes it again (needs: pip install opencv-python-headless)

The same file is used by the title slide and the closing slide of both
editions, so the two QR codes always carry the same address.
"""

import sys
from pathlib import Path

import qrcode

URL = ("https://wwlapaki310.github.io/gsoc2026-nuttx-wireguard/"
       "docs/presentation/coc-glasgow-reveal/index.html")
OUT = Path(__file__).resolve().parent.parent / "assets" / "slides-qr.svg"


def main():
    qr = qrcode.QRCode(error_correction=qrcode.constants.ERROR_CORRECT_M,
                       border=4)  # 4 modules of quiet zone, as the standard asks
    qr.add_data(URL)
    qr.make(fit=True)
    # One <path> of horizontal runs, with a viewBox, so the image scales
    # cleanly to any size (and stays a few KB).
    m, b = qr.modules, qr.border
    n = qr.modules_count + 2 * b
    runs = []
    for y, row in enumerate(m):
        x = 0
        while x < len(row):
            if row[x]:
                w = x
                while w < len(row) and row[w]:
                    w += 1
                runs.append("M%d %dh%dv1h-%dz" % (x + b, y + b, w - x, w - x))
                x = w
            else:
                x += 1
    svg = ('<?xml version="1.0" encoding="UTF-8"?>\n'
           '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %d %d" '
           'width="%d" height="%d" shape-rendering="crispEdges">'
           '<title>QR code: %s</title>'
           '<rect width="%d" height="%d" fill="#fff"/>'
           '<path d="%s" fill="#000"/></svg>\n'
           % (n, n, n * 8, n * 8, URL, n, n, "".join(runs)))
    OUT.write_text(svg, encoding="utf-8", newline="\n")
    print("wrote %s (version %d, %d modules)" % (OUT, qr.version, qr.modules_count))

    if "--verify" in sys.argv:
        import cv2
        import numpy as np
        png = qr.make_image(fill_color="black", back_color="white").get_image()
        arr = np.array(png.convert("L").resize((800, 800)))
        text, _, _ = cv2.QRCodeDetector().detectAndDecode(arr)
        print("decoded:", text)
        sys.exit(0 if text == URL else 1)


if __name__ == "__main__":
    main()
