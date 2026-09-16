# GS2200M usrsock: issue draft for apache/nuttx

Two defects found while running a second, kernel-registered netdev (`wg0`, a
`NET_LL_TUN` device) alongside the GS2200M usrsock daemon on Spresense
(NuttX 12.7.0, `spresense:wifi`). Neither is specific to WireGuard; any
application that registers its own netdev or relies on `SIOCDENYINETSOCK`
will hit them. Status: **draft, not filed** (2026-09-16).

Suggested title: `drivers/wireless/gs2200m: ifreq ioctls ignore ifr_name;
daemon forwards SIOCDENYINETSOCK to the driver`

---

## 1. `gs2200m_ioctl_ifreq()` answers every ifreq as its own

**Where:** `drivers/wireless/gs2200m.c`, `gs2200m_ioctl_ifreq()`; reached
through `apps/wireless/gs2200m/gs2200m_main.c` `ioctl_request()`.

**What happens:** With `CONFIG_NET_USRSOCK`, `netdev_ioctl()` (in
`net/netdev/netdev_ioctl.c`) calls the socket's `si_ioctl` first and only
falls through to `netdev_ifr_ioctl()` when that returns `-ENOTTY`:

```c
  if (psock->s_sockif && psock->s_sockif->si_ioctl)
    {
      ret = psock->s_sockif->si_ioctl(psock, cmd, arg);
    }

  if (ret != OK && ret != -ENOTTY)
    {
      return ret;
    }
```

The GS2200M daemon forwards `SIOCSIFADDR`, `SIOCSIFDSTADDR`,
`SIOCSIFNETMASK`, `SIOCGIFADDR`, `SIOCGIFFLAGS`, `SIOCGIFHWADDR` to the
driver, and `gs2200m_ioctl_ifreq()` applies them to `dev->net_dev` without
ever looking at `ifr.ifr_name`. For any other `SIOC*` it returns `-EINVAL`,
which `netdev_ioctl()` treats as a final answer rather than "not mine".

So for a netdev the driver does not own (here `wg0`):

- `netlib_set_ipv4addr("wg0", ...)` reprograms the **Wi-Fi module's** IP
  (`AT+NSET=10.10.0.2,...`), breaking the LAN link.
- `netlib_ifup("wg0")` (`SIOCSIFFLAGS`) fails with `-EINVAL`, so
  `netdev_ifup()` and the device's `d_ifup()` never run.

**Expected:** compare `ifr.ifr_name` against the driver's own interface name
and return `-ENOTTY` for anything else, so `netdev_ifr_ioctl()` can resolve
the interface by name. Unknown `SIOC*` should also be `-ENOTTY`, not
`-EINVAL`.

**Log (SIOCSIFADDR = 0x702, SIOCSIFFLAGS = 0x71a, both meant for wg0):**

```
gs2200m_ioctl_ifreq: +++ start: cmd=702
gs2200m_send_cmd: +++ cmd=AT+NSET=10.10.0.2,255.255.255.0,192.168.0.1
gs2200m_ioctl_ifreq: +++ start: cmd=71a
gs2200m_ioctl_ifreq: +++ end:
```

**Workaround used:** the application configures its own `struct
net_driver_s` directly and calls `netdev_ifup()` instead of going through a
socket ioctl.

---

## 2. Daemon forwards `SIOCDENYINETSOCK` to the driver and returns `-1`

**Where:** `apps/wireless/gs2200m/gs2200m_main.c`, `ioctl_request()`,
`case SIOCDENYINETSOCK`.

**What happens:** the case updates `priv->usock_enable` and `break`s with
`drvreq` still `true`, so the request is also sent to the driver as
`GS2200M_IOC_IFREQ`. The driver has no handler for that cmd and returns
`-EINVAL`; the daemon then stores the raw `ioctl()` return value (`-1`) in
`resp.result`, which the caller sees as `-EPERM`. The flag *is* updated, so
the call "fails" while having taken effect.

**Expected:** handle it entirely in the daemon, as
`apps/lte/alt1250/usock_handlers/alt1250_ioctl_denyinetsock.c` does:

```c
      case SIOCDENYINETSOCK:
        read(fd, &sock_type, sizeof(uint8_t));
        priv->usock_enable = (sock_type != DENY_INET_SOCK_ENABLE);
        ret = OK;
        drvreq = false;
        break;
```

Also, when `drvreq` is taken, `ret` should be `-errno` on `ioctl()` failure
rather than `-1`.

---

## Why this matters beyond WireGuard

`SIOCDENYINETSOCK` is the documented way to let an application create
kernel-stack sockets while a usrsock daemon is running. Combined with (1),
a board using GS2200M cannot bring up any second netdev (TUN, PPP, a
second Ethernet) through the normal `netlib_*` helpers, and cannot reliably
switch socket creation to the kernel stack. Both fixes are small and local
to the GS2200M driver/daemon.

## Reproduction

`spresense:wifi` + `CONFIG_NET_TUN` + any app that does
`netdev_register(&dev, NET_LL_TUN)` then `netlib_ifup(dev.d_ifname)`.
Full context: https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard
(`docs/development/phase4-log.md`, 2026-09-16 entries).
