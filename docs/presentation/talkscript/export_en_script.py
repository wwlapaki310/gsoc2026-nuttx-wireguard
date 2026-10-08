#!/usr/bin/env python3
"""Regenerate coc-glasgow-script-en.md from the deck's speaker notes.

    python export_en_script.py

The "Say" text of every slide is the deck's own notes (the hidden
.notes-src block, paragraphs separated by "||"), so the script and the
notes cannot drift apart: edit the notes in ../coc-glasgow-slides.html and
re-run this. What the notes do not carry (section titles, what is on the
slide, pauses, demo checklist, cut order) lives in SLIDES below.

Times are cumulative and computed, not typed: 150 words per minute for
the notes, 10 s for each agenda divider, 90 s for the demo.
"""

import html
import re
from pathlib import Path

HERE = Path(__file__).resolve().parent
DECK = HERE.parent / "coc-glasgow-slides.html"
OUT = HERE / "coc-glasgow-script-en.md"

WPM = 150
DIVIDER_S = 10
DEMO_S = 90

# number: (heading, tag, on-slide text, extra cue lines)
SLIDES = {
    1: ("Title", "",
        '"WireGuard for Apache NuttX." ESP32-S3 & SPRESENSE tunnel over real '
        "Wi-Fi · real WireGuard peers (Linux kernel, Windows client) · runs in "
        "a real kernel build (BUILD_KERNEL) · one source across NuttX 13.0.1 / "
        "master / 12.7.0.", []),
    2: ("Agenda", "",
        "The seven chapters with their times: Background (~6 min) / Porting "
        "Strategy (~4) / Hidden Pitfalls (~7) / Kernel Driver (~5) / "
        "Verification (~5) / Demo (~2) / Toward Upstream (~4).", []),
    3: ("Agenda: ① Background", "divider", None, []),
    4: ("About Me", "",
        "Photo, and a timeline Robotics → Computer Vision → Edge AI → "
        "Embedded Systems → NuttX / WireGuard with a picture per step.", []),
    5: ("Project Overview", "",
        "The wg0 netdev picture: application / NSH, the NuttX network stack, "
        "wg0 next to eth0 / wlan0, a UDP socket underneath, the Internet, "
        "the peer.", []),
    6: ("Background: What Is NuttX", "",
        "Software stack (applications / POSIX, ANSI, BSD socket API / "
        "networking, VFS, file systems, device drivers, NSH / NuttX kernel / "
        "MCU, MPU) and tags: RTOS, POSIX-oriented, BSD sockets, VFS, NSH, "
        "native networking, device drivers, ASF top-level project.", []),
    7: ("Positioning: Where NuttX Fits", "",
        "FreeRTOS / Zephyr / NuttX table (design centre, typical use), GitHub "
        "stars with the note that stars are visibility, not deployment; "
        "number cards: 15+ architectures, 300+ boards, 1500+ configs, Sony, "
        "PX4, Xiaomi OpenVela 1000+ SKUs, Japan's 2024 lunar mission.",
        ["**Check before the talk:** the star counts move. Re-check them on "
         "the day, or leave the date on the slide as it is."]),
    8: ("History: NuttX at Sony", "",
        "Timeline: 2015 Sony audio products / 2018–19 SPRESENSE, CXD56xx "
        "(OSS + ELC Europe 2019) / 2020 upstream collaboration (NuttX Online "
        'Workshop, "A Journey from Fork to Mainline") / 2020s sensing and '
        "edge devices.", []),
    9: ("Experience: SPRESENSE and AITRIOS", "",
        "Photos of SPRESENSE and the AITRIOS edge AI camera; the flow "
        '"application development → system integration → deployment & '
        'operation".', []),
    10: ("Problem: The Remote Access Gap", "",
         "Engineer → Internet → router → Wi-Fi → NuttX device, showing that "
         "WPA2/WPA3 covers only the Wi-Fi hop; cards: public exposure / "
         "bespoke protocol / vendor cloud.", []),
    11: ("Solution: Why WireGuard", "",
         "Engineer / Linux / Windows ⟷ WireGuard tunnel ⟷ NuttX device; "
         "modern IP-layer VPN / UDP transport, public-key peers / already "
         "everywhere.", []),
    12: ("Architecture: WireGuard on the NuttX Network Stack", "",
         "Application → TCP/UDP/IP → wg0 (NET_LL_TUN netdev) → WireGuard "
         "encrypt/decrypt → UDP socket → wlan0 / usrsock → network, with TX "
         "and RX arrows.", []),
    13: ("Agenda: ② Porting Strategy", "divider", None, []),
    14: ("Porting Strategy: Reusing wireguard-lwip Without lwIP", "",
         "Three cards: protocol & crypto (KEEP) / platform hooks (ADAPT, the "
         "four functions in wireguard-platform.h) / lwIP network glue "
         "(REPLACE).", []),
    15: ("Design: Mapping lwIP onto NuttX netdev", "",
         "netif → net_driver_s, pbuf → iob, netif_add() → netdev_register(), "
         "callbacks → devif_poll().", []),
    16: ("Result: A Working Tunnel Is Only the Beginning", "",
         "Protocol & crypto: 3,079 lines unchanged / OS hooks: four functions "
         "/ network glue: the real work. wg genkey / set / setconf at run "
         "time.", []),
    17: ("Agenda: ③ Hidden Pitfalls", "divider", None, []),
    18: ("The Testing Pattern: Shallow Passes, Deep Fails", "",
         "SHALLOW TEST (handshake / ping, PASS) → ASSUMPTION (\"that part "
         "must be fine\") → DEEPER TEST (TCP / real hardware / kernel / "
         "security, FAIL).", []),
    19: ("Pitfall 1/7: SO_RCVTIMEO", "", None, []),
    20: ("Pitfall 2/7: The Detached pthread", "", None, []),
    21: ("Pitfall 3/7: ping Works, TCP Dies", "the good one", None, []),
    22: ("Pitfall 4/7: \"No Real Impact\" Was 10×", "", None, []),
    23: ("Pitfall 5/7: sim Passes, Hardware Freezes", "", None, []),
    24: ("Agenda: ④ Kernel Driver", "divider", None, []),
    25: ("Part 3: Into the Kernel", "", None, []),
    26: ("The Kernel Design", "", None, []),
    27: ("Two More Bugs, Same Shape", "locked with 26", None, []),
    28: ("Agenda: ⑤ Verification", "divider", None, []),
    29: ("Verification: Seven Places It Has to Hold", "core — do not cut",
         "Table: sim / rv-virt knetnsh64 (BUILD_KERNEL) / rv-virt pnsh64 "
         "(BUILD_PROTECTED) / rv-virt knetnsh64_smp (4 CPUs) / SPRESENSE "
         "measured / ESP32-S3 + SPRESENSE over real Wi-Fi / apps v0.1.1 on "
         "both boards.", []),
    30: ("Verification: A Working Tunnel Is Not Evidence", "core — do not cut",
         "Three rows (same private key every boot / session keys that "
         "outlive the tunnel / a leak that shows after hours), each with "
         '"would ping notice?" → no.', []),
    31: ("A Design Call: The Timestamp Problem Has No Free Answer",
         "core — do not cut",
         "What replay protection obliges, the four options (real-time clock "
         "+ in-boot high-water mark / persist every time / durable range "
         "reservation / wait to catch up), and the measured pair: clock "
         "unset → no handshake in 75 s, clock set → 4.1 s.", []),
    32: ("Agenda: ⑥ Demo", "divider", None, []),
    33: ("The Demo", "", None,
         ["**Live:** `uname -a` / `ifconfig` (wg0 = 10.10.0.2) / `wg show` "
          "(handshake, transfer bytes) / `ps` (wg_rx running) / "
          "`webserver &`, then `http://10.10.0.2/` in the browser. Show "
          "`wg show` before and after opening the page: the growing byte "
          "count is the evidence, not the terminal text.",
          "**Never on screen:** the contents of `.config`, `kconfig-tweak` "
          "runs, build logs, `wg showconf` (prints the private key). The "
          "SSID and passphrase are in plain text in those places.",
          "**Fallback:** the recording, youtu.be/1kyX2av5WG4. Switch at once "
          "if the live demo stalls."]),
    34: ("Agenda: ⑦ Toward Upstream", "divider", None, []),
    35: ("Operability", "cut candidate 3", None, []),
    36: ("Portability", "cut candidate 2", None, []),
    37: ("Contributing Back", "cut first", None,
         ['**If asked "is it merged?":** not yet. These are submission '
          "candidates, not a claim that it is mergeable. See the Q&A file.",
          "**If time is short:** skip the whole slide. The talk's argument "
          "(the pattern, the verification) does not rest on it; just keep "
          "the answer above ready for questions."]),
    38: ("Takeaways", "", None, []),
    39: ("Thank You", "", None,
         ["**After:** keep [coc-glasgow-qa.md](coc-glasgow-qa.md) at hand, "
          "not a slide. The usual questions (rebase status, #14, IOB "
          "exhaustion, checkpatch) are answered there and on no slide."]),
}

HEADER = """\
# Speaking script — WireGuard for Apache NuttX (Community Over Code, Glasgow)

Deck: [`coc-glasgow-slides.html`](../coc-glasgow-slides.html) (and its
official-template edition [`coc-glasgow-reveal/`](../coc-glasgow-reveal/)),
{count} slides, English. **Generated** by
[`export_en_script.py`](export_en_script.py) from the speaker notes in the
deck (press `N` there to see them live): the "Say" text below *is* those
notes. Do not edit this file by hand. Edit the notes in the deck and
re-run the script, or the two will drift. The Japanese
[rehearsal script](coc-glasgow-script-ja.md) follows the same slide order.

**Total: about {total}, including a 90-second demo.** Times are cumulative
("you should be at roughly this point") at {wpm} words per minute. That is
already a little over a 30-minute slot, so rehearse to land the main body at
27–28 minutes and keep the cut list below ready.

**The centre of gravity is slides 29–31** (verification): together they are
{core} of the talk, and they carry the claims a reviewer will actually
check. Do not compress that block to protect time elsewhere. Cut before it
or after it, never inside it. 29 → 30 → 31 is one argument: 29 is where it
runs, 30 is what "it runs" does not prove, 31 is the one place the answer
is a judgment call rather than a test. Splitting them loses the argument.

**Do not cut slides 4–12 either** (About Me through Architecture). What
NuttX is, where it is used, Sony's history with it, where I met it and why
that led to WireGuard: everything after depends on that chain.

**If time is short, drop in this order:**
1. Slide 37 (Contributing Back). It supports the talk but is not its
   spine; if dropped, answer it in Q&A if asked.
2. Slide 36 (portability table). Supporting evidence, not the spine.
3. Slide 35 (operability).

If the live demo stalls, switch to the recording at once. **The agenda
dividers (3, 13, 17, 24, 28, 32, 34) are a ten-second breath each**: point
at the highlighted line and pause, do not read the list. With no time at
all, advance through them silently. They save almost nothing, so cut the
three slides above instead.

**Check before the talk:** the GitHub star counts on slide 7 change. Re-check
them on the day, or leave the date on the slide.

---
"""


def notes():
    deck = DECK.read_text(encoding="utf-8")
    out = []
    for sec in re.split(r"<section\b", deck)[1:]:
        m = re.search(r'<div class="notes-src">(.*?)</div>\s*</section>',
                      sec, re.S)
        raw = m.group(1) if m else ""
        paras = []
        for p in raw.split("||"):
            p = html.unescape(re.sub(r"<[^>]+>", "", p))
            p = re.sub(r"\s+", " ", p).strip()
            if p:
                paras.append(p)
        out.append(paras)
    return out


def mmss(s):
    return "%d:%02d" % (s // 60, s % 60)


def main():
    all_notes = notes()
    count = len(all_notes)
    if sorted(SLIDES) != list(range(1, count + 1)):
        raise SystemExit("SLIDES covers %d slides but the deck has %d"
                         % (len(SLIDES), count))

    spans, t = [], 0
    for i, paras in enumerate(all_notes, 1):
        words = sum(len(p.split()) for p in paras)
        tag = SLIDES[i][1]
        d = (DIVIDER_S if tag == "divider" else
             DEMO_S if i == 33 else round(words / WPM * 60))
        spans.append((t, t + d))
        t += d

    core = spans[30][1] - spans[28][0]
    body = [HEADER.format(count=count, total=mmss(t), wpm=WPM,
                          core="%.1f minutes" % (core / 60))]

    for i, paras in enumerate(all_notes, 1):
        heading, tag, on_slide, extra = SLIDES[i]
        a, b = spans[i - 1]
        suffix = " *(%s)*" % tag if tag and tag != "divider" else ""
        body.append("## %d — %s (%s → %s)%s\n" % (i, heading, mmss(a),
                                                  mmss(b), suffix))
        if on_slide:
            body.append("**On slide:** %s\n" % on_slide)
        if tag == "divider":
            body.append("**Say (one line):** %s\n" % " ".join(paras))
            body.append("**Pause:** point at the highlighted line, one "
                        "beat.\n")
        else:
            body.append("**Say:**")
            body.extend("- %s" % p for p in paras)
            body.append("")
        for line in extra:
            body.append(line + "\n")

    OUT.write_text("\n".join(body).rstrip() + "\n", encoding="utf-8",
                   newline="\n")
    print("wrote %s (%d slides, %s)" % (OUT.name, count, mmss(t)))


if __name__ == "__main__":
    main()
