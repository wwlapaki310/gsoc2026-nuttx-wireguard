#!/usr/bin/env python3
"""Regenerate coc-glasgow-script-en.md from the deck's speaker notes.

    python export_en_script.py

The "Say" text of every slide is the deck's own notes (the hidden
.notes-src block, paragraphs separated by "||"), so the script and the
notes cannot drift apart: edit the notes in ../coc-glasgow-slides.html and
re-run this. What the notes do not carry (section titles, what is on the
slide, pauses, demo checklist, cut order) lives in SLIDES below.

Times are cumulative and computed, not typed: 150 words per minute for
the notes, 10 s for each agenda divider, 90 s for the demo. Slide numbers
in the header (core block, cut order, dividers) come from the tags.
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

# In deck order: (heading, tag, on-slide text, extra cue lines). Tags:
# "divider", "demo", "core ...", "cut first", "cut candidate N".
SLIDES = [
    ("Title", "",
        '"WireGuard for Apache NuttX." ESP32-S3 & SPRESENSE tunnel over real '
        "Wi-Fi · real WireGuard peers (Linux kernel, Windows client) · runs in "
        "a kernel build on QEMU (BUILD_KERNEL) · one source across NuttX 13.0.1 / "
        "master / 12.7.0.", []),
    ("About Me", "",
        "Three bands: photo, name line and the fox icon / five hobby photos "
        "(space, semiconductors, DNA nanotech, robots, Linux) with three "
        "activity chips / a one-row path Robotics → Computer Vision → Edge AI "
        "→ Embedded Systems → NuttX / WireGuard, a picture per step.", []),
    ("Agenda", "",
        "The seven parts, each a short title with a keyword line and its "
        "time: overview & background (~5 min) / the problem and the idea "
        "(~4) / the plan (~2) / version 1: the app, six bugs, two in detail (~4) / version 2: "
        "the kernel driver, a bug in NuttX, testing (~7) / live demo (~3) / summary & "
        "status, what I learned (~4).",
        []),
    ('Agenda: ① Overview and Background', "divider", None, []),
    ("Project Overview", "",
        "The wg0 netdev picture: application / NSH, the NuttX network stack, "
        "wg0 next to eth0 / wlan0, a UDP socket underneath, the Internet, "
        "the peer.", []),
    ("How It Started: February to October", "",
        "A month axis Feb → Oct. Above it: mid Feb, read the GSoC idea "
        "list / early Mar, proposed the plan / Mar 20, heard "
        "about this conference from Alan, sent a talk / early Apr, GSoC "
        "proposal / early May, talk accepted, GSoC not selected / Oct, "
        "Glasgow. Below it: Jun–Jul a little at a time, August holiday "
        "most of the work, September FLAT app → kernel driver.", []),
    ("Background: What Is NuttX", "",
        "Software stack (applications / POSIX, ANSI, BSD socket API / "
        "networking, VFS, file systems, device drivers, NSH / NuttX kernel / "
        "MCU, MPU) and tags: RTOS, POSIX-oriented, BSD sockets, VFS, NSH, "
        "native networking, device drivers, ASF top-level project.", []),
    ("Where NuttX Fits", "",
        "FreeRTOS / Zephyr / NuttX table (design centre, typical use), GitHub "
        "stars with the note that stars are visibility, not deployment; "
        "number cards: 15+ architectures, 300+ boards, 1500+ configs, Sony, "
        "PX4, Xiaomi OpenVela 1000+ SKUs, Japan's 2024 lunar mission.",
        ["**Check before the talk:** the star counts move. Re-check them on "
         "the day, or leave the date on the slide as it is."]),
    ("History: NuttX at Sony", "",
        "Timeline: 2015 Sony audio products / 2018–19 SPRESENSE, CXD56xx "
        "(OSS + ELC Europe 2019) / 2020 upstream collaboration (NuttX Online "
        'Workshop, "A Journey from Fork to Mainline") / 2020s sensing and '
        "edge devices.", []),
    ("Experience: SPRESENSE and AITRIOS", "",
        "Photos of SPRESENSE and the AITRIOS edge AI camera; the flow "
        '"application development → system integration → deployment & '
        'operation".', []),
    ('Agenda: ② The Problem and the Idea', "divider", None, []),
    ("Problem: Reaching a Device From Far Away", "",
         "Engineer → Internet → router → Wi-Fi → NuttX device, showing that "
         "WPA2/WPA3 covers only the Wi-Fi hop; cards: open it to the "
         "Internet / make your own protocol / use a vendor's cloud.", []),
    ("Options: Other Ways to Reach a Device", "",
         "A table of seven ways (open a port / HTTPS to a cloud API / MQTT "
         "broker / reverse tunnel or relay / VPN on the router / carrier "
         "closed network / VPN on the device) against: who connects, any "
         "service or not, server in the middle, where security ends, what "
         "NuttX already has.",
         ["**If asked about Tailscale or ZeroTier:** they are also a VPN on "
          "the device, but they usually need a bigger OS than a "
          "microcontroller has today (footnote on the slide)."]),
    ("Solution: Why WireGuard", "",
         "Engineer / Linux / Windows ⟷ WireGuard tunnel ⟷ NuttX device; "
         "modern IP-layer VPN / UDP transport, public-key peers / already "
         "everywhere.", []),
    ("Architecture: WireGuard on the NuttX Network Stack", "",
         "Application → TCP/UDP/IP → wg0 (NET_LL_TUN netdev) → WireGuard "
         "encrypt/decrypt → UDP socket → wlan0 / usrsock → network, with TX "
         "and RX arrows.", []),
    ('Agenda: ③ The Plan', "divider", None, []),
    ("Porting Strategy: Reusing wireguard-lwip Without lwIP", "",
         "Three cards: protocol & crypto (KEEP) / platform hooks (ADAPT, the "
         "four functions in wireguard-platform.h) / lwIP network glue "
         "(REPLACE).", []),
    ("Design: Mapping lwIP onto NuttX netdev", "",
         "netif → net_driver_s, pbuf → iob, netif_add() → netdev_register(), "
         "callbacks → devif_poll().", []),
    ('Agenda: ④ Version 1: The App', "divider", None, []),
    ("Version 1: WireGuard as a NuttX Application", "",
         "Goal: run a standard WireGuard tunnel entirely as a NuttX "
         "application. Reuse the protocol and crypto; implement the NuttX "
         "OS hooks and native network glue. Takeaway: the first working "
         "tunnel proved the approach and exposed the problems still hidden.", []),
    ("Where It Was Hard: Six Bugs", "",
         "One table of all six bugs (bug / showed up in / lesson): 1 the "
         "receive thread stopped with the wg command / 2 ping works, TCP dies "
         "(in detail) / 3 \"no real impact\" was 10× (in detail) / 4 stack 94% "
         "full, saving the config froze the board, real boards only / 5 "
         "NuttX's own ChaCha20-Poly1305 (part 5) / 6 a 6 KB array on a 3 KB "
         "kernel stack (part 5). The GitHub line with a QR code. Takeaway: "
         "most hid behind a test that passed.",
         ["**Bugs 1 and 4 have no slide of their own any more.** Their story "
          "is in the notes above; the full version (and bug 1's "
          "`SO_RCVTIMEO` detour) is in the Q&A file."]),
    ("Bug 2/6: ping Works, TCP Dies", "my favorite", None, []),
    ("Bug 3/6: \"No Real Impact\" Was 10×", "cut candidate 3", None, []),
    ("Version 1 Demo: The First Video", "cut candidate 2",
         "Left: a still frame of the video (the Windows WireGuard client "
         "beside a telnet session into NuttX on the ESP32-S3). Right: the "
         "steps telnet 10.10.0.2 / uname -a, ifconfig / webserver & / "
         "browser http://10.10.0.2/; ESP32-S3 DevKit, apps version (FLAT), "
         "USB unplugged; the official Windows client; small text "
         "youtu.be/1kyX2av5WG4.",
         ["**No playback.** The still is enough; the video is the backup "
          "for the live demo.",
          "**It is the apps version.** Do not mix it with the "
          "kernel-version results later in the talk."]),
    ('Agenda: ⑤ Version 2: The Kernel Driver', "divider", None, []),
    ("Version 2: WireGuard as a NuttX Kernel Driver", "",
         "Two panels: Version 1, an app (in apps/, uses kernel internals, "
         "FLAT only, never an in-tree driver) / Version 2, a kernel driver "
         "(drivers/net/wireguard, set up by a small wg command through ioctl, "
         "made for FLAT, PROTECTED and KERNEL). One line on the three build "
         "modes.", []),
    ("The Kernel Design: The Big Picture", "",
         "A picture: user space holds only the wg command; an ioctl (one flat "
         "struct, no pointers; the private key is write-only) crosses the "
         "user/kernel boundary to wg0 in the kernel, which owns its UDP socket "
         "and RX thread, keeps timers / peers / cookies and uses NuttX crypto, "
         "between the NuttX network stack and the encrypted UDP to the peer.",
         []),
    ("Bugs 5 & 6/6: Two More Bugs, One in NuttX Itself", "locked with the kernel design",
         "Two panels: NuttX's own chacha20poly1305 nonce bug (handshake works, "
         "data after the first packet fails; two-line crypto: fix) / a "
         "BUILD_KERNEL-only crash (6 KB array on a 3 KB kernel stack, moved to "
         "the heap). One line: SPRESENSE did not boot with CONFIG_RTC_HIRES on "
         "13.0.1 / master; fixed locally, report drafted, not sent.", []),
    ("Testing: What Ran Where", "core — do not cut",
         "Three cards. FLAT on real boards: ESP32-S3 2026-10-12 with the exact "
         "submitted series (handshake with Linux, ping both ways up to the "
         "full MTU) / an earlier driver build on ESP32-S3 + SPRESENSE to the "
         "Windows client / SPRESENSE memory after 40 down/up. KERNEL on QEMU: "
         "tunnel to Linux, 4-CPU stress, one scripted run. PROTECTED on QEMU: "
         "tunnel works, the wg stack overflow found and fixed (20/20). Then "
         '"Not shown yet: PROTECTED or KERNEL on real hardware" and the GitHub '
         "line with a QR code.", []),
    ("Testing: What Ping Misses", "core — do not cut",
         "Three cards: same key on every boot? (SPRESENSE, 3 resets, 3 keys) "
         "/ keys left after down? (sim, all zero) / a leak after hours? (sim, "
         "151 rekeys, 100 down/up, nothing lost).", []),
    ("A Design Choice: The Timestamp Problem Has No Easy Answer",
         "core — do not cut",
         "The rule (a newer timestamp, even after a reboot), Chosen (real-time "
         "clock + the highest value this boot, plus a warning) / Not chosen "
         "(save every time, reserve a range in flash, wait), and the measured "
         "pair: clock unset → no handshake in 75 s, clock set → 4.1 s.", []),
    ('Agenda: ⑥ Live Demo', "divider", None, []),
    ("Demo Setup: One Tunnel on a Phone's Network", "",
         "Diagram: venue Wi-Fi (blocks devices from talking to each other) "
         "→ phone hotspot (its own small local network) → laptop "
         "(WireGuard peer in Docker, wg0 10.10.0.1; voice clips on :8000) "
         "and StackChan (ESP32-S3 + NuttX, wg0 10.10.0.2 over "
         "wlan0 on the hotspot; apps version v0.1.1, FLAT build), joined by one WireGuard tunnel; SPRESENSE as a "
         "dashed second peer (wg0 10.11.0.2).", []),
    ("The Demo", "demo",
         "Seven steps: wg show / telnet 10.10.0.2 / ifconfig / stackchan "
         "face happy / stackchan say hello.wav / tcpdump outside vs. inside "
         "/ wg show again. Right panel: outside = only encrypted UDP 51820, "
         "inside = the telnet text; \"Runs the apps version (v0.1.1), FLAT "
         "build.\"",
         ["**Live:** `scripts/stackchan/demo.sh`: `login` in the left "
          "terminal, `outside` and `inside` in two terminals on the right, "
          "`show` before and after. Put the terminals on the left and the "
          "StackChan (camera or on the desk) on the right.",
          "**Before the talk:** phone hotspot on (2.4 GHz / maximize "
          "compatibility), laptop on it, Docker Desktop running, the "
          "StackChan joined with `nsh_wifi.py` (the passphrase is not "
          "saved on the board), `wg_setup.py` run once. Check "
          "`stackchan say` once. If the hotspot was turned off and on, its "
          "addresses may change: run `nsh_wifi.py` and `wg_setup.py` "
          "again. Rarely (about 1 in 20) the board stops during `say`; "
          "power it off and on, then repeat the same steps.",
          "**Never on screen:** `wg showconf` (prints the private key), "
          "the key files in `%USERPROFILE%\\stackchan-wg`, the hotspot "
          "passphrase.",
          "**Fallback:** the recording. Switch at once if the live demo "
          "stalls."]),
    ('Agenda: ⑦ Summary and Status', "divider", None, []),
    ("Giving Back: Where It Stands", "cut first", None,
         ['**If asked "is it merged?":** not yet. These are submission '
          "candidates, not a claim that it is mergeable. See the Q&A file.",
          "**If time is short:** skip the whole slide. The talk's argument "
          "(the pattern, the verification) does not rest on it; just keep "
          "the answer above ready for questions."]),
    ("What I Learned", "",
         "Three cards: from application user to OS contributor / AI helped me "
         "code, but understanding was my job (highlighted) / a welcoming "
         "open-source community (docs and guides, review, Assisted-by and no "
         "AI Signed-off-by, CI and hackathons). Bottom line: the hope to keep "
         "contributing.",
         ["**Say the AI part plainly.** Not as a confession and not as a "
          "boast: it was faster, and understanding was still hard and still "
          "mine."]),
    ("Summary: What I Built, and Three Technical Lessons", "", None, []),
    ("Thanks: The People Behind This Talk", "",
         "Four boxes: Alan / the Apache NuttX community / the Apache "
         "Software Foundation and the CoC organizers and volunteers / "
         "Sony's NuttX developers, past and present.", []),
    ("Thank You", "", "The GitHub repository with a QR code, the demo video "
         "link, questions welcome.",
         ["**After:** keep [coc-glasgow-qa.md](coc-glasgow-qa.md) at hand, "
          "not a slide. The usual questions (rebase status, #14, IOB "
          "exhaustion, checkpatch) are answered there and on no slide."]),
]

HEADER = """\
# Speaking script — WireGuard for Apache NuttX (Community Over Code, Glasgow)

Deck: [`coc-glasgow-slides.html`](../coc-glasgow-slides.html) (and the
version on the official template, [`coc-glasgow-reveal/`](../coc-glasgow-reveal/)),
{count} slides, English. This file is **made by**
[`export_en_script.py`](export_en_script.py) from the speaker notes in the
deck (press `N` there to see them). The "Say" text below *is* those notes.
Do not edit this file by hand. Change the notes in the deck and run the
script again, or the two will stop matching. The Japanese
[practice script](coc-glasgow-script-ja.md) uses the same slide order.

**Total: about {total}, including a 90-second demo.** The times add up
from the start ("you should be at about this point"), at {wpm} words per
minute. This is just over a 30-minute slot, so practice to finish the
main talk in about 28 minutes, and keep the cut list below ready.

**The most important part is slides {core_range}** (testing). Together they take
{core} of the talk, and they hold the claims that a reviewer will really
check. Their slides and narration were made shorter on 2026-10-12 (the
detail is in the Q&A file and on GitHub). Do not cut their claims or their
caveats (FLAT on boards / KERNEL and PROTECTED on QEMU only / sim, the
earlier board runs vs. the 2026-10-12 ESP32-S3 run of the exact series,
the PROTECTED wg stack fault and its fix, one scripted 4-CPU run, nothing
shown on hardware for PROTECTED, the 75 s watch) any further. Cut before
them or after them. They are one story: the first is where it runs, the
second is what "it runs" does not prove, and the third is the one place
where I had to decide instead of test. If you split them, the story breaks.

**Do not cut slide 2 or slides 4–{arch} either** (About Me, then Overview to Architecture). What
NuttX is, where it is used, Sony's history with it, how I met it, and why
that led to WireGuard: everything after that depends on this. Keep slide
{learned} (What I learned) too: it is the speaker's own message.

**If time is short, cut in this order:**
1. Slide {cut1} (Giving Back). It helps the talk, but the talk does not
   depend on it. If you cut it, answer it in Q&A if someone asks.
2. Slide {cut2} (the version 1 video still). The live demo shows the same
   kind of thing.
3. Slide {cut3} (Bug 3/6, the 50 ms polling). Its one-line lesson is
   already in the bug table.

If the live demo stops, switch to the recording right away. **The agenda
slides ({dividers}) are a ten-second pause each**: point
at the highlighted line and pause. Do not read the list. If you have no
time at all, skip them without a word. They save almost no time, so cut the
three slides above instead.

**Check before the talk:** the GitHub star numbers on slide {stars} change. Check
them again on the day, or keep the date on the slide.

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
    if len(SLIDES) != count:
        raise SystemExit("SLIDES covers %d slides but the deck has %d"
                         % (len(SLIDES), count))

    def find(pred):
        return [i for i, sl in enumerate(SLIDES, 1) if pred(sl)]

    spans, t = [], 0
    for i, paras in enumerate(all_notes, 1):
        words = sum(len(p.split()) for p in paras)
        tag = SLIDES[i - 1][1]
        d = (DIVIDER_S if tag == "divider" else
             DEMO_S if tag == "demo" else round(words / WPM * 60))
        spans.append((t, t + d))
        t += d

    core = find(lambda sl: sl[1].startswith("core"))
    core_s = spans[core[-1] - 1][1] - spans[core[0] - 1][0]
    body = [HEADER.format(
        count=count, total=mmss(t), wpm=WPM,
        core="%.1f minutes" % (core_s / 60),
        core_range="%d–%d" % (core[0], core[-1]),
        arch=find(lambda sl: sl[0].startswith("Architecture"))[0],
        cut1=find(lambda sl: sl[1] == "cut first")[0],
        cut2=find(lambda sl: sl[1] == "cut candidate 2")[0],
        cut3=find(lambda sl: sl[1] == "cut candidate 3")[0],
        dividers=", ".join(map(str, find(lambda sl: sl[1] == "divider"))),
        learned=find(lambda sl: sl[0] == "What I Learned")[0],
        stars=find(lambda sl: sl[0].startswith("Where NuttX Fits"))[0])]

    for i, paras in enumerate(all_notes, 1):
        heading, tag, on_slide, extra = SLIDES[i - 1]
        a, b = spans[i - 1]
        suffix = (" *(%s)*" % tag if tag and tag not in ("divider", "demo")
                  else "")
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
