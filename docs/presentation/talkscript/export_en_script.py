#!/usr/bin/env python3
"""Regenerate coc-glasgow-script-en.md from the deck's speaker notes.

    python export_en_script.py

The notes of every slide are the hidden .notes-src block of
../coc-glasgow-slides.html: paragraphs separated by "||". A paragraph is

  - Essential (the default): spoken. Read these from top to bottom and the
    talk is complete, links between slides included;
  - Optional, if it starts with "[opt]": spoken only when there is time;
    the next sentence never depends on it;
  - a Presenter Note, if it starts with "[note]": never spoken (sources,
    operation steps, checks).

So the script and the notes cannot drift apart: edit the notes in the deck
and re-run this. What the notes do not carry (section titles, what is on
the slide, cut order, longer cues) lives in SLIDES below.

Times are computed, not typed: WPM words per minute for the Essential text,
10 s for each agenda divider, DEMO_S for the live demo (talking and
operating). Optional text is added on top for the second total.
"""

import html
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
DECK = HERE.parent / "coc-glasgow-slides.html"
OUT = HERE / "coc-glasgow-script-en.md"

WPM = 125
DIVIDER_S = 10
DEMO_S = 180

# In deck order: (heading, tag, on-slide text, extra cue lines). Tags:
# "divider", "demo", "core ...", "cut first", "cut candidate N".
SLIDES = [
    ("Title", "",
        '"WireGuard for Apache NuttX", large, with the subtitle "Bringing '
        'Secure Remote Access to Embedded Devices". Community Over Code '
        "Glasgow 2026 on top; Satoru Akita, Sony Semiconductor Solutions "
        "below. A QR code of the slides (\"Slides\") at the bottom right. "
        "No technical details on this slide.", []),
    ("About Me", "",
        "Three bands: photo, name line and the fox icon / a map of Japan "
        "(Nagoya, Sendai, Kawasaki) labelled Japan / five large photos under "
        '"My background & activities": Rocket / Aerospace Engineering, DNA '
        "Nanorobotics, Semiconductor Research, Robots, Open-Source "
        "Conference Volunteer.", []),
    ("Agenda", "",
        "The seven parts, each a short title with a one-line preview and "
        "its time: overview & background, project origins, NuttX & Sony, my "
        "experience (~5 min) / the problem & solution, the remote access "
        "challenge, why WireGuard (~4) / the development plan, reuse "
        "wireguard-lwip, adapt to NuttX (~2) / version 1, "
        "application-based implementation, the first working tunnel, "
        "debugging challenges (~4) / version 2, the kernel driver, why a "
        "driver, integration challenges, validation (~5) / live demo, "
        "secure remote access, Stack-chan control (~3) / summary & "
        "reflections, achievements, personal reflections, community (~4).",
        []),
    ('Agenda: ① Overview and Background', "divider", None, []),
    ("Project Overview", "",
        "The wg0 netdev picture (unchanged): application / NSH, the NuttX "
        "network stack, wg0 next to eth0 / wlan0, a UDP socket underneath, the "
        "Internet, the peer. Below it three cards: lightweight VPN / virtual "
        "interface (wg0) / secure communication over UDP.", []),
    ("How It Started: February to October", "",
        "A month axis Feb → Oct. Above it: mid Feb, read the GSoC idea "
        "list / early Mar, proposed the plan / Mar 20, heard about this "
        "conference from Alan, submitted a CfP / early Apr, GSoC proposal / "
        "early May, CfP accepted, GSoC not selected / Oct, Glasgow. Below it, "
        "three arrows: May–Jul study and exploration → Aug Version 1 "
        "(application, FLAT, hardware testing) → Sep– Version 2 (kernel "
        "driver).", []),
    ("A Quick Overview of Apache NuttX", "",
        "Left: logo, four points (POSIX-like real-time OS / small footprint / "
        "broad hardware support, 15+ architectures, 300+ boards / Apache "
        "open-source project) and a small table of standard APIs (FreeRTOS, "
        "Zephyr, NuttX). Right: photo tiles of real devices: Sony, Fitbit "
        "Charge 3 / 4, PX4 flight controllers, the SORA-Q lunar robot, and "
        "Xiaomi OpenVela (1,000+ products).",
        ["**Check before the talk:** the Fitbit models come from Fitbit's own "
         "2020 NuttX talk, and the Xiaomi number is a reported figure."]),
    ("Sony's History with NuttX", "",
        "Timeline: 2013–15 early adoption in audio products / 2016–17 "
        "commercial products / 2018 Xperia Ear Duo and SPRESENSE / 2019–20 "
        "upstream collaboration / 2020s expanded edge-sensing capabilities.", []),
    ("My Experience with NuttX", "",
        "Two photo cards: SPRESENSE, a satellite project (picture of the "
        "ARICA CubeSat) and AITRIOS, an edge AI sensing platform (edge AI "
        "camera). Bottom line: I wanted a secure way to access and "
        "troubleshoot remote embedded devices.",
        ["**Check before the talk:** the ARICA picture is a third-party "
         "image. Get the project's permission, or replace it."]),
    ('Agenda: ② The Problem & Solution', "divider", None, []),
    ("Problem: How Can We Securely Access Remote Devices?", "",
         "Engineer → Internet → router → Wi-Fi → NuttX device, with a "
         "bracket for the secure path needed all the way, and WPA2/WPA3 "
         "covering only the Wi-Fi hop; three cards: devices are far away / "
         "remote diagnosis / a secure path.", []),
    ("Options: Ways to Reach a Remote Device", "",
         "A table of six ways (open a port / HTTPS or MQTT via a cloud / "
         "reverse tunnel or relay / VPN on the router / carrier closed "
         "network / VPN on the device) against: any IP service, no server in "
         "the middle, protected up to where.",
         ["**If asked about Tailscale or ZeroTier:** they are also a VPN on "
          "the device, but they usually need a bigger OS than a "
          "microcontroller has today (footnote on the slide)."]),
    ("Solution: A VPN on the Device", "",
         "Engineer / Linux / Windows ⟷ WireGuard tunnel ⟷ NuttX device. "
         '"Why WireGuard?": small & simple (a small bar chart of lines of '
         "code: IPsec ~420k, OpenVPN ~120k, WireGuard ~3.8k, from 2018) / "
         "modern cryptography (ChaCha20-Poly1305, Curve25519) / UDP-based "
         "tunnel.",
         []),
    ("Architecture: WireGuard on the NuttX Network Stack", "",
         "Application → TCP/UDP/IP → wg0 (NET_LL_TUN netdev) → WireGuard "
         "encrypt/decrypt → UDP socket → wlan0 / usrsock → network, with TX "
         "and RX arrows.", []),
    ('Agenda: ③ The Development Plan', "divider", None, []),
    ("Porting Strategy: Start from wireguard-lwip", "",
        "The link github.com/smartalock/wireguard-lwip (clickable), then "
        "three rows: KEEP protocol & crypto (with two small fixes) / ADAPT "
        "platform hooks / REPLACE lwIP network glue.",
        ["**Check before the talk:** the four platform hooks are a "
         "monotonic clock, random bytes, TAI64N time and an under-load check "
         "(`wireguard-platform.h`). Say them from the notes, not the slide."]),
    ("Mapping lwIP Networking to NuttX", "",
        "Table: network device (netif → net_driver_s) / packet buffers "
        "(pbuf → iob) / registration (netif_add() → netdev_register()) / "
        "sending (output callback → d_txavail → devif_poll()) / UDP wire "
        "(udp_pcb → UDP socket). A note: matches of ideas, not a one-to-one "
        "swap of APIs.", []),
    ('Agenda: ④ Version 1: Application-Based Implementation', "divider", None, []),
    ("Version 1: WireGuard as a NuttX Application", "",
         "A GOAL box (run a standard WireGuard tunnel as a NuttX "
         "application; reuse the existing core and implement the NuttX "
         "integration). A table of layer / work needed / in this port: "
         "protocol & crypto (almost none; crypto byte-identical, two small "
         "fixes; ~3,100 lines reused, ~40 changed) / OS hooks (four "
         "functions; ~120 new lines) / network glue (the real work: netdev + "
         "UDP socket + RX task; ~1,200 new lines). Takeaway in two lines: "
         "Version 1 proved WireGuard could run on NuttX; it also exposed OS "
         "and networking problems the first tunnel did not reveal.",
         []),
    ("Where It Was Hard: Six Bugs", "",
         "One table of all six bugs (bug / showed up in / lesson): 1 the "
         "receive thread stopped with the wg command / 2 ping works, TCP dies "
         "(in detail) / 3 \"no real impact\" was 10× (in detail) / 4 stack 94% "
         "full, saving the config froze the board, real boards only / 5 "
         "NuttX's own ChaCha20-Poly1305 (part 5) / 6 a 6 KB array on a 3 KB "
         "kernel stack (part 5). Takeaway: most hid behind a test that "
         "passed.",
         ["**Bugs 1 and 4 have no slide of their own any more.** Their story "
          "is in the notes above; the full version (and bug 1's "
          "`SO_RCVTIMEO` detour) is in the Q&A file."]),
    ("Bug ②: Ping Works. TCP Dies.", "my favorite",
         "Orange-red title. Left box SYMPTOM: ping 0% loss; the TCP "
         "handshake completes, then the connection dies; EBADF, \"Bad file "
         "descriptor\". Right box ROOT CAUSE & FIX: sends from wg_rx worked, "
         "sends from the system work queue failed; a file descriptor belongs "
         "to one task group; use psock_*() on a struct socket.", []),
    ("Bug ③: \"No Real Impact\" Was 10× Slower", "cut candidate 3",
         "Orange-red title. Two boxes: QUICK FIX, check every 50 ms "
         "(measured: about 10× slower) / REAL FIX, event-driven wake-up "
         "(psock_poll() callback and a semaphore).", []),
    ("Version 1 Demo: The First Video", "cut candidate 2",
         "Left: the video as a still frame (the Windows WireGuard client "
         "beside a telnet session into NuttX on the ESP32-S3) with a play "
         "button; a click plays the YouTube video in the slide, and leaving "
         "the slide stops it. Right: the "
         "steps telnet 10.10.0.2 / uname -a, ifconfig / webserver & / "
         "browser http://10.10.0.2/; ESP32-S3 DevKit, apps version (FLAT), "
         "USB unplugged; the official Windows client; small text "
         "youtu.be/1kyX2av5WG4.",
         ["**It is the apps version.** Do not mix it with the "
          "kernel-version results later in the talk."]),
    ('Agenda: ⑤ Version 2: The Kernel Driver', "divider", None, []),
    ("Version 2: WireGuard as a NuttX Kernel Driver", "",
         "Two panels, BEFORE — VERSION 1 (an application: in apps/, uses "
         "kernel internals, FLAT only, does not fit as an in-tree driver) / "
         "AFTER — VERSION 2 (a kernel driver: drivers/net/wireguard, set up "
         "by a small wg command through ioctl, made for FLAT, PROTECTED and "
         "KERNEL). A BUILD MODES box with three rows: FLAT (one address "
         "space) / PROTECTED (MPU) / KERNEL (MMU). The goal line.", []),
    ("The Kernel Design: The Big Picture", "",
         "A picture in two bands: USER SPACE holds only the wg command; an "
         "ioctl (one flat struct, no pointers; the private key is "
         "write-only) crosses the user/kernel boundary into the NUTTX KERNEL "
         "band, to wg0, which owns its UDP socket and RX thread, keeps "
         "timers / peers / cookies and uses NuttX crypto, between the NuttX "
         "network stack and the encrypted UDP to the peer.",
         []),
    ("Two Issues Found During Kernel Integration", "",
         "Two labelled boxes. CRYPTOGRAPHY — ChaCha20-Poly1305 (NuttX "
         "crypto/chachapoly.c: the nonce counter sits in different bytes "
         "from the RFC 8439 / WireGuard layout; the handshake works, data "
         "after the first packet fails; a two-line fix with test vectors) / "
         "KERNEL STACK — Stack Overflow (this driver, KERNEL build only: a "
         "6 KB array on a 3 KB kernel stack, moved to the heap). The "
         "SPRESENSE RTC_HIRES finding is in the notes only.",
         ["**Say it exactly:** the stack overflow was in this driver, not in "
          "NuttX. Nothing is filed or sent yet; do not say it is."]),
    ("Testing: What Ran Where", "core — do not cut",
         "A table, build mode / environment / result: FLAT on ESP32-S3 and "
         "SPRESENSE (real boards) verified / KERNEL on QEMU rv-virt "
         "verified (tunnel to Linux, 4 CPUs under load) / PROTECTED on QEMU "
         "rv-virt verified (one fault found and fixed, 20/20) / KERNEL and "
         "PROTECTED on real hardware not tested yet. A small note that the "
         "check mark means it passed in the conditions noted. The GitHub "
         "line and QR code are gone.", []),
    ('Agenda: ⑥ Live Demo', "divider", None, []),
    ("Live Demo: Stack-chan over WireGuard", "demo",
         "One slide, kept on screen with the terminal beside it. Top, NETWORK "
         "SETUP: Laptop (WireGuard peer, VPN IP 10.10.0.1) and Stack-chan "
         "(Apache NuttX + WireGuard, VPN IP 10.10.0.2), both on a "
         "Smartphone (Wi-Fi hotspot, provides the local network) over "
         "dashed Wi-Fi links, and one thick red Encrypted WireGuard Tunnel "
         "between laptop and Stack-chan. Bottom, LIVE DEMO STEPS: 1 VPN "
         "Connection (wg show) / 2 Remote Login (telnet 10.10.0.2) / 3 "
         "Network Interface (ifconfig → wg0) / 4 Control Stack-chan (change "
         "face, play voice). A small Stack-chan photo that links to its "
         "GitHub repository. No Version label, no takeaway box, no video "
         "link.",
         ["**Before the talk:** phone hotspot on (2.4 GHz / maximize "
          "compatibility), laptop on it, Docker Desktop running, the "
          "Stack-chan joined with `nsh_wifi.py` (the passphrase is not "
          "saved on the board), `wg_setup.py` run once. Check "
          "`stackchan say` once. If the hotspot was turned off and on, its "
          "addresses may change: run `nsh_wifi.py` and `wg_setup.py` "
          "again. Rarely (about 1 in 20) the board stops during `say`; "
          "power it off and on, then repeat the same steps.",
          "**Never on screen:** `wg showconf` (prints the private key), "
          "the key files in `%USERPROFILE%\\stackchan-wg`, the hotspot "
          "passphrase.",
          "**Fallback:** if the live demo stalls, say so and open "
          "the next slide, the backup video (click the thumbnail), or go "
          "on to the summary."]),
    ("Live Demo: backup video", "backup",
         "A video player thumbnail (click to play) of the recorded demo, "
         "with a small panel: THREE TERMINALS, left control, middle tcpdump "
         "encrypted, right tcpdump plaintext, and the video link. For the "
         "case the live demo fails; skipped otherwise.", []),
    ('Agenda: ⑦ Summary and Reflections', "divider", None, []),
    ("Summary", "",
         "Three rows, each a short heading and one sentence: 1 Secure "
         "Communication (brought WireGuard to Apache NuttX, secure IP "
         "communication with standard peers) / 2 Application to Kernel "
         "Driver (evolved from an application into a kernel network driver, "
         "tested on real boards and QEMU) / 3 Toward Upstream Contribution "
         "(preparing the driver and related fixes for review; a small line: "
         "prepared as small, separate changes, nothing is submitted yet).",
         ["**State of the submission (2026-10-13):** no pull request about "
          "this work exists in apache/nuttx or apache/nuttx-apps. The only "
          "related issue is the closed GSoC proposal thread (#18548). The "
          "branches exist only on the author's forks. Do not say "
          "\"submitted\"."]),
    ("What I Learned", "",
         "Three cards: from application user to OS contributor / AI helped me "
         "code, but understanding was my job (highlighted) / a welcoming "
         "open-source community (docs and guides, review, AI-assisted "
         "development with people responsible, CI and hackathons help new "
         "people join). Bottom line: the hope to keep contributing.",
         []),
    ("Acknowledgements", "",
         "A plain vertical list, a name and one line each: Alan Carvalho de "
         "Assis (encouraged me to submit this talk) / The Apache NuttX "
         "Community (discussions, documentation, support) / Sony's NuttX "
         "Developers, Past and Present (their contributions and the "
         "foundation they built) / The Apache Software Foundation & Community "
         "Over Code Organizers (bringing the community together).", []),
    ("Thank You", "",
         "A QR code of the slides (\"Scan to view the slides\") with the "
         "slides link and the GitHub repository link, both clickable. The "
         "same QR code is on the title slide. No video link, no call for "
         "questions.",
         ["**After:** keep [coc-glasgow-qa.md](coc-glasgow-qa.md) at hand, "
          "not a slide. The usual questions (rebase status, #14, IOB "
          "exhaustion, checkpatch) are answered there and on no slide.",
          "**Check before the talk:** the QR code and both links open "
          "https://wwlapaki310.github.io/gsoc2026-nuttx-wireguard/docs/"
          "presentation/coc-glasgow-reveal/index.html (GitHub Pages must "
          "be up to date with the last push)."]),
    ("Testing: What Ping Misses", "appendix",
         "Three cards: same key on every boot? (SPRESENSE, 3 resets, 3 "
         "keys) / keys left after down? (sim, all zero) / a leak after "
         "hours? (sim, 151 rekeys, 100 down/up, nothing lost).", []),
    ("Design Choice: Trust the Clock?", "appendix",
         "The rule (a newer timestamp, even after a reboot), Chosen (real-time "
         "clock + the highest value this boot, plus a warning) / Not chosen "
         "(save every time, reserve a range in flash, wait), and the measured "
         "pair: clock unset → no handshake in 75 s, clock set → 4.1 s.", []),
]

HEADER = """\
# Speaking script — WireGuard for Apache NuttX (Community Over Code, Glasgow)

Deck: [`coc-glasgow-slides.html`](../coc-glasgow-slides.html) (and the
version on the official template, [`coc-glasgow-reveal/`](../coc-glasgow-reveal/)),
{count} main slides plus {napp} appendix slides, English. This file is **made by**
[`export_en_script.py`](export_en_script.py) from the speaker notes in the
deck (press `S` in the reveal edition, `N` in the standalone one). Do not edit
this file by hand. Change the notes in the deck and run the script again, or the
two will stop matching. The Japanese [practice script](coc-glasgow-script-ja.md) is made
from the same notes by [`export_ja_script.py`](export_ja_script.py) and follows this
script one to one (same slides, same Essential / Optional / Presenter Notes, same times).

## How to read this script

Every slide has up to three parts:

- **Say — Essential:** read these lines, top to bottom. They are a complete talk by
  themselves, with the links between the slides. A slow, clear voice is assumed.
- **Say — Optional:** add them if there is time. If you skip them, the next
  sentence still works.
- **Presenter Notes:** never spoken. Sources, the steps of the demo, checks,
  cautions.

## Time

{wpm} words per minute for the spoken text, 10 seconds for each agenda slide, and
{demo} for the live demo (talking and typing). Slide changes and short pauses are
inside these numbers.

| | Words | Time |
|---|---:|---:|
| **Essential only** (agenda pauses and live demo included) | {e_words} | **{e_time}** |
| Essential + Optional | {eo_words} | {eo_time} |
| of which the live demo | {demo_words} | {demo} |
| Room left in a 30-minute slot, Essential only | | {room_e} |
| Room left in a 30-minute slot, Essential + Optional | | {room_eo} |

| Part | Slides | Essential | + Optional |
|---|---|---:|---:|
{chapters}

The time of each slide, shown in its heading, is the Essential time, added up
from the start ("you should be about here").

## What must not be cut

**The most important slide is {core_range}** (Testing: What Ran Where): the claims a
reviewer will check. Keep its caveats: FLAT on real boards, KERNEL and PROTECTED on
QEMU only, nothing yet on real hardware for KERNEL or PROTECTED.
The slides "What Ping Misses" and "Trust the Clock?" are the appendix (A1, A2) after
Thank You: not in the times above, opened with the down arrow only if a question needs them.

**Keep the story of the first half** (slides 2 and 4–{arch}: About Me, then Overview to
Architecture): why I started, Sony and NuttX, my own experience, the problem, why a VPN on
the device, why WireGuard. Everything after depends on it. Keep slide {learned} (What I
learned) too: it is the speaker's own message.

**If time is short, cut in this order:**
1. Every Optional line.
2. Slide {cut2} (the Version 1 video still). The live demo shows the same kind of thing.
3. Slide {cut3} (Bug ③, the 50 ms polling). Its one-line lesson is already in the bug table.

If the live demo stops, say so and open the next slide, the backup
video (not timed), or go on to the summary. **The agenda slides ({dividers}) are a ten-second pause each**: point at the
highlighted line and say the one sentence. If you have no time at all, skip them.

---
"""


def parse_notes():
    """Per slide: three lists of paragraphs (essential, optional, notes)."""
    deck = DECK.read_text(encoding="utf-8")
    out = []
    for sec in re.split(r"<section\b", deck)[1:]:
        m = re.search(r'<div class="notes-src">(.*?)</div>\s*</section>',
                      sec, re.S)
        raw = m.group(1) if m else ""
        ess, opt, note = [], [], []
        for p in raw.split("||"):
            p = html.unescape(re.sub(r"<[^>]+>", "", p))
            p = re.sub(r"\s+", " ", p).strip()
            if not p:
                continue
            if p.startswith("[opt]"):
                opt.append(p[5:].strip())
            elif p.startswith("[note]"):
                note.append(p[6:].strip())
            else:
                ess.append(p)
        out.append((ess, opt, note))
    return out


def words(paras):
    return sum(len(p.split()) for p in paras)


def mmss(s):
    return "%d:%02d" % (s // 60, s % 60)


def analyse(all_notes):
    """Times and totals, shared with export_ja_script.py."""
    count = len(all_notes)

    def find(pred):
        return [i for i, sl in enumerate(SLIDES, 1) if pred(sl)]

    # times per slide: (essential seconds, optional seconds, e words, o words)
    per = []
    for i, (ess, opt, note) in enumerate(all_notes, 1):
        tag = SLIDES[i - 1][1]
        ew, ow = words(ess), words(opt)
        if tag in ("appendix", "backup"):
            per.append((0, 0, 0, 0))
            continue
        if tag == "divider":
            es = DIVIDER_S
        elif tag == "demo":
            es = DEMO_S
        else:
            es = round(ew / WPM * 60)
        per.append((es, round(ow / WPM * 60), ew, ow))

    spans, t = [], 0
    for es, os_, ew, ow in per:
        spans.append((t, t + es))
        t += es
    e_time = t
    eo_time = t + sum(x[1] for x in per)
    e_words = sum(x[2] for x in per)
    eo_words = e_words + sum(x[3] for x in per)
    demo_i = find(lambda sl: sl[1] == "demo")[0]
    demo_words = per[demo_i - 1][2] + per[demo_i - 1][3]
    napp = len(find(lambda sl: sl[1] == "appendix"))

    # parts: 0 = opening (before the first agenda divider), then 1..7
    dividers = find(lambda sl: sl[1] == "divider")
    bounds = [1] + dividers + [count - napp + 1]
    names = ["Opening: title, about me, agenda"] + [
        re.sub(r"^Agenda: ", "", SLIDES[d - 1][0]) for d in dividers]
    rows = []
    for k in range(len(bounds) - 1):
        a, b = bounds[k], bounds[k + 1] - 1
        es = sum(per[i - 1][0] for i in range(a, b + 1))
        os_ = sum(per[i - 1][1] for i in range(a, b + 1))
        rows.append("| %s | %d–%d | %s | %s |" % (names[k], a, b, mmss(es), mmss(es + os_)))

    return dict(per=per, spans=spans, e_time=e_time, eo_time=eo_time,
                e_words=e_words, eo_words=eo_words, demo_words=demo_words,
                napp=napp, dividers=dividers, names=names, rows=rows,
                bounds=bounds)


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    all_notes = parse_notes()
    count = len(all_notes)
    if len(SLIDES) != count:
        raise SystemExit("SLIDES covers %d slides but the deck has %d"
                         % (len(SLIDES), count))

    def find(pred):
        return [i for i, sl in enumerate(SLIDES, 1) if pred(sl)]

    d = analyse(all_notes)
    per, spans = d["per"], d["spans"]
    e_time, eo_time = d["e_time"], d["eo_time"]
    e_words, eo_words = d["e_words"], d["eo_words"]
    demo_words, napp = d["demo_words"], d["napp"]
    dividers, rows = d["dividers"], d["rows"]

    core = find(lambda sl: sl[1].startswith("core"))
    body = [HEADER.format(
        count=count - napp, napp=napp, wpm=WPM, demo=mmss(DEMO_S),
        e_words=e_words, e_time=mmss(e_time), eo_words=eo_words,
        eo_time=mmss(eo_time), demo_words=demo_words,
        room_e=mmss(1800 - e_time), room_eo=mmss(1800 - eo_time),
        chapters="\n".join(rows),
        core_range=("%d" % core[0] if len(core) == 1
                    else "%d–%d" % (core[0], core[-1])),
        arch=find(lambda sl: sl[0].startswith("Architecture"))[0],
        cut2=find(lambda sl: sl[1] == "cut candidate 2")[0],
        cut3=find(lambda sl: sl[1] == "cut candidate 3")[0],
        dividers=", ".join(map(str, dividers)),
        learned=find(lambda sl: sl[0] == "What I Learned")[0])]

    k = 0
    for i, (ess, opt, note) in enumerate(all_notes, 1):
        heading, tag, on_slide, extra = SLIDES[i - 1]
        a, b = spans[i - 1]
        suffix = (" *(%s)*" % tag
                  if tag and tag not in ("divider", "demo", "appendix", "backup")
                  else "")
        if tag == "appendix":
            k += 1
            body.append("## A%d — %s (appendix, not timed)\n" % (k, heading))
        else:
            body.append("## %d — %s (%s → %s)%s\n" % (i, heading, mmss(a),
                                                      mmss(b), suffix))
        if on_slide:
            body.append("**On slide:** %s\n" % on_slide)
        if tag == "divider":
            body.append("**Say (one line):** %s\n" % " ".join(ess))
            body.append("**Pause:** point at the highlighted line, one beat.\n")
            continue
        body.append("**Say — Essential:**")
        body.extend("- %s" % p for p in ess)
        body.append("")
        if opt:
            body.append("**Say — Optional:**")
            body.extend("- %s" % p for p in opt)
            body.append("")
        if note or extra:
            body.append("**Presenter Notes:**")
            body.extend("- %s" % p for p in note)
            body.extend("- %s" % p for p in extra)
            body.append("")

    OUT.write_text("\n".join(body).rstrip() + "\n", encoding="utf-8",
                   newline="\n")
    print("wrote %s (%d slides + %d appendix)" % (OUT.name, count - napp, napp))
    print("essential: %d words, %s | + optional: %d words, %s | demo %s"
          % (e_words, mmss(e_time), eo_words, mmss(eo_time), mmss(DEMO_S)))
    for r in rows:
        print(r)


if __name__ == "__main__":
    main()
