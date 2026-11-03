---
name: slide-color-palette
description: The user's colour rule for slides and diagrams. Use when making or reviewing any deck, chart, table or diagram (CoC reveal decks, design.html, presentation slides) to choose colours - one base hue in tints plus black/white/grey, and a complementary accent only at the key point.
---

# Slide colour rule

The user's preference, stated 2026-11-03:

1. **Base: one hue, in tints and shades, plus black, white and grey.** Everything that is not special is drawn with this.
2. **Accent: the complementary colour of the base, used sparingly at the important point** (the outcome, the key date, the one thing the audience must see).

In the CoC / OTFSG decks the base is the red-purple of the slide headings (`--wg: #7c297d`, tints `#4f1a50`, `#7c297d`, `#b985ba`, `#e2c8e3`, soft `#f3e8f3`). Its **complement is yellow-green** (`--nx: #5f8f12`, soft `#eef5da`); teal, blue and gold are not complements and must not be used as the accent. Negatives (not adopted, not supported, excluded) are **grey** (`--quiet`, `--line-2`), never red.

## Do

- A sequence of steps or items (chevrons, numbered rows, stairs, stacked bars, categories of one whole) is **one hue**: same colour, or steps of lightness (dark to light, or light to dark for progression).
- Grey carries secondary information (the TAC bar on the timeline, footnotes, labels, table rules).
- The accent appears once or twice per slide at most, and means "look here" (e.g. CFP adopted, the 4/16 key date).
- Use real contrast only when the slide compares two things (adopted vs not adopted, supported vs not supported). Then the accent colour is one side of the contrast, not decoration.
- Prefer a figure over a paragraph: chevron flow, table, timeline, stacked bar, stairs, chips, bullet list, photo grid. Cut the sentence down to what the figure cannot show.

## Meaning of a coloured panel

- A coloured background must always say what it means: put a one-line legend next to it (`黄緑：採択 / 灰：不採択`) and add a symbol to the panel heading (`○`, `✕`), so colour is never the only carrier.
- State the relation: a **contrast** (A vs B, adopted vs not) uses accent + grey with a legend; a **parallel list** uses the same colour for every item.
- Conclusion bars (`.claim`) are in the base hue (purple tint), never red and never the accent.

## Don't

- No red / green / blue / gold side by side when the items are not opposites. Three parallel items are not a traffic light.
- If green (the accent) is used inside a comparison on a slide, do not also put the conclusion bar in green. Conclusion bars (`.claim`) use the base hue.
- No colour per item just to tell items apart. Use labels, position or lightness.
- Do not use the accent for a whole panel, slide background or every heading.

## Implementation notes (otfsg deck)

- Overrides live in the `css_extra` block of the build script, after the Glasgow CSS: `.chev .c1/.c2/.c3` all `--wg`; `.sum-row*` all `--wg` (`!important`); `.pj-bar.c` and `.pj-ev.key` use `--nx`; `.pj-bar.t` is grey; `.yn .pl` uses `--wg`; `.yn .no` and `.panel.neg` are grey. `:root` overrides `--wg/--nx` after the Glasgow CSS.
- Keep `.yn .ok` (accent) / `.yn .no` (gold) only for a real supported/not-supported contrast.
- When adding a slide: pick the figure first, then colour it with the base; add the accent only for the single point the takeaway names.
