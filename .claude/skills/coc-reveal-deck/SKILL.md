---
name: coc-reveal-deck
description: Build, extend or check a slide deck in the Community Over Code (CoC) Glasgow reveal.js look, the way docs/presentation/coc-glasgow-reveal/ does it. Use when asked to make a new talk deck in the CoC style, add or reorder slides of the Glasgow deck, regenerate its index.html or PDFs, check slides for overflow, write speaker notes, or reuse the Glasgow template for another event.
---

# CoC-style reveal.js decks (standalone source -> reveal build)

The Glasgow talk is kept in two editions. Understand this before touching anything.

| Edition | File | Role |
|---|---|---|
| A (source of truth) | `docs/presentation/coc-glasgow-slides.html` | One standalone HTML: 1280x720 canvas, own viewer JS, own CSS, red WireGuard look. Works alone as the fallback. |
| B (generated) | `docs/presentation/coc-glasgow-reveal/index.html` | reveal.js 6 + the official `cocglasgow` theme. **Never hand-edit**; `build.py` overwrites it. |

Edit slide content in A (or in the new deck's `*.src.html`), then rebuild B. The CoC look lives only in `coc-glasgow-reveal/frame-reset.css` and `coc.css`.

## Commands (run in `docs/presentation/coc-glasgow-reveal/`)

```bash
python renumber.py        # after adding/removing/moving slides: "5 / 43" and "PART 2 · 2/4" in A
python build.py           # A -> index.html  (--check fails if index.html is stale)
python check.py           # overflow report for every slide, headless Chrome/Edge (exit 1 on issues)
python check.py --shots DIR   # + one PNG per slide
python make_qr.py [--verify]  # ../assets/slides-qr.svg (pip install qrcode)
python pdf.py [slides|scripts]  # deck PDF + talk-script PDFs
```

Always finish with `renumber.py` -> `build.py` -> `check.py`. Open `index.html` over `file://`; it works offline except the Plex Mono web font. Keys: arrows, `S` presenter view, `F` fullscreen, `Esc`/`O` overview.

## How build.py turns A into B

- **CSS lifting** (`lift_css`): the `<style>` of A is scoped under `.reveal .frame`. Rules for A's own viewer (`body`, `#stage`, `#canvas`, `#bar`, `#help`, `#notes`, `.notes-src`, `.title-slide`, `.slide::after`) are dropped; `.slide`/`.slide.on` become `.reveal .frame`; the `@media` block is removed. `frame-reset.css` undoes reveal.js/theme element styles inside `.frame`; `coc.css` then applies the CoC look (loaded after, so it wins).
- **Slides** are regexed from A as `\n  <section class="..."> ... \n  </section>` (two-space indent, exact). Keep that shape or nothing is found.
- **Title and closing slides are not converted**: a slide with class `title-slide` is replaced by `TITLE`, and one containing `<p class="eyebrow">Thank You</p>` by `CLOSING`; both are hard-coded in `build.py` (dark skyline background, kicker, QR card). Every other slide becomes `<div class="frame ...">` + the CoC footer band (`October 11-14, 2026 | logo | Glasgow, Scotland`, 54 px).
- **Asset paths**: A uses `assets/...`; B rewrites `"assets/` to `"../assets/`.
- **Speaker notes**: A holds them in `<div class="notes-src">`, paragraphs split by `||`. B converts them to `<aside class="notes">`. Prefix a paragraph with `[opt]` (read only if time, shown as "OPTIONAL:") or `[note]` (never read, shown as "NOTE, do not read:").
- **Appendix**: give the section `class="slide appendix"` in A. It goes after Thank You, stacked vertically under it in B (reach it with the down arrow), numbered `A1 / A2`, not counted in `N / TOTAL`.
- **Parts**: a part starts at an agenda slide whose highlighted item is `agenda-item current`; `renumber.py` derives `PART k` and `i/n` from that. No agenda-`current` slide, no part label.

## Making a new deck in the same look (another event or language)

Do not copy `vendor/`. Reuse the Glasgow folder by relative path, as `docs/blog/build_reveal.py` does:

1. Write a standalone source `X.src.html` in the A format (copy the head/CSS/`<script>` from `coc-glasgow-slides.html`; one `<section class="slide">` per slide; a `title-slide` first; `<div class="num">` and `<div class="notes-src">` in each).
2. Write a small `build_reveal.py` that loads the Glasgow `build.py` with `importlib`, sets its module globals `TITLE` and `CLOSING`, calls its `lift_css` and `convert`, fills `template.html`, and rewrites paths (`vendor/`, `frame-reset.css`, `coc.css`, `img/`, `../assets/`) to point at the Glasgow folder. Add a tiny extra CSS file after `coc.css` for deck-specific tweaks (see `docs/blog/otfsg-tac-reveal.css`).
3. Page titles use `<p class="eyebrow">` (the CoC purple Montserrat heading); only an Agenda slide uses `<h2>`. A bare `<h2>` title in the source comes out as an ink-coloured heading with a gradient rule, which is not the Glasgow look.

## Design conventions that were settled (keep them)

- One purple title per slide, one thin rule, no decorative red: **red/green/blue/gold only where they carry meaning in a figure** (WireGuard red, NuttX teal). `coc.css` swaps decorative reds for the CoC purple/orange.
- Every content slide ends with a **Takeaway** bar (one sentence); concise titles; one idea per slide.
- Every slide shows where it is: `N / TOTAL`, and the part and place in it. Agenda subtexts preview each part. A progress stepper (`1 CoC ... 6 学び`) above the footer band also works for a short talk.
- Title slide says what, why and who; the closing slide says Thank You, with a QR to the slides.
- Notes are written to be **read aloud**: short sentences, abbreviations spelled out at first use, no slogans. The English script is the master and the Japanese script follows it (`talkscript/`).
- Claims on slides are fact-checked against the code and evidence files (`docs/upstream/evidence/`); `review-coc-glasgow.md` is the running record (append, do not rewrite).
- Do not call the project a GSoC project in anything user-facing (the Glasgow talk avoids it; the personal-trip blog and the TAC sharing deck are the exception because the GSoC story is the topic).

## Naming slides and parts (the title rules of the Glasgow deck)

The titles were reworked over many commits ("use concise slide titles", "simplify slide headings and takeaways", "retitle slide 7", "tell the bugs as where it was hard, not as a slogan", "agenda subtexts preview each part"). The settled rules. **The one-line test first**: a title is a *label*, not a sentence; it is concrete, it tells what the slide shows, and it stops there. Too little ("Overview", "これから") and the viewer must guess; too much (a full sentence, a claim, a reason) and the title does the Takeaway's job. Prefer the thing on the slide (a number, a name, a count) to an abstraction: `渡航にかかる費用`, not `費用をどう工面するか`; `審査の3つの観点`, not `何が審査されるか`.

1. **Title = topic, Takeaway = claim.** A title names what the slide is about; the sentence you want remembered goes in the Takeaway bar at the foot. Do not put the conclusion in the title and do not repeat it in both. Separate context slides from conclusion slides.
2. **`Role: specific content`** is the default shape. The part before the colon says what job the slide does in the story, the part after says what it is about:
   `Problem: How Can We Securely Access Remote Devices?` / `Options: Ways to Reach a Remote Device` / `Solution: A VPN on the Device` / `Architecture: where wg0 sits` / `Porting Strategy: Start from wireguard-lwip` / `Version 1: WireGuard as a NuttX application` / `Version 2: WireGuard as a NuttX kernel driver` / `Live Demo: Stack-chan over WireGuard` / `Testing: What Ran Where` / `Design Choice: Trust the Clock?`.
   Typical roles: Problem, Options, Solution, Architecture, Strategy, Version n, Demo, Testing, Design Choice, Bug n.
3. **Follow one narrative order**: Problem -> Options -> Solution -> Development Plan, then versions, then demo, then summary. If a slide does not fit a role, ask whether it belongs.
4. **Problems as questions, answers as plain statements**: `How Can We Securely Access Remote Devices?` then `A VPN on the Device`. A short question also suits an appendix decision (`Trust the Clock?`).
5. **Tell it as a story, not a slogan.** `Where it was hard: six bugs` replaced a slogan slide; the Summary's first lesson became "test beyond the handshake" instead of "simple tests pass". If a title sounds like a poster, rewrite it as what happened.
6. **A bug's title is its symptom**, in the reader's words: `Bug ②: Ping Works. TCP Dies.` (class `eyebrow bug`, orange-red). Number it with a circled digit; the fix goes inside the slide (SYMPTOM / ROOT CAUSE & FIX).
7. **Short and plain**: title case in English, no verbs of marketing, no full sentence. Keep to one line at 38 px. Possessive and first person are fine for the personal parts: `My Experience with NuttX`, `How it started`, `What I learned`, `Sony's history with NuttX`.
8. **Repeated slides repeat their title.** Every agenda slide is just `Agenda`; the part number and place in the part live in `.num`, not in the title.
9. **Part titles are short nouns with a keyword line** (the agenda subtext previews what the part will show): `Overview & background`, `The Problem & Solution`, `The Development Plan`, `Version 1: Application-Based Implementation`, `Version 2: The Kernel Driver`, `Live Demo`, `Summary & Reflections`. Change the subtext, not the title, when the content changes.
10. **The opening trio** is fixed: title slide says what, why and who; About Me comes before the agenda; the closing slide says `Thank You` (no "Questions?").

Japanese decks: keep the same shape with a full-width colon, `ラベル：内容` (`課題：渡航費用を、どう工面するか`, `採択後：手続きの流れ`), and keep each title to one line (about 28 characters at 36 px). Part names stay plain nouns (`渡航支援制度（TAC）`); slide titles carry the role label. Check by reading only the titles in order: they should tell the story by themselves.

## Component catalog (copy the markup from coc-glasgow-slides.html; CSS is already there)

Every content slide is: a title, a `.body`, a `.num`, a `.notes-src`. Inside `.body` pick from these. Colours mean something: **red = the problem / before, teal = the fix / after, blue = neutral info, gold = caution or "needs work", grey = quiet**.

### Numbering: where am I?
- `<div class="num"><span class="part">PART 4 &middot; 3/5</span>21 / 36</div>` in every slide. Bottom-right in A, top-right in B. Do not type it by hand; `renumber.py` rewrites it. The agenda slide that opens a part shows `PART k` only; an appendix slide shows `APPENDIX A1 / A2`.
- Circled digits for sequence or ranking: use character references `&#9312;` `&#9313;` `&#9314;` (①②③) in a title (`Bug &#9313;: Ping Works. TCP Dies.`) and in SVG labels. In an SVG map the same numbers sit in red circles (`<circle r="22" fill="#8d1f24" stroke="#fff" stroke-width="5">` + white bold digit). For a numbered list of steps use `<span class="badge">1</span>` (40 px red circle) in the first column of `table.steps`; for summary rows use `.sum-row` + `.sum-n` (54 px circle, colour per row: nx / `b` blue / `c` gold); the agenda uses `.agenda-num` (28 px pink circle, solid when `current`).

### Agenda = table of contents
- `.agenda` of `.agenda-item` rows: `.agenda-num`, `<h3>` title + `<p>` one-line subtext that previews the part, `.agenda-time` (`~5 MIN`). Put it twice: once whole (slide 3), and once per part with `agenda-item current` on the active row and `dim` (opacity .38) on the others, as the divider that opens that part. `renumber.py` reads `current` to number the parts.
- **The part divider is the table of contents shown again, with the part that starts next in bold.** Insert it as the first slide of every part, including part 1. On it the active row is `agenda-item current` (accent number, accent h3, ink subtext, accent time) and every other row is `agenda-item dim`. Same title (`Agenda`), same rows, same order; only the highlight moves. Its `.num` shows `PART k` only (no `i/n`), and the content slides after it count `1/n ... n/n`. Add a bold-emphasis rule so the current row stands out beyond colour: `.agenda-item.current h3 { font-weight: 900; font-size: 22px; }`. Give it a one-line speaker note that names the next part ("次は、現地での8日間の様子です。").
- Generate dividers rather than copying them by hand: keep one `agenda_items` list, and in the assembly loop emit `divider(k)` whenever a slide's `PART k` appears for the first time (see the scratchpad-style `build.py` in `docs/blog/otfsg-tac-slides.src.html`'s history, commit "part dividers"). Editing an agenda row then updates all dividers.
- The footer progress stepper (`.stepper`) can sit alongside the dividers; it is not a substitute for them.
- A deck that lists several talks, papers or items from a source: one item per slide (`聴講：<name>`, label `TALK k / n`), each with its own link. Never invent a direct video URL; link to a verified page (the channel search URL `https://www.youtube.com/@<channel>/search?query=...` is a safe fallback) and note it to swap in the real one.

### Conclusion at the foot of the slide
- `<div class="takeaway"><b>Takeaway</b><p>One sentence.</p></div>` last in `.body`. The `<b>` is hidden by CSS; it is only a marker. Two lines: `<p><span class="l">...</span><span class="l">...</span></p>`. Green bar with a CoC gradient rule on top in B.
- Stronger claims: `.claim` (red) / `.claim.ok` (green) with `<p>` bold + `<small>` support.

### Small titles on blocks (the mono label that names a block)
Use a short ALL-CAPS label, never a sentence. Four kinds:
- **Pill on a panel**: `<span class="plabel">SYMPTOM</span>` inside `.panel.bugl` (orange-red pill) and `<span class="plabel">ROOT CAUSE &amp; FIX</span>` inside `.panel.nx.fixr` (teal pill). The same pair carries `QUICK FIX` / `REAL FIX`, `BEFORE &mdash; VERSION 1` (grey pill, plain `.panel`) / `AFTER &mdash; VERSION 2` (teal pill, `.panel.nx`) under `.body.ba`, and per-topic labels like `CRYPTOGRAPHY &mdash; ChaCha20-Poly1305`. Always two panels in `.two`, problem left, answer right.
- **GOAL box**: `<div class="goal"><p class="tag">GOAL</p><p class="l1">...</p><p class="l2">...</p></div>` grey box, teal left bar.
- **Section label above a figure or table**: `<p class="dm-label">NETWORK SETUP</p>`, `LIVE DEMO STEPS`, `THREE TERMINALS`. Grey mono, letter-spaced. Same idea: `.mtitle` inside `.modes` (`BUILD MODES`), `.why-label`, `.about-label`.
- **Card heading inside `.fact`**: `<div class="fact nx"><b>Heading</b><p>...</p></div>` in a `.three` (or `.four`) grid. The `.fact` colour class (`wg` red, `nx` teal, `blue`, `gold`) sets the 4 px top bar. The `<b>` is a mono caps label (11 px); in `.why`, `.talks`, `.res` it is turned into a normal bold heading. A circled digit in that `<b>` is unreadable at 11 px: restyle the `<b>` (about 20 px, `text-transform:none`, `font-family:inherit`), as the TAC review slide of `docs/blog/otfsg-tac-slides.src.html` does.

### Other recurring parts
- `.repo`: a link strip with a `tag`, the repo URL and a right-aligned description. `.sources`: tiny grey source line pinned above the footer (check.py keeps content above it).
- `.kar.keep/.adapt/.replace`: Keep / Adapt / Replace rows, big verb on the left. `.vs .box.q` / `.box.r`: question vs answer with a `.meas` figure. `table.options`: comparison table, the chosen row has `class="this"`. `ul.big` / `ul.pts`: bullet lists. `.chips` / `.tags`: pills. `.ptl`: project timeline; `.timeline-v` (`.tlv-item`): vertical dated list; `.timeline-h`: horizontal five-step.
- Architecture figures are inline SVG (`class="route"`, `viewBox`), fonts via `svg text` CSS, labels as `class="label"` / `class="small"`. Give each SVG a descriptive `aria-label`.
- Dark title and closing: only through `TITLE` / `CLOSING` in `build.py`. Put a QR card (`.qr-card`) on both, linking to the slides.

### Video in a slide
- **YouTube, click to play** (`assets/youtube-embed.js`): `<figure class="still yt" data-yt="VIDEO_ID"><button class="yt-play" type="button" aria-label="..."><img src="assets/thumb.jpg" alt="..."><span class="yt-btn" aria-hidden="true">&#9654;</span></button></figure>`. Only the thumbnail loads until clicked; the player is removed when the slide is left (sound stops); keys stay in the player until the viewer clicks outside. From `file://` YouTube refuses to embed (error 153), so the click opens youtube.com in a new tab: always add a plain link too. Works in A and B (it looks for `.on` and `.present`). Both scripts are loaded at the end of the page (`../assets/` in B).
- **Local backup video** (`assets/backup-video.js`): a hidden `<div class="bv" hidden>` with `.bv-open` button, `.bv-overlay` (full-slide `<video controls>` + `.bv-close`). The script reveals the button only if `assets/demo/backup-demo.mp4` really loads, so the slide has no dead button before the file exists. Close with the button or Esc; arrows and Space stay in the player.
- Make thumbnails with a real frame of the video (`assets/demo-thumb*.jpg`), and describe the frame in `alt`. Keep videos out of the PDF: they are covered by the thumbnail.

## Layout limits (what check.py enforces)

Frame 1280x720; content padding 40/72/80 px; the CoC footer band is the bottom 54 px (the dark title/closing frames have none). check.py flags an element that leaves the frame, runs under the footer or over a `.sources` line, a covered link, a link without `target="_blank" rel="noopener noreferrer"`, and a `.body` that overflows. It measures after web fonts load.

## Gotchas learned the hard way

- **`?print-pdf` is blank in headless Chrome** (for this deck and for the upstream template). `pdf.py` instead screenshots each slide at 2x (2560x1440) and prints one image per 1280x720 page. Speaker notes are not in that PDF; the script PDFs cover them.
- `Montserrat Bold` has no Japanese glyphs; CJK falls back to the system font. Set the Japanese title size by hand (about 36 px for headings, 52 px on the dark title) and drop `white-space: nowrap` on `h1` (coc.css sets it for the English title).
- `.num` is moved to the top-right in B; anything pinned to the bottom must sit above the footer band (`bottom: 62px`), not at the slide edge.
- Do not patch Python build scripts through a shell heredoc with Windows paths and `\U`/`\u` sequences; write the patch to a file and run it.
- Check a changed deck by rendering, not only by reading HTML: `chrome --headless --window-size=1280,720 --virtual-time-budget=4000 --screenshot=x.png "file:///.../index.html#/N"` (reveal slide indices are 0-based in the hash).
- Keep `__pycache__` out of git; the repo already ignores it.

## Publishing

The decks are served from GitHub Pages at `https://wwlapaki310.github.io/gsoc2026-nuttx-wireguard/docs/presentation/coc-glasgow-reveal/index.html`. The QR code encodes that address; if it ever changes, update `URL` in both `build.py` and `make_qr.py`, then re-run `make_qr.py` and `build.py`. Commit the generated `index.html` together with its source, and regenerate the PDFs with `pdf.py` after content edits (CI also regenerates them).
