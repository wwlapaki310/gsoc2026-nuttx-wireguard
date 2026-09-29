# Review — CoC Glasgow deck, design, and script

Reviewed 2026-09-29. Target: [`coc-glasgow-slides.html`](coc-glasgow-slides.html)
(26 slides), its embedded speaker notes, [`coc-glasgow-script-en.md`](talkscript/coc-glasgow-script-en.md) /
[`coc-glasgow-script-ja.md`](talkscript/coc-glasgow-script-ja.md), and
[`coc-glasgow-qa.md`](talkscript/coc-glasgow-qa.md). Three reviewer personas —
a general software engineer, an ASF community member, and a NuttX
maintainer/contributor — plus a fact-check of slide claims against the actual
driver source and the evidence files in `docs/upstream/evidence/`. This file
is a running record; new passes append rather than rewrite.

**Method note on the personas:** these are not live external reviewers — they
are this session working from the actual code, the actual ASF/NuttX public
norms, and the actual evidence docs, structured as three angles of attack
rather than one generic pass. Findings are graded by how they were reached:
"checked against X" (verifiable), vs "a person like this would likely ask/say"
(judgment call). Both are marked as such below.

---

## Fact-check: claims vs. implementation

Spot-checked the highest-stakes numeric and architectural claims against the
driver source (`drivers/net/wireguard/`, `include/nuttx/net/wireguard.h`,
`include/nuttx/net/ioctl.h`) and the evidence files.

| Claim (slide) | Checked against | Result |
|---|---|---|
| `wg0` is a `NET_LL_TUN` netdev (slide 2, 7) | `drivers/net/wireguard/wireguard.c:1943`, `netdev_lower_register(dev, NET_LL_TUN)` | **Matches** |
| ioctl ABI: `SIOCS/GWGIF`, `SIOCS/D/GWGPEER` (slide 16) | `include/nuttx/net/ioctl.h:166-170` | **Matches exactly**, including the "Get" naming |
| Private key write-only, "get ioctl never returns it" (slide 16) | `include/nuttx/net/wireguard.h:91-93` | **Matches**, and the code comment is correctly scoped to "this ioctl doesn't return it," not the broader "never leaves the kernel" claim the outline had already flagged as wrong in an earlier pass — the current wording does not repeat that mistake |
| PROTECTED split 80% kernel / 8% user of 256 KB, stock layout (slide 18) | `evidence/protected-runtime-2026-09-28.md:34-44` | **Matches exactly** (209 832 B / 256 KB = 82%, rounds to the stated 80%; 21 576 B / 256 KB = 8.4%, rounds to 8%) |
| SPRESENSE `wg_rx` high-water 1472 B of 6096 (slide 18, 25) | `evidence/spresense-resources-2026-09-28.md:57,61` | **Matches exactly** |
| sim `wg_rx` high-water 2536 B, 151 rekeys / 620 s / 100 down-up (slide 19) | `evidence/sim-soak-2026-09-28.md:27-61` | **Matches exactly** |
| #14 timestamp: unset clock → no handshake in 75 s; set → 4.1 s (slide 20) | `evidence/spresense-timestamp-2026-09-28.md` (per prior session's read) | **Matches** the recorded pair |
| ChaCha20-Poly1305 nonce counter placement bug (slide 17) | `nuttx` fork commit `a2dd121201`: "Place the 64-bit counter nonce in the last eight bytes of the IETF nonce" | **Matches** the slide's description |
| Vendored X25519 is MIT, from STROBE, not separately listed in the root `LICENSE` | `apps/system/wg/wg_x25519.c` SPDX header vs. `nuttx-apps/LICENSE` | **Correct as-is** — per-file SPDX headers without a root-`LICENSE` entry is the existing convention across `nuttx-apps` (checked against `examples/chat/chat_main.c`, `examples/json/json_main.c`, and others already using the same pattern), so this is not a gap |
| "GSoC" does not appear anywhere in the deck, outline, or scripts | grepped `coc-glasgow-*.{html,md}` and `talkscript/coc-glasgow-*.md` | **Clean** — the only occurrence is the *instruction* "do not frame this as a GSoC project" in the outline's own guidance, never in user-facing text |
| The outline's four standing corrections (detached-thread = my mistake; key write-only ≠ never-leaves-kernel; apps v0.1.1 vs. kernel driver kept distinct; only the nonce bug is pre-existing NuttX, not the stack overflow) | Slides 11, 16, 18, 17/25 | **All four still honored** in the current 26-slide deck |

No implementation-vs-slide mismatch was found in this pass. The one thing worth
independent reverification before the talk, because it wasn't re-checked here
and is stated confidently on slide 8: the "3,079 lines byte-identical to
upstream" protocol/crypto figure. It is consistent with numbers used
elsewhere in the project's history (`docs/releases/v0.1.0.md` and others), so
it is very likely still correct, but it was not recomputed against the current
tree in this pass.

---

## Persona 1 — General software engineer (no NuttX or ASF background)

**What lands well:**
- The framing device — "shallow tests pass, deep tests fail," stated on
  slide 9 and paid off seven times — gives an engineer without embedded
  background a reason to keep watching even through NuttX-specific detail.
  It is a real, portable lesson, not project trivia.
- Pitfall 3 (slide 12, "ping works, TCP dies") is the strongest slide in the
  deck for this audience: a concrete, debuggable symptom, a specific wrong
  errno, and a root cause (fd scoping) that generalizes past NuttX. This is
  the slide most likely to be quoted afterward.
- The "working tunnel is not evidence" slide (19) is unusually good judgment
  for a conference talk: most talks stop at "it works," and this one
  explicitly explains why that is not enough, with three concrete failure
  modes a functional test cannot see. A general SW engineer will recognize
  this pattern from their own domain (staging environments that "pass" while
  hiding a resource leak, etc.) even without knowing WireGuard.

**Friction points (judgment call):**
- Slides 1–8 (title through "how it was built") front-load ASF/NuttX context
  before the first payoff (slide 9). For an audience that is not
  NuttX-specific, that is roughly 4.3 of 24.5 minutes before the talk's
  actual thesis appears. The script already marks slide 3 as a cut candidate
  for time; a general engineer's honest reaction is that it could be trimmed
  even without a time crunch, not just as a fallback.
- The volume of acronyms and short-lived NuttX terms in a single slide (netif
  / pbuf / iob / `devif_poll` / netdev_lowerhalf, all inside slides 6–7 and
  15–16) will lose a non-embedded audience member for a few seconds each
  time, right where the deck needs them tracking the fd/thread reasoning in
  slides 9–14. None of this is wrong, it's dense.
- Slide 20 (the timestamp design call) is the longest and most rewarding
  slide for someone who already cares about distributed systems correctness,
  but it is also the single densest block of reasoning in the deck (4 options
  compared, a durability argument, an RTC-seeding detail). A general engineer
  will follow it, but it is the one place in the talk where losing the thread
  for 10 seconds loses the conclusion. The script's own advice — do not
  compress or split this slide — is right, but consider one visual
  simplification: the four-option table could drop the "Let it catch up" row
  in the table itself and mention it only in speech, since it is dismissed in
  one sentence and its table row currently draws as much visual weight as
  the adopted option.

**Verdict:** strong for this audience once past the setup; the identified
friction is about density and front-loading, not correctness or credibility.

---

## Persona 2 — ASF community member (not a NuttX contributor; knows Apache Way, dev@ norms, and other ASF projects' talks)

**What lands well:**
- Slide 3 does the thing ASF audiences actually check for early in a CoC
  talk: it states plainly that the goal is code merged and maintained by the
  community, not a personal demo, and it uses "community over code" as an
  actual claim rather than a slogan reused from the conference banner. That
  reads as earned, not decorative, given slide 24 backs it up with a real
  staged-PR plan.
- The contribution staging on slide 24 (small `crypto:` PR first, because it
  "stands on its own and helps everyone," then the driver, then the apps
  command) matches how ASF/NuttX reviewers actually prefer incoming series —
  independently mergeable units, smallest and least controversial first. An
  ASF-experienced viewer will recognize this as someone who has watched how
  PRs succeed or stall in this kind of project, not someone submitting a
  single enormous PR.
- The vendored MIT code (X25519) is handled the way ASF policy expects:
  correct SPDX header, MIT is Category A / always-allowed for inclusion, and
  it is not listed in the project's top-level `LICENSE` — which the fact-check
  above confirms matches existing practice elsewhere in `nuttx-apps`, so the
  deck is not introducing a new licensing question, it is following the
  established one.

**Friction points:**
- **(Judgment call.)** Nothing on any slide states the current *submission*
  status (rebased, three PRs prepared, signed, published to the presenter's
  own fork, not yet opened against `apache/*`). Slide 24 describes the
  intended shape of the PR series in the future tense ("A small `crypto:` PR
  first...") without saying where things actually stand as of the talk date.
  An ASF audience member's first question after this slide, based on how
  these talks usually go, is "so is it merged, and if not, why isn't it a PR
  yet" — and the deck has no slide-level answer; it currently lives only in
  `coc-glasgow-qa.md`. Given the project is honest almost everywhere else
  about what is and is not done, the one place this is true but *silent* is
  slide 24. This does not need a new slide — one line under the existing
  bullets (something like "status: rebased, three signed candidate PRs
  prepared, not yet opened") would close it and is consistent with the rest
  of the deck's voice.
- **(Judgment call.)** The talk never says explicitly that this stalled as a
  GSoC proposal and continued anyway. That is the right call per the
  project's own standing instruction not to frame it as a GSoC project, and
  the fact-check above confirms the word does not leak into any user-facing
  text — but an ASF community member who already knows this history (the
  Community Over Code / NuttX Workshop crowd plausibly does) may ask about it
  live. The Q&A doc does not currently have a prepared answer for "wasn't
  this a GSoC project?" — worth a one-line addition there, not on a slide.
- **(Judgment call, minor.)** Slide 3's "headed for `apache/nuttx` and
  `apache/nuttx-apps`" claim is accurate as of the talk, but pairs awkwardly
  with the silent PR status above: a careful listener holds slide 3's "headed
  for" against slide 24's staged plan and slide 26's Q&A, and notices the
  actual current position is never stated on-slide between "headed for" (3)
  and "here is the plan" (24). Same fix as above resolves both.

**Verdict:** the ASF-facing framing and process description are accurate and
match how this community actually reviews contributions; the one real gap is
that "status as of today" is answered in the Q&A doc but not on any slide,
for the single audience most likely to ask.

---

## Persona 3 — NuttX maintainer / active contributor

**What lands well:**
- The privilege-model table on slide 18 is exactly the shape a NuttX
  maintainer would want: FLAT is explicitly *not* claimed as sufficient
  anymore (it is relegated to the bottom row as the older apps version), and
  KERNEL and PROTECTED are each given their own row with what was actually
  exercised, not just "it builds." A NuttX reviewer's first instinct on any
  new driver PR is "does this even build under PROTECTED," and the deck
  answers that before being asked.
- The SMP claim on slide 18 is carefully hedged in exactly the way a NuttX
  concurrency reviewer would insist on: "one scripted interleaving on an
  emulator is not a race search... the claim is no longer only on paper." A
  maintainer reading a driver PR's cover letter that claimed unqualified
  "SMP-safe" would push back immediately; this phrasing pre-empts that.
- Slide 17's framing of the ChaCha20-Poly1305 nonce bug — found via this
  port, going upstream as its own `crypto:` PR, ahead of and independent from
  the driver PR — is the right way to present a bug found in someone else's
  subsystem to that subsystem's own community: it is offered as a
  contribution back, staged first, not folded into the driver PR where a
  reviewer would have to review crypto changes to approve a netdev.
- The ioctl ABI being described as "flat, pointer-free... because in
  PROTECTED/KERNEL the kernel copies the caller's struct directly" (slide 16)
  is precisely the property a NuttX driver reviewer checks first for any
  ioctl-based ABI crossing the user/kernel boundary, and the deck states the
  *reason*, not just the fact.

**Friction points:**
- **(Checked.)** The `mm/iob/Kconfig` default change mentioned in
  `handoff.md`'s Task 2 (the IOB pool default) is not mentioned anywhere in
  the deck. A NuttX maintainer reviewing this driver will notice a shared
  Kconfig default changing as a side effect of one driver's PR and will ask
  about it regardless of whether the talk mentions it — but since the talk
  already discusses IOB behavior at length (slide 18's soak, the traps list),
  a maintainer in the room may reasonably expect the shared-file touch to be
  acknowledged on slide 18 or 24, not left for the PR description alone.
  This is a minor omission, not an error.
- **(Judgment call.)** Slide 16 says "the only vendored code is a small MIT
  X25519 in the user-space command, for offline key generation" — accurate,
  but a NuttX maintainer's next question is usually "why not use NuttX's own
  Curve25519 from `crypto/` for that instead, so there is no vendored code at
  all." The deck doesn't pre-empt this, even though the project has already
  worked out the answer (A5 in `open-questions.md`: cryptodev's curve25519
  path exists but drags in `CRYPTO_SW_AES` as a dependency price). This
  answer already exists almost verbatim in `coc-glasgow-qa.md`, so the gap is
  the same shape as the ASF persona's finding: the deck is silent where the
  prepared material already has the answer. Given how central "why is this
  vendored" is to a NuttX code reviewer's first pass on any driver, this is
  the finding most worth promoting from Q&A onto the slide itself, e.g. as a
  half-line addition to slide 16's existing bullet.
- **(Judgment call, minor.)** The deck states NuttX 13.0.1 / master / 12.7.0
  as "one source" (title slide, slide 1) for the *whole* project, but per the
  verification matrix and evidence files, the multi-version claim was
  verified for the **apps v0.1.1** implementation; the kernel driver's
  cross-version status is not restated anywhere in the deck. A NuttX
  maintainer who knows the codebase moves fast (as flagged in Task 1 of
  `handoff.md` — 267 commits behind at last check) will wonder whether the
  kernel driver was actually built against all three, or only against
  current `upstream/master` post-rebase. Worth either confirming and stating
  it, or scoping the claim to "the apps implementation" explicitly the way
  slide 22 (portability) already correctly does.

**Verdict:** technically credible to this audience and pre-empts the
concurrency and privilege-model objections a NuttX reviewer would raise
first; the two things a maintainer is most likely to ask live (why vendor
X25519 instead of NuttX's own curve25519; does the multi-version claim cover
the kernel driver) already have answers written down, just not surfaced on
the relevant slide.

---

## Design review

- **Visual system is coherent and disciplined.** Consistent color coding
  (maroon for the "problem"/WireGuard-specific panels, teal for the
  NuttX-native resolution, blue/gold used sparingly) makes the two-panel
  "before/after" and "problem/fix" slides scannable without reading every
  word — a real aid at talk speed, not just decoration.
- **All referenced assets exist** (`assets/*.png`, `*.svg`, `*.jpg` for the
  title slide and demo slide) — checked directly; no broken images.
- **Fonts are loaded from Google Fonts over the network** (`fonts.googleapis.com`
  / `fonts.gstatic.com`, no local/bundled fallback beyond the generic
  `system-ui` stack in `--sans`/`--mono`). Conference venue Wi-Fi is a known
  failure point; if the font request fails, the deck still renders (system
  fallback fonts are declared), just not in the intended typeface. Worth a
  local copy or a pre-flight check on the venue network before the talk,
  since this is cheap to fix and mildly annoying to discover live.
- **No dark-mode handling and no responsive layout** — both are appropriate
  choices here, not omissions: this is a fixed 1280×720 presentation canvas
  opened directly on a presenter's machine and scaled as a whole (`fit()` in
  the script), not a page meant to adapt to arbitrary viewers or the OS
  theme.
- **Contrast** on body/panel text (`--muted #5c6c78` on `--surface #fbfcfd`)
  and on the colored eyebrow/highlight text (`--wg #8d1f24`, `--nx #176c67`,
  both against the light backgrounds used) reads comfortably at normal
  monitor distance; no low-contrast combinations were found by inspection.
  Not independently measured with a contrast-ratio tool.
- **Slide numbering is internally consistent** ("`N / 26`" on every slide,
  including the title slide correctly counted as 1/26) — this was rechecked
  after an earlier miscount in this session's own tooling (a regex missed
  the title slide's extra `title-slide` class), not a deck defect.
- **One small inconsistency:** the "seven places it has to hold" title
  (slide 18) and the eyebrow "Verification" are shared with slide 19, which
  is about what verification *doesn't* show — the two slides read as a pair
  in the notes and in this review, but nothing in the on-slide title
  signals that slide 19 is the deliberate rebuttal of slide 18 rather than
  a second independent verification slide. A subtitle change (e.g., keeping
  "Verification" but changing slide 19's eyebrow to something like "Verification
  · the limits") would make the pairing visible without reading the speaker notes.

---

## Script and Q&A review

- **Timing math re-verified independently** in this session (word count per
  slide's notes at 140 wpm): total ≈24.5 minutes across 26 slides, matching
  the script file's stated total and the outline's 20–30 minute target.
- **The "locked block" guidance (slides 18–20, ~10.7 of 24.5 minutes) is
  correct and important** — these are also the slides carrying every claim
  fact-checked above; cutting into them under time pressure would cut the
  part of the talk that is actually being defended, not just illustrated.
- **JA/EN correspondence**: spot-checked several sections (title slide,
  pitfall 3, slide 18, slide 20) for meaning-level fidelity rather than
  literal translation — the JA script preserves the causal chain and the
  actual numbers (75 s / 4.1 s, 1472/6096, 80%/8%) exactly rather than
  rounding or paraphrasing them, which matters if the JA version is ever
  used to answer a number-specific question rather than only for rehearsal.
- **Q&A gap found in this pass, not previously recorded:** neither
  `coc-glasgow-qa.md` nor any slide has a prepared answer for "wasn't this
  originally a GSoC proposal?" — a plausible question specifically from the
  ASF/NuttX-workshop audience this talk is aimed at, given the project's own
  git history and public record predate the pivot away from that framing.
  The existing standing instruction is *not to frame the talk that way*,
  which the deck correctly follows — but not framing it that way and not
  having an answer ready if asked directly are different things.

---

## Action items (priority order)

1. **Add a one-line "status as of today" to slide 24** (rebased, three signed
   candidate PRs prepared and published to the presenter's own fork, not yet
   opened against `apache/*`) — closes the ASF persona's main finding and the
   ambiguity between slides 3 and 24.
2. **Add a half-line to slide 16** on why X25519 is vendored instead of using
   NuttX's own curve25519 (the `CRYPTO_SW_AES` dependency-price argument from
   A5) — closes the NuttX persona's most likely first question, using
   material that already exists in `coc-glasgow-qa.md`.
3. **Add one Q&A entry** for "wasn't this a GSoC project?" to
   `coc-glasgow-qa.md`, consistent with the standing instruction not to frame
   it that way on slides.
4. **Scope or confirm the multi-version claim** ("NuttX 13.0.1 / master /
   12.7.0, one source") on slide 1/title for the kernel driver specifically,
   not only the apps implementation — either state it covers both, or narrow
   the title-slide claim the way slide 22 already correctly does.
5. **(Optional, cheap)** vendor the two Google Fonts locally or verify venue
   network access ahead of time.
6. **(Optional)** differentiate slide 19's eyebrow from slide 18's so the
   "verification vs. its limits" pairing is visible without the speaker
   notes.

None of these are correctness problems — the fact-check pass found no
mismatch between what the deck claims and what the implementation and
evidence files actually show. They are about surfacing answers that already
exist in supporting documents onto the one slide or Q&A entry where the
most likely audience for each question will actually be looking.

---

## Render and scope pass — 2026-09-29

This pass rendered all 26 slides at 1920x1080 in Chromium and inspected each
frame, then compared the wording again with `verification-matrix.md`,
`remaining-work.md`, the published candidate-branch handoff, and both scripts.
It supersedes the earlier statement above that there was no slide-level
accuracy problem.

### Findings and disposition

| Priority | Finding | Audience most affected | Disposition |
|---|---|---|---|
| P0 | Slide 18's seven-row verification table extended below the 720 px canvas; the last row was visibly clipped | all | **Fixed and re-rendered:** all seven rows, conclusion, scope footer and slide number fit |
| P0 | Slide 24 said the design had been shared on `dev@`; it has been prepared but not posted | ASF | **Fixed:** now says prepared, and explicitly states fork-published / upstream-not-open status |
| P1 | The title's three-version claim visually read as applying to the kernel driver, although the matrix supports it for apps v0.1.1 | NuttX | **Fixed:** title and scripts now scope the claim to apps v0.1.1 |
| P1 | Slide 18 did not disclose that hardware runs predate the final upstream rebase, while KERNEL/SMP were rerun on the rebased candidate | NuttX | **Fixed:** evidence-scope footer added |
| P1 | Slide 19 generalized one selectable xorshift128 backend as “NuttX's default `/dev/urandom`” and three different keys could sound like entropy certification | security-aware SW / NuttX | **Fixed:** backend and test scope narrowed; explicitly a sanity/regression check |
| P1 | The demo had only 35 seconds and a half-width thumbnail, despite being the emotional payoff | general SW | **Fixed:** 90-second runbook, visible counter change, larger image |
| P2 | “Production Readiness” was stronger than the honest remainder (#14, SmartFS power-cut, AP-loss, day-scale testing) supports | ASF / NuttX | **Fixed:** renamed “Operability / toward something operable” |
| P2 | Wi-Fi security vs VPN and NuttX vs FreeRTOS/Zephyr were absent, leaving common audience questions unanswered | general SW | **Fixed:** local-hop/end-to-end distinction on slide 4; careful NuttX positioning in slide 2 notes/scripts |
| P2 | Q&A still said fork push and `checkpatch.sh -g` had not happened | ASF / NuttX | **Fixed:** current three signed fork branches and completed patch-form checks recorded |

### Three-audience verdict after edits

- **General software engineer:** the “shallow passes, deep fails” story remains
  the strongest spine. The added local-hop/end-to-end distinction answers “why
  not just Wi-Fi security?” without turning the talk into a protocol lecture.
  The demo now has enough time to show causality: traffic changes the WireGuard
  counters, then the browser reaches the device.
- **ASF community member:** the contribution story is now candid at slide
  level: candidate branches exist on the author's forks, checks pass, and
  neither `dev@` discussion nor upstream PRs are open. This is more credible
  than implying process that has not happened.
- **NuttX maintainer:** claims are now scoped by implementation and revision.
  The talk distinguishes apps multi-version portability, kernel privilege-model
  runtime tests, pre-rebase hardware evidence, and post-rebase KERNEL/SMP
  reruns. That is the level of precision expected in a PR cover letter.

### Remaining presentation risks

- Slides 18–20 still consume about 10.7 minutes and are cognitively dense.
  This is defensible for the NuttX workshop audience, but rehearsal must avoid
  reading the tables verbatim. State the claim, one decisive datum, and the
  limitation; let the slide carry the rest.
- Slide 3 explains ASF to an ASF conference. It remains the first cut when time
  is short; its useful sentence is that “done” means community-maintainable.
- The Sony context is present and relevant as motivation, but the speaker
  should avoid implying that the kernel driver is an AITRIOS product feature.
  Say “the devices I work with exposed the need,” not “this ships in AITRIOS.”
- A live demo still depends on venue networking. Keep the recorded fallback
  locally available, not only as a YouTube URL, and rehearse the switch without
  apology or debugging on stage.
