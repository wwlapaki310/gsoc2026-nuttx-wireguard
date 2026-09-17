# Talk script: short-slides.html (9 slides) — English delivery

English delivery script for all 9 slides of [`short-slides.html`](../short-slides.html). It is the
same deck as the Japanese script [`short-slides.md`](short-slides.md): same slide order, same cut
list, same claims — only the spoken language differs. The slide text is already English, and the
speaker notes inside the deck (`N` key) are English too, but they are terse; read this instead.
PDF: [`short-slides.pdf`](../short-slides.pdf).

The deck is the 8-slide Returns deck ([`returns-slides.html`](../returns-slides.html) /
[`returns-slides.md`](returns-slides.md)) plus **the SPRESENSE hardware result** and
**verification across three NuttX versions (13.0.1 / master / 12.7.0)**.

The full 27-slide deck's script is [`slides.md`](slides.md); the Q&A is in
[`presentation-script.md`](presentation-script.md) and at the end of [`returns-slides.md`](returns-slides.md).

---

## Timing

English delivery runs at roughly **140 words/min**. Reading every line:

| | Speaking | Video | Total |
|---|---|---|---|
| All 9 slides | ~9 min | +1–2 min | **~10–11 min** |

### Routes by slot

| Slot | What to do |
|---|---|
| **10–12 min** | Full deck. Use the 60-second video cut |
| **8 min** | Compress 3 (ASF) to 20 seconds; take 5 (the plan) and 6 (the shape) at one slide's pace |
| **5 min** | Only 1 → 2 → 4 → 6 → 8 → 9. Drop 3, 5, 7. Video becomes a still, 30 seconds |

**Never drop:** `4` (why this topic) and `8` (where it runs). Without 4 this is just a porting
report; without 8 you cannot answer "does it really work?". `6 → 7` is one set — shape, then
detail — but in a 5-minute slot drop 7 and keep 6.

---

## Legend

- **[SLIDE]** — what is on screen (for your own reference; do not read it out)
- **[SAY]** — the spoken line. Written to be read as is
- **[BEAT]** — pauses, key presses, where to look
- Second counts assume 140 words/min

---
---

## 01 TITLE 〔45 s〕

**[SLIDE]** WireGuard for Apache NuttX / three tiles on the right: NuttX logo, AITRIOS camera, SPRESENSE / achievement strip along the bottom, four items (ESP32-S3, SPRESENSE, real WireGuard peers, NuttX 13.0.1 / master / 12.7.0)

**[SAY]**
Today I want to talk about implementing WireGuard for Apache NuttX. It shows up as a network device called `wg0`.

The tunnel comes up over real Wi-Fi on two boards: an ESP32-S3, and a SPRESENSE with a Wi-Fi add-on. On both boards, telnet and a web server run through the tunnel.

**[BEAT]** — point at the strip along the bottom.

The peer at the other end is always a real WireGuard implementation: the Linux kernel module, and the official Windows client. Talking to my own code would prove nothing about interoperability.

And the same source runs on three NuttX versions — the current release 13.0.1, development master, and the older 12.7.0.

---

## 02 SUMMARY 〔80 s〕

**[SLIDE]** the diagram on NuttX at the top (`wg0` → UDP 51820 → existing WireGuard peer), then a "What is WireGuard?" heading over four explainer cards

**[SAY]**
First, what is actually in this repository.

Apache NuttX is a POSIX-compliant RTOS with its own TCP/IP stack and BSD sockets, but no VPN. So WireGuard is implemented as a NuttX network device, `wg0`. To an application it is an ordinary network interface. What carries the encrypted payload underneath is a UDP socket on port 51820, and the peer can be any existing WireGuard endpoint.

**[BEAT]** — down to the cards.

So what is WireGuard itself. It is a lightweight VPN protocol, originally written for Linux, and it is increasingly used in embedded and IoT work. It builds an encrypted tunnel on top of UDP, with Curve25519, ChaCha20-Poly1305 and BLAKE2s. The implementation is about four thousand lines — compact enough to fit on a microcontroller.

It is newer than people expect, and it is not a big-company product. It was written by Jason Donenfeld, an independent security researcher, who started in 2015 and published in 2016. It lived as an out-of-tree module for four years and only landed in the Linux kernel in March 2020, in 5.6. If you use Tailscale, you are already using WireGuard — Tailscale is built on this protocol.

---

## 03 THE ASF AND COMMUNITY OVER CODE 〔55 s〕

**[SLIDE]** what the ASF is / Community Over Code 2026 (Glasgow 11–14 Oct, Sydney in November) / NuttX International Workshop

**[SAY]**
Before the technical part, a short word on where this work is going.

The Apache Software Foundation is a non-profit founded in 1999, and it exists to provide software for the public good. It is run almost entirely by volunteers, it supports several hundred projects, and it is the steward of the Apache License 2.0. Apache NuttX is one of those projects, and the goal here is to get this code merged into `apache/nuttx-apps`.

Community Over Code is the ASF's official conference. In 2026 it runs in Glasgow in October and in Sydney in November. The NuttX International Workshop is co-located with Glasgow, and I submitted to it. The name of the conference is the ASF's principle itself: community over code.

**[BEAT]** In an 8-minute slot, compress this to one sentence: it is an ASF project, the goal is an upstream merge, and I have submitted to the workshop in Glasgow in October.

---

## 04 WHY THIS TOPIC 〔85 s〕

**[SLIDE]** two settings (the AITRIOS AI camera = ESP32 + NuttX / the SPRESENSE satellite project) / the three options when you have no VPN / small GSoC footnote along the bottom

**[SAY]**
Why this topic. I did not pick it off a list — it came out of my own work.

I am an edge AI engineer at Sony Semiconductor Solutions, and I use NuttX from the application side. One setting is AITRIOS, an edge AI platform. The AI camera we use there runs on an ESP32, and its OS is NuttX. The other is a bottom-up activity: a satellite project built around SPRESENSE. SPRESENSE is Sony's small, low-power board, with a track record in satellites and ocean monitoring.

**[BEAT]** — one beat.

Both were NuttX devices I wanted to reach securely from elsewhere, and could not. Without a VPN the options are: expose a global IP, build your own protocol, or move onto a vendor cloud. None of those is appealing.

WireGuard is the protocol Tailscale uses, and I am a heavy Tailscale user myself. A compact implementation and a simple key model make it a good fit for a constrained environment like this.

One footnote, in small print. The topic itself came from the Google Summer of Code idea list, and that proposal was not accepted. I kept working on it anyway.

---

## 05 THE PLAN 〔70 s〕

**[SLIDE]** the three layers of smartalock/wireguard-lwip (protocol + crypto / the four-function platform layer / lwIP netif glue) / the replacement table (`struct netif` → `struct net_driver_s`, `pbuf_alloc()` → `iob_alloc()`, lwIP UDP → BSD sockets)

**[SAY]**
Now the technical part, starting with the plan. I did not write WireGuard from scratch — there is a reference implementation.

`smartalock/wireguard-lwip` implements WireGuard as an lwIP `netif`, that is, a virtual NIC. It is treated exactly like `eth0` or `wlan0`, so the stack above sees an ordinary interface and routing just works. The good thing about that implementation is that everything OS-specific is isolated behind four functions. The protocol core and the crypto are portable, OS-independent C.

The problem is that NuttX does not use lwIP. NuttX has its own TCP/IP stack, and the lwIP headers are not even on the include path. So the netif glue does not compile at all.

That gave me the plan: implement the four platform functions for NuttX, and replace the lwIP API calls one at a time with their NuttX equivalents. That is what the proposal said, and that is how the work actually went.

---

## 06 HOW IT WAS BUILT 〔55 s〕

**[SLIDE]** the three layers with line counts (3,079 lines byte-identical / 186 lines / 2,157 lines) / four architectures (x86_64, Cortex-A7, Xtensa LX7, Cortex-M4F)

**[SAY]**
So how did it go. If you split the source into three layers, the porting cost per layer is completely different.

The protocol and the crypto cost nothing. Portable C, taken as is: 3,079 lines, byte-identical to upstream. The four OS-dependent functions were cheap — 186 lines against the NuttX POSIX API. The lwIP netif glue was a total loss. Nothing could be reused; it had to be rewritten. That is the 2,157 lines written for NuttX.

**[BEAT]** — point at the four architectures, bottom right.

And isolating the OS dependency into that one small file paid off. The result runs on four architectures — x86_64, ARM Cortex-A7, Xtensa LX7 and ARM Cortex-M4F — without changing a single line.

---

## 07 THE DETAILS 〔80 s〕

**[SLIDE]** `wg0` = a netdev registered with `netdev_register()`, wired up by a UDP socket / the transmit and receive data paths / the four OS hooks / "the one place the plan did not survive" = `psock_*()` and the FLAT build

**[SAY]**
One level of detail further.

Because NuttX has its own TCP/IP stack, `wg0` is registered with `netdev_register()`. I followed the NuttX TUN driver as the model. The thing doing the wiring underneath is a UDP socket. On transmit, `devif_poll` hands the packet over, it gets encrypted, and it goes out with `psock_sendto`. On receive, a background task blocks in `psock_poll`, decrypts, and injects into the stack with `ipv4_input`.

The four OS hooks are all there is: a monotonic clock, cryptographic random bytes, a TAI64N timestamp, and an under-load check. The under-load check just returns false.

**[BEAT]** — one beat. This is the part that matters.

Finally, the one place where the plan did not survive. The proposal said BSD `socket()` and `bind()`. In practice the socket is held as a `struct socket` rather than a file descriptor, and the internal `psock` APIs are used instead. NuttX scopes file descriptors per task group, while the transmit path runs on an unrelated worker thread. Because of that, the implementation currently assumes a FLAT build, and that is one of the open questions to settle before it goes upstream.

---

## 08 VERIFIED ON 〔85 s〕

**[SLIDE]** a photo of the two boards, top right / a three-row table (sim / QEMU, ESP32-S3, SPRESENSE — architecture, Wi-Fi approach, peer, what was confirmed) / claim: same source on 13.0.1, master and 12.7.0, both boards / "three real bugs"

**[SAY]**
Where does it actually run.

For the simulator and QEMU, the peer is the Linux kernel's WireGuard. The two physical boards talk to the official Windows client across a home access point.

**[BEAT]** — point at the SPRESENSE row.

The SPRESENSE result is the newer one. The Wi-Fi add-on uses a GS2200M module, which keeps the TCP/IP stack inside the module — from the kernel's point of view it appears as usrsock, a socket proxy. That is a completely different underside from the in-kernel netdev on the ESP32-S3, and the same `wg0` code runs on both. Getting there meant hitting three real bugs, and all three are fixed.

**[BEAT]** — down to the claim.

And the same source builds on three NuttX versions: the current release 13.0.1, master — which is what an upstream submission is measured against — and the older 12.7.0. Both boards have been re-verified on all three. On 13.0.1 and master, one scheduler-side fix was needed for a startup failure found on SPRESENSE, and that is written up to be reported upstream. On the ESP32-S3 side it was only following defconfig changes: a different init system and a smaller default stack.

---

## 09 THE DEMO 〔60 s + video〕

**[SLIDE]** video thumbnail (left: the Windows WireGuard client / right: telnet into the ESP32-S3) / youtu.be/1kyX2av5WG4 / "the same flow runs on SPRESENSE"

**[SAY]**
Finally, what it actually looks like.

On the left is the official Windows WireGuard client, showing the handshake and the transfer counters. On the right is a telnet session into the ESP32-S3 through the tunnel. `ifconfig` shows `wlan0` and `wg0` side by side, `wg show` has the handshake, and `ps` shows the tasks.

**[BEAT]** — play the video. For the 60-second cut: telnet login → `webserver &` → open it in a browser.

In the video I log in over the tunnel with telnet, run a few commands, start a web server on the board, and finally open it from a browser. There is no USB attached the whole time. Power adapter only, over home Wi-Fi, with the official Windows client at the other end.

After that recording, I ran the same flow on the SPRESENSE with the Wi-Fi add-on. There is a separate tunnel to each board, so both can be used from two terminals at once.

**[BEAT]** — end here. Close with "that's all from me" and go to questions.

---
---

## Likely questions (specific to this deck)

In addition to the table at the end of [`returns-slides.md`](returns-slides.md):

| Q | A |
|---|---|
| SPRESENSE has no Wi-Fi on board, does it? | It does not. There is a Wi-Fi add-on, the iS110B (GS2200M). The driver uses the usrsock approach, and the TCP/IP stack lives in the module |
| What were the three bugs? | (1) usrsock intercepted ioctls meant for `wg0`, so the handshake never started → fixed by configuring the netdev directly. (2) Symbol collision between NuttX's own crypto and the vendored crypto → namespaced with a `wg_` prefix. (3) On NuttX 13.0.1 / master, cxd56 would not boot because of a `CONFIG_RTC_HIRES` circular dependency → a one-place fix on the sched side, to be reported upstream — [phase4-log.md](../../development/phase4-log.md), 2026-09-16 / 17 |
| Why verify on three versions? | Upstream PRs go against master. Users run the most recent release (13.0.1). 12.7.0 was the version originally pinned, and it shows the same steps still work |
| What differs between 12.7.0 and 13.0.1? | The WireGuard sources are identical. The Dockerfile absorbs the NuttX-side differences (esp32s3 init system, default stack, SPIFFS format; the cxd56 boot regression) |
| Can both boards be used at once? | Yes. There are two tunnels on the Windows side — 10.10.0.0/24 and 10.11.0.0/24, ports 51820 and 51821 |
