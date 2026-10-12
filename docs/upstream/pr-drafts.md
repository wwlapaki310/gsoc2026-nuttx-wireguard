# Upstream PR description drafts (English)

Ready-to-paste PR bodies, laid out exactly as the official PR template
(apache/nuttx `CONTRIBUTING.md` §2.3 and `.github/PULL_REQUEST_TEMPLATE.md`,
read at upstream `4bc1ab2fd7` on 2026-10-12): **Summary / Impact / Testing /
PR verification Self-Check**. These are drafts: the author opens the PRs.

Last updated: 2026-10-12. Branches, SHAs, CI reproduction and the rules each
section answers are in [submission-checklist.md](submission-checklist.md)
§6 「公式ルールとの照合 (2026-10-12)」.

How to use:

- Every `<...>` is a placeholder the author fills in. `<HW LOG>` blocks are
  required (CONTRIBUTING 1.3 / 1.7.2 / 1.9: build **and runtime logs on at
  least one real hardware target**; sim/QEMU do not count). No hardware log
  has been taken on the 2026-10-12 SHAs yet.
- The PR title is the first commit's topic (CONTRIBUTING 2.2). Keep the
  `area: Capitalised context.` form.
- Dependencies are declared with **line-anchored `depends-on:` lines outside
  any code block** (`.github/scripts/depends_on.py`). CI cherry-picks the named
  PRs before building. `#1234` or "Depends on #1234" is **not** parsed.
  Use `depends-on: apache/nuttx/pull/<N>` or the full URL.
- The AI disclosure paragraph at the end of each Summary is **optional**.
  The mandatory disclosure is the `Assisted-by:` trailer on every commit
  (CONTRIBUTING 1.5). The paragraph is offered because the ASF guidance asks
  contributors to indicate tool use. The author decides whether to include it
  and keeps it identical to the trailers: `Assisted-by: Claude Code:claude-opus-5-5`
  (the speaker reported "Claude Opus 5.5", not yet confirmed; records also mention
  Codex, so add `Assisted-by: Codex:<MODEL_VERSION>` and name it here if it was used).
- Do not paste `.config`, keys, PSKs, Wi-Fi credentials or public endpoint
  addresses into any log (checklist §3 (b)).

Order and dependencies (merge-strategy.md §2):
PR-C → (dev@) → PR-K1 ; PR-N ; then PR-A (nuttx-apps) paired with PR-D.

---

## PR-C — `crypto/chachapoly: Place the 64-bit nonce in the last eight bytes.`

Target: apache/nuttx, branch `pr-latest/crypto-chachapoly` (2 commits).

```markdown
## Summary

  * Why: chacha20poly1305_encrypt()/_decrypt() expand their 64-bit counter
    nonce into the 96-bit IETF ChaCha20 nonce by copying it into bytes 0..7.
    RFC 8439 and WireGuard put it in bytes 4..11 with the first four bytes
    zero (wireguard-go: PutUint64(nonce[4:], counter)). The two layouts agree
    only for counter 0, so a standard peer accepts the first message of a
    session and rejects every later one.
  * What: crypto/chachapoly.c (the fix) and crypto/testmngr.c/.h (new
    known-answer tests).
  * How: copy the counter to le_nonce_array + 4. xchacha20poly1305_*() pass
    their 64-bit nonce remainder to the same functions and are fixed by the
    same change. A second commit adds ChaCha20-Poly1305 vectors for
    counters 0, 1 and 2 (a misplaced nonce fails from counter 1), a
    decryption round trip, a forged-tag rejection, and an
    XChaCha20-Poly1305 vector that also covers HChaCha20. They run from
    crypto_test() under CONFIG_CRYPTO_ALGTEST.
  * Related: needed by the WireGuard network device (a later PR). No issue.
  * <optional> Generative AI tooling (Claude Code, claude-opus-5-5) assisted
    with this change; every commit carries an Assisted-by: trailer. I have
    reviewed and tested all of it and take responsibility for it.

## Impact

  * Is new feature added? Is existing feature changed? YES: the output of
    chacha20poly1305_*() and xchacha20poly1305_*() changes for every nonce
    other than 0, to the RFC 8439 layout. New KATs under CRYPTO_ALGTEST.
  * Impact on user (will user need to adapt to change)? NO. There is no
    in-tree caller of these functions.
  * Impact on build (will build process change)? NO.
  * Impact on hardware (will arch(s) / board(s) / driver(s) change)? NO.
  * Impact on documentation (is update required / provided)? NO.
  * Impact on security (any sort of implications)? YES: makes the AEAD
    interoperate with standard implementations. No key handling changes.
  * Impact on compatibility (backward/forward/interoperability)? YES: data
    sealed by the old code with a nonzero counter no longer opens. No
    in-tree user is known to have stored any.
  * Anything else to consider or add? No.

## Testing

  I confirm that changes are verified on local setup and works as intended:
  * Build Host(s): Linux x86_64 (Ubuntu 24.04 container on Windows 11), GCC 13.3.
  * Target(s): sim:nsh + CONFIG_CRYPTO + CONFIG_CRYPTO_ALGTEST; <HW board:config>.

  Testing logs before change (testmngr vectors on the old chachapoly.c, sim):

  ```
  test_chacha20poly1305: ERROR: Failed chacha20poly1305 encrypt #1
  crypto test failed
  ```

  Testing logs after change:

  ```
  sim: up_cryptoinitialize: crypto test OK
  <HW LOG: board, NuttX SHA, boot log with "crypto test OK">
  ```

## PR verification Self-Check

  * [x] This PR introduces only one functional change.
  * [x] I have updated all required description fields above.
  * [x] My PR adheres to Contributing [Guidelines](https://github.com/apache/nuttx/blob/master/CONTRIBUTING.md) and [Documentation](https://nuttx.apache.org/docs/latest/contributing/index.html) (git commit title and message, coding standard, etc).
  * [ ] My PR is still work in progress (not ready for review).
  * [x] My PR is ready for review and can be safely merged into a codebase.
```

---

## PR-N — `tools/nxstyle: Skip the vendored X25519 in system/wg.`

Target: apache/nuttx, branch `pr-latest/nxstyle-wg` (1 commit).

```markdown
## Summary

  * Why: the nuttx-apps system/wg command (apache/nuttx-apps PR to follow)
    carries an X25519 implementation taken from the STROBE project under the
    MIT license. Its algorithm body keeps the upstream formatting so that it
    can still be diffed against the STROBE source. nuttx-apps CI runs the
    nxstyle of nuttx master, which rejects that formatting.
  * What: tools/nxstyle.c, g_white_files[].
  * How: add wg_x25519.c and wg_x25519.h to g_white_files[], the way other
    vendored sources are listed.
  * Related: apache/nuttx-apps#<PR-A> (needs this to pass its check job).
  * <optional> AI disclosure paragraph (see top of file).

## Impact

  * Is new feature added? Is existing feature changed? NO.
  * Impact on user? NO.
  * Impact on build? NO (style checker only).
  * Impact on hardware? NO.
  * Impact on documentation? NO.
  * Impact on security? NO.
  * Impact on compatibility? NO.
  * Anything else to consider or add? Only these two file names are skipped.

## Testing

  I confirm that changes are verified on local setup and works as intended:
  * Build Host(s): Linux x86_64 (Ubuntu 24.04 container), GCC 13.3.
  * Target(s): none (host tool).

  Testing logs before change (nuttx master checkpatch on the apps PR):

  ```
  /opt/apps/system/wg/wg_x25519.c:105:2: error: Left bracket not on separate line
  ...
  ```

  Testing logs after change:

  ```
  checkpatch.sh -c -u -m -g on the apps PR: no file errors
  ```

## PR verification Self-Check

  (same five boxes as PR-C)
```

---

## PR-K1 — `drivers/net/wireguard: Add the WireGuard device.`

Target: apache/nuttx, branch `pr-latest/wireguard-driver` (6 commits on top of
PR-C). Open it after PR-C is merged and the branch is rebased; until then, add
`depends-on: apache/nuttx/pull/<PR-C>` so CI builds with the fix. Use the
title of the device commit, not of the first (whitespace-only) commit.

```markdown
## Summary

  * Why: NuttX has no VPN. This adds WireGuard as a virtual network device,
    wg0, so a NuttX device can join a WireGuard network with any standard
    peer (Linux kernel module, wireguard-go, boringtun, another NuttX).
  * What: drivers/net/wireguard/ (new), drivers/net/netdev_upperhalf.c,
    net/netdev/netdev_ioctl.c and netdev_register.c, include/nuttx/net/
    ioctl.h and wireguard.h (new ABI), net/Kconfig, mm/iob/Kconfig,
    drivers/drivers_initialize.c, LICENSE, boards/sim (sim:wireguard),
    Documentation/os/drivers/special/net/wireguard.rst.
  * How: wg0 is a NET_LL_TUN netdev_lowerhalf driver. It owns a kernel UDP
    socket and an RX kernel thread; outbound datagrams are queued as
    immutable buffers and sent only outside the device lock, so it works on
    buffered UDP, unbuffered UDP and usrsock. Keys and peers are set at run
    time through five flat, pointer-free ioctls (0x0046..0x004a); the
    private key is write-only. Cryptography is NuttX's crypto/ (BLAKE2s,
    ChaCha20-Poly1305, Curve25519). The protocol core (wg_noise.c:
    Noise_IKpsk2, key schedule, anti-replay, cookies) is derived from
    wireguard-lwip under BSD-3-Clause, hence ALLOW_BSD_COMPONENTS and the
    LICENSE entry.
  * Commits: (1) whitespace-only nxstyle fixes in the two netdev files the
    later commits touch; (2) netdev_upperhalf passes NET_LL_TUN packets to
    ip_input() and skips the VLAN walk for non-Ethernet devices; (3) the
    ioctl ABI; (4) the device; (5) sim:wireguard, the configuration that
    compiles the driver in CI; (6) documentation. Each commit builds.
  * Bundling (CONTRIBUTING 1.7.9): the protocol core has no use and no way
    to be tested without the device, so they are one PR. This is not a
    breaking change: everything is under CONFIG_NET_WIREGUARD (default n).
  * Related: the userspace command is apache/nuttx-apps#<PR-A>, documented
    by #<PR-D>. Design thread: <dev@ archive URL>.
  * <optional> AI disclosure paragraph (see top of file).

depends-on: apache/nuttx/pull/<PR-C>

## Impact

  * Is new feature added? Is existing feature changed? YES: new
    CONFIG_NET_WIREGUARD device. Existing netdev_upperhalf behaviour changes
    only when CONFIG_NET_WIREGUARD is set.
  * Impact on user? NO for existing configurations.
  * Impact on build? YES: new Kconfig options; IOB_NCHAINS defaults to
    IOB_NBUFFERS when NET_WIREGUARD is enabled.
  * Impact on hardware? NO arch or board code changes (new sim config only).
  * Impact on documentation? YES: wireguard.rst added in this PR.
  * Impact on security? YES: adds an encrypted tunnel. The private key is
    write-only through the ioctl ABI; session keys are zeroed on down.
    Known limitation: TAI64N after a reboot on boards without reliable
    time (documented in wireguard.rst).
  * Impact on compatibility? NO. New ioctl numbers 0x0046..0x004a do not
    overlap existing ones (checked against <upstream SHA>).
  * Anything else to consider or add? BUILD_PROTECTED: builds and runs on
    rv-virt:pnsh64 (QEMU). There the configuration ioctls run on the wg
    task's user stack, so apache/nuttx-apps#<PR-A> sets a larger default
    wg stack for protected builds.

## Testing

  I confirm that changes are verified on local setup and works as intended:
  * Build Host(s): Linux x86_64 (Ubuntu 24.04 container on Windows 11), GCC 13.3, RISC-V GCC <ver>.
  * Target(s): sim:wireguard; rv-virt:knetnsh64 / knetnsh64_smp (BUILD_KERNEL);
    <ESP32-S3 board:config>; <Spresense board:config>.
  * Peer: Linux kernel WireGuard.

  Testing logs before change: not applicable (new device).

  Testing logs after change:

  ```
  <HW LOG ESP32-S3: SHA, uname -a, ifconfig wg0, wg show, ping both ways>
  <HW LOG Spresense: same; note the cxd56 RTC_HIRES patch used to boot>
  sim suite (verify-sim-wg-suite.sh): <result line>
  ```

## PR verification Self-Check

  (same five boxes as PR-C)
```

---

## PR-D — `Documentation: Add the system/wg command.`

Target: apache/nuttx, branch `pr-latest/doc-system-wg` (1 commit on top of
PR-K1). CONTRIBUTING 1.8: documentation for nuttx-apps code goes in a separate
nuttx PR. Open it together with PR-A and link the two. A Documentation-only PR
runs only the docs job (`doc.yml`; `build.yml` and `check.yml` ignore
`Documentation/**`), which builds the branch as it is and does not read
`depends-on:`. So PR-D must already contain PR-K1's `wireguard.rst`: open it
after PR-K1 is merged and the branch is rebased.

```markdown
## Summary

  * Why: documentation for the wg command added by apache/nuttx-apps#<PR-A>
    (CONTRIBUTING 1.8).
  * What: Documentation/applications/system/wg/index.rst (new) and a link
    from Documentation/os/drivers/special/net/wireguard.rst.
  * How: describes the configuration options, the subcommands, how the
    private key is kept in the configuration file, and an example session.
  * Related: apache/nuttx-apps#<PR-A>, #<PR-K1>.

## Impact

  * Is new feature added? Is existing feature changed? NO (documentation only).
  * Impact on user / build / hardware / security / compatibility? NO.
  * Impact on documentation? YES: new page.
  * Anything else to consider or add? No.

## Testing

  * Build Host(s): Linux x86_64, python 3.10, pipenv sync (Documentation/Pipfile.lock).
  * `make html` (SPHINXOPTS includes -W): <result line>

## PR verification Self-Check

  (same five boxes as PR-C)
```

---

## PR-A — `system/wg: Add a WireGuard configuration command.`

Target: apache/nuttx-apps, branch `pr-latest/system-wg` (2 commits). Its check
job uses the nxstyle of nuttx master, so PR-N must be merged first. Its build
job applies the `depends-on:` PRs.

```markdown
## Summary

  * Why: the WireGuard device (apache/nuttx#<PR-K1>) is configured through
    ioctls; this is its userspace client.
  * What: system/wg/ (new) and LICENSE.
  * How: the wg NSH command configures wg0 over the ioctl ABI and reads and
    writes wg(8)-format configuration files. Subcommands: up, down, show,
    showconf, setconf, saveconf, set, genkey, pubkey. The driver never
    returns the private key, so the configuration file is its only copy:
    persistence failures are reported instead of ignored, and saving first
    reserves an exclusive recovery copy of the old file, because a NuttX VFS
    rename may unlink the destination before the new file is in place.
    genkey/pubkey work without the device; pubkey uses wg_x25519.c, taken
    from the STROBE project under the MIT license (recorded in LICENSE by
    the second commit), instead of cryptodev, which would pull the whole
    software cipher suite in for one scalar multiplication.
  * Related: apache/nuttx#<PR-K1> (ABI), apache/nuttx#<PR-N> (nxstyle
    exclusion), documentation apache/nuttx#<PR-D>.
  * <optional> AI disclosure paragraph (see top of file).

depends-on: apache/nuttx/pull/<PR-K1>
depends-on: apache/nuttx/pull/<PR-N>

## Impact

  * Is new feature added? Is existing feature changed? YES: new
    CONFIG_SYSTEM_WG command.
  * Impact on user? NO for existing configurations.
  * Impact on build? NO (new Kconfig option, default n).
  * Impact on hardware? NO.
  * Impact on documentation? YES: apache/nuttx#<PR-D>.
  * Impact on security? YES: handles the private key; the configuration
    file is written with mode 0600 through exclusive temporary files.
  * Impact on compatibility? NO. Configuration files are wg(8) format.
  * Anything else to consider or add? wg_x25519.c keeps the STROBE body
    unchanged; the adaptation block defines X25519_SUPPORT_VERIFY 0 and
    silences a GCC -Wstringop-overread false positive so it builds with
    -Werror.

## Testing

  I confirm that changes are verified on local setup and works as intended:
  * Build Host(s): Linux x86_64 (Ubuntu 24.04 container), GCC 13.3.
  * Target(s): sim:wireguard; rv-virt:knetnsh64 (wg as a separate ELF);
    rv-virt:pnsh64 (BUILD_PROTECTED, QEMU);
    <ESP32-S3 board:config>; <Spresense board:config>.
  * BUILD_PROTECTED: a system call runs on the caller's user stack, so the
    wg stack must also hold the driver's ioctl path (an X25519 scalar
    multiplication when a private key or a peer is set) and the trap and
    interrupt frames. CONFIG_SYSTEM_WG_STACKSIZE therefore defaults to 6144
    when BUILD_PROTECTED (4096 otherwise). With 4096, repeated runs of the
    rv-virt:pnsh64 test failed 7 to 9 of 10 times (stack overflow); with
    6144, 20 of 20 runs passed. Not tested on PROTECTED hardware.

  Testing logs after change:

  ```
  <HW LOG: wg genkey | wg pubkey matches wg show "public key:" (no private key in the log)>
  <HW LOG: wg setconf / wg show / ping both ways>
  ```

## PR verification Self-Check

  (same five boxes as PR-C)
```
