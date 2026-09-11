# Sechack365 Returns talk: WireGuard for Apache NuttX

HTML deck: [returns-slides.html](returns-slides.html)

Audience: Sechack365 Returns alumni event. The deck should feel like a concise project talk,
not a loose Japanese LT. Use the wording already present in [README.md](../../README.md) and
[proposal.md](../proposal/proposal.md) as the source of truth.

## Core Message

> A WireGuard VPN implementation for Apache NuttX, exposed as a `wg0` network device.
> Verified on real hardware against real WireGuard peers.

Why it matters:

- Apache NuttX is a POSIX-compliant RTOS designed for resource-constrained environments.
- NuttX currently has no native, lightweight VPN capability.
- Remote and secure access to NuttX devices is a real, unsolved problem across edge AI,
  industrial IoT, satellite and space hardware, and unmanned infrastructure.
- Without a VPN, the realistic options are to expose a global IP, build a bespoke protocol,
  or accept a vendor cloud.
- The upstream goal is to make WireGuard available through `CONFIG_NET_WIREGUARD=y`.

## Slide Structure

| # | Title | Role | Timing |
|---|---|---|---|
| 1 | WireGuard for Apache NuttX | One-line project identity | 30 sec |
| 2 | NuttX is used where physical access is expensive. | Context with photos | 70 sec |
| 3 | Secure remote access is still a real gap. | Motivation from README/proposal | 70 sec |
| 4 | A regular network interface, backed by an encrypted tunnel. | Explain `wg0` | 60 sec |
| 5 | Keep the protocol core, replace the network glue. | Implementation strategy | 90 sec |
| 6 | Verified on real hardware against real WireGuard peers. | Current status | 70 sec |
| 7 | Make it available to NuttX developers immediately. | Upstream/Returns close | 45 sec |

## Visual Sources

- Apache NuttX logo: <https://nuttx.apache.org/>
- SPRESENSE product photo: <https://developer.spresense.sony-semicon.com/>
- AITRIOS edge AI device photos: <https://www.aitrios.sony-semicon.com/edge-ai-devices>

Downloaded local copies are in [assets](assets/).

## Text Policy

- Prefer English. Avoid awkward Japanese slide copy.
- Use project text already written in README/proposal wherever possible.
- Keep Sechack365 context subtle: close with the idea that a discomfort from real work can
  become an upstream contribution when shaped, tested, and documented.
