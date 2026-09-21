# WireGuard for Apache NuttX — Community Over Code (Glasgow) talk outline

Production deck skeleton. **English, ~20–30 min, ~30 slides.** Built by
deepening `short-slides.html` and folding in the newer kernel-migration
work and the debugging war stories from `slides.html`.

> **Source of truth:** the built deck is `coc-glasgow-slides.html`; this
> file is its outline. Both must keep these corrections (from the Codex
> reviews on issues #11/#12) and must not regress on regeneration:
> (a) the detached-thread pitfall is *my misconception*, not a NuttX POSIX
> violation; (b) the private key is write-only only in that *the get ioctl
> never returns it* — it still lives in the user-side config file, so
> "never leaves the kernel" is wrong; (c) real-hardware runs were the
> **apps v0.1.1 (FLAT)** version, the **kernel driver** is verified on sim +
> rv-virt (BUILD_KERNEL) with hardware still ahead — never conflate them;
> (d) of the two bugs, only the chachapoly nonce is a pre-existing NuttX
> bug; the stack overflow is in the new driver.

- Author line: **Satoru Akita / Sony Semiconductor Solutions**
- Spine of the talk: **"shallow tests pass, deep tests fail."** Every hard
  bug in this project had that same shape — including two found by pushing
  the port into the kernel, one of them a bug in NuttX's own tree.
- Do **not** frame this as a GSoC project.

Legend for each slide: **on-slide content** as bullets, then *note:* = what
to say / why the slide exists. `[reuse]` = adapt from short/slides.html,
`[NEW]` = new material (kernel migration).

---

## Act 1 — Context and the port (~8 min)

### 1. Title `[reuse]`
- **WireGuard for Apache NuttX**
- A WireGuard VPN implemented as a `wg0` network device.
- Satoru Akita / Sony Semiconductor Solutions · Community Over Code, Glasgow
- *note:* one line of who/what. Keep it clean.

### 2. Hero / proof up front `[reuse+update]`
- Badges: **ESP32-S3 tunnel up over real Wi-Fi** · **Spresense (GS2200M) up over real Wi-Fi** · peers = **Linux kernel + Windows client** · **NuttX 13.0.1 / master / 12.7.0, one source** · **also runs in a real kernel build (rv-virt knetnsh64)**
- *note:* lead with evidence. "This isn't a prototype — it tunnels to stock WireGuard on real hardware, and it runs inside a NuttX kernel build."

### 3. Summary / architecture `[reuse]`
- One diagram: App/NSH → NuttX BSD socket stack → `wg0` (virtual) alongside `eth0/wlan0` (physical NIC).
- Packets routed to `wg0` are encrypted and sent as UDP; UDP in is decrypted and delivered as if received on `wg0`.
- *note:* the mental model in 20 seconds. Everything else hangs off this picture.

### 4. The ASF and Community Over Code `[reuse]`
- Apache NuttX is an ASF project. ASF: 501(c)(3), founded 1999, "software for the public good," run by volunteers across hundreds of projects.
- The Apache Way — *community over code*.
- *note:* why this room. Sets up the closing (contributing back is the point, not just the code).

### 5. Why this topic — the problem `[reuse]`
- I wanted secure remote access to the NuttX devices I work with. NuttX has **no native, lightweight VPN**.
- Without one: expose a global IP / build a bespoke protocol / accept a vendor cloud — each unappealing.
- Real across edge AI, industrial IoT, satellite/remote.
- *note:* concrete pain. Name the device classes (AITRIOS edge cameras, ESP32+NuttX).

### 6. Why it matters — what changes `[reuse from slides.html s7]`
- It's not that you can do new things — it's that **the ordinary way now works**: ssh/telnet into a field device, pull logs, update firmware, with your usual tools.
- Pull the cloud into the device's network, not the other way around. Zero-trust reaches the MCU. Interop with *any* WireGuard peer — not a bespoke protocol.
- *note:* the value framing. This is the "so what."

### 7. WireGuard in one slide `[reuse from slides.html s5]`
- Modern VPN, merged into Linux 5.6. **Small on purpose**: fixed crypto suite (Noise / Curve25519 / ChaCha20-Poly1305 / BLAKE2s), no negotiation, single UDP port (NAT-friendly).
- Small implementation = small attack surface. A good fit for an MCU.
- *note:* why WireGuard specifically, and why it's tractable to port.

### 8. The plan — don't write from scratch `[reuse]`
- Port `smartalock/wireguard-lwip`: it implements WireGuard as an lwIP netif and **isolates all OS-specific behaviour behind `wireguard-platform.h` — four functions**. Protocol core + crypto are portable C.
- BSD-licensed → consistent with NuttX's licensing.
- *note:* the strategy. Keep the protocol core; replace the network glue.

### 9. Design decision — NuttX netdev, not an lwIP netif `[reuse from slides.html s9]`
- NuttX has its own TCP/IP stack (not lwIP). Map the abstractions: `netif`→`net_driver_s`, `pbuf`→`iob`, `netif_add()`→`netdev_register()`, callback→`devif_poll()`.
- `wg0` is a `NET_LL_TUN` netdev; its "wire" is a UDP socket.
- *note:* the single most important porting decision. Sets up the data path.

### 10. What shipped first + how much code `[reuse s7/s11/s3]`
- Layer table: protocol+crypto **3,079 lines byte-identical to upstream**; OS hooks = **four functions**; the glue is the netdev + UDP wiring (~3,000 lines ported).
- Runtime config: `wg genkey` / `wg set` / `wg setconf` — no rebuild to change keys or peers.
- *note:* the v1 (apps-side) result. "It worked." Then the turn: *but working was the easy part.*

---

## Act 2 — "Shallow tests pass, deep tests fail" (~8 min) `[reuse slides.html Part 2]`

### 11. The thesis `[reuse s14]`
- Every hard bug in this project had the **same shape**: a shallow test passes, a deeper one fails — and the passing shallow test sends you looking in the wrong place for hours.
- *note:* this is the heart of the talk. Say it plainly; the next slides are five (then two more) instances.

### 12. Pitfall — a return code of 0 that did nothing `[reuse s15]`
- `setsockopt(SO_RCVTIMEO)` returns 0 (success), but `recvfrom()` blocks forever.
- Perfect chicken-and-egg: timer never fires → no initiation → peer stays silent → `recvfrom()` never returns.
- *note:* first taste of "success return, no effect."

### 13. Pitfall — the detached thread that didn't survive `[reuse s16]`
- RX thread started with `pthread_create()` + `pthread_detach()`. When the `wg` command exits, the thread stops — not even in `ps`.
- A `usleep()` right after create makes its `printf` appear → it *was* created.
- *note:* my misconception — I read `detach` as "outlives the command." `detach` only controls when the thread's resources are reclaimed, not whether it survives its launcher exiting. Not a NuttX POSIX violation. Foreshadows the kernel redesign, which owns the thread in the driver.

### 14. Pitfall — ping works, TCP dies → `EBADF` `[reuse s17–20]`
- Same tunnel: ping is 0% loss, telnet's 3-way handshake completes, then the connection dies.
- Instrumented TX: `psock_sendto ret=-1 errno=9` (**EBADF**). Sends that worked ran in the `wg_rx` task's own context; sends that failed came from another task.
- Cause: **fds are scoped to the task group, not global.** Fix: use the fd-less internal API — `psock_socket/bind/sendto/recvfrom` on a `struct socket` (just memory, usable from any task).
- *note:* the best debugging story. This is also *why the kernel version holds the socket as a `struct socket`* — bridge to Act 3.

### 15. Pitfall — a 50 ms "no big deal" that was 10× `[reuse s21]`
- The fd-less fix broke `poll()` (also fd-based). Stopgap: poll every 50 ms. Assumed: "just wakes the CPU when idle."
- Wrong — packets waited up to 50 ms, capping **throughput 10×**. Fixed by hooking `psock_poll()` + a semaphore = real blocking wait.
- *note:* "measured 'no impact' and it was 10×." Measurement beats intuition.

### 16. Pitfall — sim passes, hardware freezes `[reuse s22–23]`
- Config persistence: `wg showconf > /data/wg0.conf` froze the board — only on the SPIFFS of real hardware, not sim.
- Root cause: the **RX task stack**. `wg_rx` calls `ipv4_input()` on its own stack — the whole TCP depth rides on it, plus reply encryption. The default 3072 was a number nobody had measured. Verified with `CONFIG_STACK_COLORATION` + `ps`.
- *note:* stacks you didn't measure. Remember this — it comes back in the kernel.

---

## Act 3 — Going deeper: into the kernel (~6 min) `[NEW]`

### 17. Why go further — the limits of the apps version `[NEW]`
- v1 lives in `apps/` and leans on `psock_*` internals — fine for a FLAT build, but it can't be a first-class in-tree driver and doesn't fit PROTECTED/KERNEL builds where apps and kernel are separated.
- To upstream it properly: move the device **into the kernel** — `drivers/net/wireguard/` — and drive it from a small user-space `wg` command over **ioctl**.
- *note:* the additional development, and why. This is what turns a demo into a mergeable contribution.

### 18. The kernel design `[NEW]`
- `wg0` is now a `netdev_lowerhalf` device in the kernel: UDP socket + a **RX kernel thread**, timers, cookie handling.
- A **flat, pointer-free ioctl ABI** (`include/nuttx/net/wireguard.h`): `SIOCS/GWGIF`, `SIOCS/D/GWGPEER`. Pointer-free because in PROTECTED/KERNEL the kernel copies the caller's struct directly.
- **Private key is write-only** — the get ioctl never returns the private key (it still lives in the config file on the user side, so "never leaves the kernel" is too strong).
- Kernel-side crypto is NuttX's own `crypto/` (BLAKE2s / ChaCha20-Poly1305 / Curve25519) — nothing vendored on the kernel side; the only vendored code is the user-space MIT `wg_x25519.c` for offline genkey/pubkey.
- *note:* the shape of the real driver. Contrast with v1: socket now *kernel-owned* (recall Pitfall 3), lifecycle owned by the driver (recall Pitfall 2).

### 19. Two more bugs — the same shape `[NEW]`  ← key slide
- **(a) A bug in NuttX's own crypto.** `chacha20poly1305` (u64 nonce) placed the counter in the wrong bytes. Counter 0 matches, so **the handshake succeeds and every data packet after the first fails** — shallow passes, deep fails. No in-tree caller had ever exercised it. Two-line fix; going upstream as a separate `crypto:` PR.
- **(b) A BUILD_KERNEL-only crash.** `wg_set_if()` snapshotted peers into a **6 KB array on the stack**; the kernel stack is 3 KB → overflow → heap corruption → panic. **sim (FLAT, big stacks) never hit it.** Moved to the heap.
- *note:* the punchline. Same thesis, now at kernel depth — and porting into the project **found and fixed a latent bug in the project itself.** Great for this audience.

### 20. Verification — be precise about which implementation `[NEW+reuse s12]`
- ① **kernel driver, local build** — `sim:wireguard` defconfig builds clean (the config CI compiles).
- ② **kernel driver, BUILD_KERNEL** — `rv-virt:knetnsh64` (virtio-net) in QEMU, bidirectional tunnel to Linux kernel WireGuard, `wg` a **separate ELF** across the syscall boundary.
- ③ **apps v0.1.1 (FLAT), real hardware** — ESP32-S3 & Spresense over real Wi-Fi, Linux/Windows peers, telnet/HTTP/7 MB/rekey/power-cycle. *This was the apps version, not the kernel driver.*
- Remaining: the **kernel driver on real hardware**.
- *note:* the credibility slide — but keep the implementations distinct. Hardware runs were the apps/FLAT version; the kernel driver is proven on sim + rv-virt, hardware still ahead.

---

## Act 4 — From demo to contribution (~5 min)

### 21. Demo `[reuse s13/short s10]`
- Live/video: telnet into the board **through the tunnel** and run commands; start a web server on the board and open it in a browser. USB unplugged — board on a power adapter. Peer = official Windows WireGuard client over home Wi-Fi.
- Link: youtu.be/1kyX2av5WG4
- *note:* keep it short; the point is "it's an ordinary host at the other end of the tunnel."

### 22. Production readiness `[reuse s24–25]`
- Before: keys baked into Kconfig, rebuild to change anything. After: runtime `genkey/set/setconf`, keys never in the build artifact.
- Long-run monitoring: sample four independent signals every minute — **ROUTER / LAN / TUN / TCP** — so when it stops you can say *whose* fault it is.
- *note:* "demo" → "operable." Shows engineering maturity.

### 23. The portability bet paid off `[reuse s26]`
- "Isolate the OS in four functions" — checked with the **apps (FLAT) implementation**: x86_64 (sim), ARM Cortex-A7 (QEMU), Xtensa LX7 (ESP32-S3), ARM Cortex-M4F (Spresense) — **no code change** to move across them. The kernel driver adds sim + rv-virt (BUILD_KERNEL) on top.
- *note:* the design decision from slide 8/9 validated across 4 architectures — with the apps version; label it so, don't conflate with the kernel driver.

### 24. Contributing back — The Apache Way `[NEW+reuse s27]`
- Upstreaming plan: a small `crypto:` PR (the nonce fix) first, then the driver PR (`net/wireguard`), then the `apps/system/wg` PR; design shared on `dev@nuttx.apache.org`.
- checkpatch/nxstyle clean; vendored source kept byte-identical and licensed; PR-shaped directory layout.
- *note:* close the loop with the room's values — community over code. The contribution includes bugs fixed *for* the project.

### 25. Takeaways `[reuse s28]`
- **Shallow tests pass, deep tests fail** — design your tests for depth (sustained data, real hardware, a real kernel build), not just "it handshook."
- A clean OS-abstraction bet pays off — 4 architectures (apps/FLAT), no changes.
- **Porting into a project can improve the project** — a latent bug in NuttX's own crypto, found and fixed on the way in (the upstream `crypto:` PR); plus a BUILD_KERNEL bug in my own driver that only a kernel build surfaced. Count them precisely: one pre-existing NuttX bug, one in the new driver.
- *note:* three things to carry out of the room. End on the contribution.

### 26. Thanks / links
- Repo, YouTube demo, contact. Q&A.
- *note:* leave links up during Q&A.

---

## Notes for building the deck

- **Optional splits** if you want to hit ~30 slides / fill 30 min: split slide 14 (EBADF) into symptom / cause / fix (as in slides.html), and slide 19 into one slide per bug. That takes you to ~30.
- **Trim path to ~20 min:** collapse Act 2 to three pitfalls (13, 14, 16), keep all of Act 3 — the kernel work and the two bugs are the new, differentiating material.
- **Visuals to reuse:** the `wg0` architecture diagram (short s3), the TX/RX data-path diagram (slides.html s10), the verification matrix (s12), the fd-table diagram (s19), the stack-measurement numbers (s23).
- **One narrative thread to keep visible:** the socket/lifecycle problems in Act 2 are exactly what the kernel design in Act 3 fixes structurally — call that back explicitly on slide 18.
