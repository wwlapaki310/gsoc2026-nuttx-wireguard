#!/usr/bin/env python3
"""Build a web page from the beginner guide.

    python build_guide_html.py

Reads coc-glasgow-beginner-guide-ja.md and writes
coc-glasgow-beginner-guide-ja.html next to it, for GitHub Pages:
headings get the same anchors as on GitHub, mermaid blocks are drawn in
the browser, and links to other Markdown files point at GitHub (Pages
does not serve them as pages). Needs the "markdown" package.
"""

import html
import re
import unicodedata
from pathlib import Path

import markdown

HERE = Path(__file__).resolve().parent
SRC = HERE / "coc-glasgow-beginner-guide-ja.md"
OUT = SRC.with_suffix(".html")
BLOB = "https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/blob/main/"
REPO_DIR = "docs/presentation/"


def github_slug(value, separator="-"):
    """GitHub's heading anchors: lower case, drop punctuation, spaces -> -."""
    value = html.unescape(re.sub(r"<[^>]+>", "", value)).strip().lower()
    out = []
    for ch in value:
        cat = unicodedata.category(ch)
        if ch in " -":
            out.append("-" if ch == " " else ch)
        elif ch == "_" or cat[0] in "LNM":
            out.append(ch)
    return "".join(out)


def list_breaks(text):
    """Python-Markdown needs a blank line before a list; GitHub does not."""
    item = re.compile(r"^\s*(?:[-*+]|\d+\.)\s")
    out, fence = [], False
    for line in text.splitlines():
        if line.lstrip().startswith("```"):
            fence = not fence
        prev = out[-1] if out else ""
        if (not fence and item.match(line) and prev.strip()
                and not item.match(prev)
                and not prev.lstrip().startswith(("|", "#", ">"))
                and not prev.startswith(" ")):
            out.append("")
        out.append(line)
    return "\n".join(out) + "\n"


def fix_links(body):
    def repl(m):
        url = m.group(1)
        if re.match(r"^(?:[a-z]+:|#)", url):
            return m.group(0)
        path, _, frag = url.partition("#")
        if path.endswith(".md"):
            parts = (REPO_DIR + path).split("/")
            norm = []
            for p in parts:
                if p == "..":
                    norm.pop()
                elif p not in ("", "."):
                    norm.append(p)
            return 'href="%s%s%s"' % (BLOB, "/".join(norm),
                                      "#" + frag if frag else "")
        return m.group(0)
    return re.sub(r'href="([^"]+)"', repl, body)


CSS = """
:root { --bg:#ffffff; --fg:#1f2328; --muted:#59636e; --line:#d1d9e0;
  --code:#f6f8fa; --accent:#7c297d; --link:#0969da; }
@media (prefers-color-scheme: dark) { :root:not([data-theme="light"]) {
  --bg:#0d1117; --fg:#e6edf3; --muted:#9198a1; --line:#3d444d;
  --code:#151b23; --accent:#d2a8ff; --link:#4493f8; } }
:root[data-theme="dark"] { --bg:#0d1117; --fg:#e6edf3; --muted:#9198a1;
  --line:#3d444d; --code:#151b23; --accent:#d2a8ff; --link:#4493f8; }
* { box-sizing: border-box; }
body { margin:0; background:var(--bg); color:var(--fg);
  font:16px/1.8 "Hiragino Sans","Noto Sans JP","Yu Gothic UI",system-ui,sans-serif; }
main { max-width: 880px; margin: 0 auto; padding: 32px 16px 96px; }
h1 { font-size: 30px; line-height: 1.35; color: var(--accent); }
h2 { font-size: 24px; margin-top: 2.4em; padding-bottom: .3em; border-bottom: 1px solid var(--line); }
h3 { font-size: 19px; margin-top: 1.8em; }
a { color: var(--link); }
code { background: var(--code); padding: .1em .35em; border-radius: 4px; font-size: .9em; }
pre { background: var(--code); padding: 12px 14px; border-radius: 6px; overflow-x: auto; line-height: 1.5; }
pre code { padding: 0; background: none; }
pre.mermaid { background: none; text-align: center; }
table { border-collapse: collapse; display: block; overflow-x: auto; margin: 1em 0; }
th, td { border: 1px solid var(--line); padding: 6px 10px; vertical-align: top; }
th { background: var(--code); }
blockquote { margin: 1em 0; padding: 0 1em; color: var(--muted); border-left: 4px solid var(--line); }
.top { display:flex; justify-content:space-between; gap:12px; font-size:14px; color:var(--muted); }
"""


def main():
    text = list_breaks(SRC.read_text(encoding="utf-8"))
    md = markdown.Markdown(
        extensions=["tables", "fenced_code", "sane_lists", "toc"],
        extension_configs={"toc": {"slugify": github_slug}})
    body = md.convert(text)
    body = re.sub(r'<pre><code class="language-mermaid">(.*?)</code></pre>',
                  lambda m: '<pre class="mermaid">%s</pre>' % m.group(1),
                  body, flags=re.S)
    body = fix_links(body)
    title = re.search(r"<h1[^>]*>(.*?)</h1>", body)
    title = re.sub(r"<[^>]+>", "", title.group(1)) if title else SRC.stem
    page = """<!doctype html>
<html lang="ja">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>%s</title>
<style>%s</style>
</head>
<body>
<main>
<div class="top"><span>Community Over Code Glasgow 2026 &middot; 理解用ガイド</span>
<a href="%s%s">Markdown 版(GitHub)</a></div>
%s
</main>
<script type="module">
import mermaid from "https://cdn.jsdelivr.net/npm/mermaid@11/dist/mermaid.esm.min.mjs";
const dark = matchMedia("(prefers-color-scheme: dark)").matches;
mermaid.initialize({ startOnLoad: true, theme: dark ? "dark" : "default" });
</script>
</body>
</html>
""" % (html.escape(title), CSS, BLOB, REPO_DIR + SRC.name, body)
    OUT.write_text(page, encoding="utf-8", newline="\n")
    print("wrote", OUT.name, "(%d KB)" % (len(page.encode()) // 1024))


if __name__ == "__main__":
    main()
