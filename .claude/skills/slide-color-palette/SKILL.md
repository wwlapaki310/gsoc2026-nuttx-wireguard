---
name: slide-color-palette
description: The user's colour rule for slides and diagrams. Use when making or reviewing any deck, chart, table or diagram (CoC reveal decks, design.html, presentation slides) to choose colours - one base hue in tints plus black/white/grey, and a complementary accent only at the key point.
---

# Slide colour rule

The user's preference, stated 2026-11-03:

1. **Base: one hue, in tints and shades, plus black, white and grey.** Everything that is not special is drawn with this.
2. **Accent: the complementary colour of the base, used sparingly at the important point** (the outcome, the key date, the one thing the audience must see).

In the CoC / OTFSG decks the base is the Glasgow maroon (`--wg`, with tints such as `#6e1f27`, `#9a3a43`, `#c58a90`, `#e0bcc0`, `--wg-soft`) and the accent is teal (`--nx`, `--nx-soft`).

## Do

- A sequence of steps or items (chevrons, numbered rows, stairs, stacked bars, categories of one whole) is **one hue**: same colour, or steps of lightness (dark to light, or light to dark for progression).
- Grey carries secondary information (the TAC bar on the timeline, footnotes, labels, table rules).
- The accent appears once or twice per slide at most, and means "look here" (e.g. CFP adopted, the 4/16 key date).
- Use real contrast only when the slide compares two things (adopted vs not adopted, supported vs not supported). Then the accent colour is one side of the contrast, not decoration.
- Prefer a figure over a paragraph: chevron flow, table, timeline, stacked bar, stairs, chips, bullet list, photo grid. Cut the sentence down to what the figure cannot show.

## Don't

- No red / green / blue / gold side by side when the items are not opposites. Three parallel items are not a traffic light.
- If green (the accent) is used inside a comparison on a slide, do not also put the conclusion bar in green. Conclusion bars (`.claim`) use the base hue.
- No colour per item just to tell items apart. Use labels, position or lightness.
- Do not use the accent for a whole panel, slide background or every heading.

## Implementation notes (otfsg deck)

- Overrides live in the `css_extra` block of the build script, after the Glasgow CSS: `.chev .c1/.c2/.c3` all `--wg`; `.sum-row*` all `--wg` (`!important`); `.pj-bar.c` and `.pj-ev.key` use `--nx`; `.pj-bar.t` is grey; `.yn .pl` uses `--wg`.
- Keep `.yn .ok` (accent) / `.yn .no` (gold) only for a real supported/not-supported contrast.
- When adding a slide: pick the figure first, then colour it with the base; add the accent only for the single point the takeaway names.
