# `apache/nuttx-apps/LICENSE` 追記案

## この文書の位置づけ

`apps/netutils/wireguard/` には wireguard-lwip 由来のサードパーティコードが含まれる。NuttX のコーディング規約では、

> サードパーティファイルの場合、そのファイル自体は元のライセンス表記を保持し、Apache 2.0 に置き換えない。その代わり、トップレベルの `LICENSE` および/または `NOTICE` ファイルに差分を記載する

とされており、`apache/nuttx-apps/LICENSE` には既に uIP・FreeModbus・THTTPD・PPPD・lvgl などが Appendix 形式で列挙されている。本文書はそこに追記する差分の下書き。

**未確定事項**: 実際の PR 提出前に、最新の `LICENSE` の書式(見出しレベル・区切り方)を再確認して合わせること。以下は 2026-08 時点の既存エントリのパターンに倣ったもの。

---

## 1. 対象ファイルの棚卸し

`Makefile` の `CSRCS` に入っている = **実際にビルドされる**ファイルのみを対象とする。

| ファイル | 由来 | ライセンス |
|---|---|---|
| `wireguard.c` / `wireguard.h` | wireguard-lwip | BSD-3-Clause (Daniel Hope) |
| `crypto.c` / `crypto.h` | wireguard-lwip | BSD-3-Clause (Daniel Hope) |
| `crypto/refc/chacha20.c` / `.h` | wireguard-lwip | BSD-3-Clause (Daniel Hope) |
| `crypto/refc/chacha20poly1305.c` / `.h` | wireguard-lwip | BSD-3-Clause (Daniel Hope) |
| `crypto/refc/blake2s.c` / `.h` | RFC 7693 リファレンス実装 | パブリックドメイン相当(RFC 由来) |
| `crypto/refc/poly1305-donna*.c` / `.h` | poly1305-donna (floodyberry) | パブリックドメイン または MIT |
| `crypto/refc/x25519.c` / `.h` | STROBE (Cryptography Research, Inc.) | MIT |
| `wireguard-platform.h` | wireguard-lwip | BSD-3-Clause (Daniel Hope) |

**ビルド対象外**:

- `wireguardif.c` / `wireguardif.h` — lwIP netif 向けのグルー。NuttX では `nuttx-wireguardif.c` が置き換えるため未使用
- `crypto/cortex/scalarmult.c` / `.h` — Cortex-M 向けアセンブリ最適化 (CC0 / B. Haase)。現状ビルドしていない

→ **対応済み (2026-08-29)。** `Dockerfile` の base ステージで `wireguardif.c` / `wireguardif.h` / `crypto/cortex/` を削除するようにした。したがって **CC0 (crypto/cortex) のエントリは不要**で、上記の追記案がそのまま提出対象になる。

---

## 2. `LICENSE` への追記案

```
============================================================================
apps/netutils/wireguard
============================================================================

The WireGuard protocol implementation and its bundled reference cryptography
were adapted from the wireguard-lwip project
(https://github.com/smartalock/wireguard-lwip).

The following files retain their original BSD-3-Clause license:

  wireguard.c, wireguard.h, wireguard-platform.h, crypto.c, crypto.h,
  crypto/refc/chacha20.c, crypto/refc/chacha20.h,
  crypto/refc/chacha20poly1305.c, crypto/refc/chacha20poly1305.h

  Copyright (c) 2021 Daniel Hope (www.floorsense.nz)
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are
  met:

  1. Redistributions of source code must retain the above copyright notice,
     this list of conditions and the following disclaimer.

  2. Redistributions in binary form must reproduce the above copyright
     notice, this list of conditions and the following disclaimer in the
     documentation and/or other materials provided with the distribution.

  3. Neither the name of the copyright holder nor the names of its
     contributors may be used to endorse or promote products derived from
     this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
  IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
  THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
  PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

----------------------------------------------------------------------------

  crypto/refc/x25519.c, crypto/refc/x25519.h

  Taken from https://sourceforge.net/p/strobe (MIT License).

  Copyright (c) 2015-2016 Cryptography Research, Inc.
  Released under the MIT License.

  Permission is hereby granted, free of charge, to any person obtaining a
  copy of this software and associated documentation files (the
  "Software"), to deal in the Software without restriction, including
  without limitation the rights to use, copy, modify, merge, publish,
  distribute, sublicense, and/or sell copies of the Software, and to permit
  persons to whom the Software is furnished to do so, subject to the
  following conditions:

  The above copyright notice and this permission notice shall be included
  in all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
  OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
  CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
  TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
  SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

----------------------------------------------------------------------------

  crypto/refc/poly1305-donna.c, crypto/refc/poly1305-donna.h,
  crypto/refc/poly1305-donna-32.h

  Taken from https://github.com/floodyberry/poly1305-donna
  Placed in the public domain by Andrew Moon, or alternatively available
  under the MIT License.

----------------------------------------------------------------------------

  crypto/refc/blake2s.c, crypto/refc/blake2s.h

  BLAKE2s reference implementation taken from RFC 7693
  (https://tools.ietf.org/html/rfc7693).
```

---

## 3. 提出前チェックリスト

- [x] `wireguardif.c` / `wireguardif.h` を vendored ツリーから除外する — **完了** (2026-08-29)。`Dockerfile` の base ステージで削除するようにした。除外後も esp32s3 のビルドが通ることを確認済み
- [x] `crypto/cortex/` を除外する — **完了** (2026-08-29)。同上
- [x] 各 vendored ファイルの元ライセンスヘッダが**改変されずに残っている**ことを再確認 — **完了**。`blake2s.c` は `// Taken from RFC7693 - https://tools.ietf.org/html/rfc7693`、`poly1305-donna.h` は `// Taken from https://github.com/floodyberry/poly1305-donna - public domain or MIT` のまま
- [x] `x25519.c` が参照している `LICENSE.txt`(STROBE 側の)の本文を実際に確認し、上記 MIT 全文が正しいか裏を取る — **完了**。`crypto/refc/x25519-license.txt` の実物と照合し、`Copyright (c) 2015-2016 Cryptography Research, Inc.` の MIT 全文が上記記載と一致することを確認
- [ ] 最新の `apache/nuttx-apps/LICENSE` の書式に合わせる
- [ ] メンターにライセンス記載のレビューを依頼する

**注意**: 上記の MIT / BSD 全文は一般的な定型文をもとに記載したもの。実際の提出時は **各ファイルに実際に書かれている文面をそのまま引用**すること(定型文と1文字でも違う場合、原文が優先)。
