# 残りの開発・検証・提出準備

更新: 2026-09-28。現在の作業順と完了条件の正本。
実測の正本は [verification-matrix.md](verification-matrix.md)、設計相談の索引は
[open-questions.md](open-questions.md)。古い計画の未実施一覧より本書を優先する。

## 現在地

- queued-output / `d_lock` 設計でsimとrv-virt BUILD_KERNELの通信を確認済み。
- ESP32-S3とSpresenseのin-kernel版でも通常通信の実機PASS報告あり。
  これはFLATでの実機通信であり、停止競合・詰まり・電源断までの保証ではない。
- TAI64Nの実時刻化・同一起動中の単調化は**コミット済み**(`7936d68402`)。
  さらに時計未設定を1度だけ警告する変更(`d472ff1c2e`)も入れた。
  RTCありsimは再起動後0.695秒で応答。RTCなしでは拒否が再現し、**#14は未解決**。
  9月26日の実機ビルド用ソースは旧TAI64N。検証対象を混ぜない。
- 2026-09-28に **T0・TR(a–d)・TZ・TE・T7** が実測PASSになった(下記)。
  未実施として残るのは PROTECTED の実行、実機でのT7/stack高水位、SMP、TVの追加KAT。
- 管理リポジトリの文書pushと、NuttX/apps forkの公開・PRは別作業。
  後者は対応するソース版と検証証拠を揃えてから行う。

## マージ前の修正・保証

優先度はこのレビューの提案であり、upstreamメンテナの受理判断ではない。

| 優先 | 作業 / 追跡 | 現状と次の一手 | 完了条件 |
| --- | --- | --- | --- |
| P0 | TAI64N再起動保証 [#14](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/14) | RTC経路は部分修正。RTCなし・時計巻き戻し・保存領域なしの方針を確定し、永続化するなら使用前の範囲予約を設計 | 同じ鍵・相手状態維持・NuttX initiatorでTT-b/c/dを実施。対応不能構成の扱いも明示。電源断を含む保証と実装が一致 |
| P0 | 鍵・設定保存 [#17](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/17) | **コード修正済み(2026-09-27)**: エラー伝播、`set private-key` の失敗を成功扱いしない、`wg_replace_file` で rename 先行(拒否時のみ unlink フォールバック、原本消失時は復旧先を明示)。simビルドとT1回帰はPASS。**失敗注入テストも追加してPASS**(`verify-sim-wg-keyfile-faults.sh`: 非ゼロ終了・診断・旧設定保持・復旧の5項目)。**残: ENOSPC/close失敗、二重writer、置換境界での中断、SmartFSのrename/耐電源断の実地確認** | エラーを成功扱いしない。旧設定を保持し、runtime/file不一致を診断。対象FSで置換・耐電源断の保証を確認 |
| P1 | 実usrsockの停止・送信詰まり [#5](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/5) / [#11](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/11) | **完了(2026-09-27、実機Spresense)**: `DEBUG_TX_STALL` で実GS2200M/usrsock上に送信を保持した状態で、参照(`wg show`)・制御更新(peer keepalive)が通り、`wg down`はETIMEDOUTを報告、保持中のciphertextは不変(アサート不発火)、反復downで回収し`wg up`で復旧。証跡は[evidence](evidence/spresense-kernel-2026-09-26.md#forced-usrsock-fault-held-send-vs-concurrent-control-2026-09-27) | close/destroyと実行中スレッドが競合せず、timeout報告と反復downで回収・再upできる。**残: IOB枯渇、送信中のWi-Fi断、持続負荷。ストールは意図的遅延でありusrsock内部のブロック挙動そのものではない** |
| P1 | IOB設定制約 [#11](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/11) | `default IOB_NBUFFERS if NET_WIREGUARD` に加え、**明示的な `IOB_NCHAINS=0` を `wireguard.c` の `#error` で明示拒否(2026-09-27)** — 不完全型の不可解なエラーではなくなった。**残: driverコミットへの統合** | 既定構成がビルドでき、不適合な明示設定が明確に拒否されるか補正される。driverコミットへ統合 |
| P1 | 最終ソース版の固定・公開 [#12](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/12) | timestamp、IOB、crypto KATに未コミット差分がある | crypto/driver/appsの対応SHA、パッチ、構成、試験結果を固定。公開したコードを新規cloneで再現可能 |

鍵保存のコード根拠と試験項目は [keyfile-correctness-review.md](keyfile-correctness-review.md)。
RTCなしの保存方式・範囲予約は [tai64n-design.md](tai64n-design.md) の**未実装提案**。
「CLOCK_REALTIMEへ変えたので#14完了」「設定ファイルへ書けば耐電源断」は採用しない。

## 発表デモの準備

| 順序 | 作業 / 追跡 | 完了条件 |
| --- | --- | --- |
| D1 | Spresenseビルドの再現性 [#12](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/12) | **実質完了(2026-09-27)**: 伏字ログ・イメージ/config ハッシュを [evidence](evidence/spresense-kernel-2026-09-26.md) に保存。**early-boot ハングの原因を特定 = 既知の cxd56 `CONFIG_RTC_HIRES` 回帰([#9](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/9))**であり「キャッシュの癖」ではなかった(以前の記述は誤り)。新規 clone + 当該パッチで起動しトンネル成立(ping 6/6)。**残: Dockerfile ステージ化(D2)と toolchain バージョンの記録** |
| D2 | kernel用ビルドスクリプトとheadless rcS (#12) | **完了(2026-09-27)**: `scripts/kernel/build-spresense-kernel.sh` がクリーンな `nuttx-13.0.1` から再現ビルド(#9パッチを同梱・適用失敗なら停止、ハッシュ出力)。`docker/spresense-kernel-etc/init.d/rcS` で電源投入 → gs2200m → `wg setconf` → `wg up`。**シリアル無操作のリセットのみで 32 秒後に ping 6/6 を実測**。資格情報は build 引数で、既定は未投入(公開物に入らない)。**残: 接続失敗時の再試行/診断の作り込み** |
| D3 | 実演リハーサル (#12) | 両ボードと使用イメージを明記し、実際にkernel版で試した操作だけを台本に採用。時間計測、ネット不調時の録画/静的ログへの切替を確認 |
| D4 | スライド・台本・文書同期 (#12) | 「両ボードで通常通信」と「全故障モードの保証」を区別。apps版のtelnet/HTTP/長時間実績をkernel版へ転用しない |

初手は**D1の証拠固定**。動作中のボードを再書込みする前に再現可能な状態を残す。
その後、発表準備はD2/D3、マージに向けた開発は#14を一件ずつ進める。
headless化は実機デモを容易にするが、#14の解決にはならない。

## 追加検証と提出判断

- **2026-09-28完了**: **TZ鍵ゼロ化** — simの生プロセスメモリを読み、ヘッダから実行時に算出したオフセットで秘密フィールドを名前ごとに確認。rekeyを強制して`prev_keypair`にも実際に鍵を載せた上で、`curr`/`prev`の送受信鍵が up中32/32バイト → down後すべてゼロ、再upで新鍵。静的鍵は設計どおり保持され、書込可能領域全体でコピーは**ちょうど1個**(前後で不変)。`handshake`はup中すでにゼロ(`wg_start_session`が鍵導出と同時にスタック上の複製ごと消す)。`scripts/kernel/verify-sim-wg-zeroize.sh`。**残り**: `next_keypair`と`handshake`は計測時点で非ゼロにならなかったため、消去はコード読みのみの根拠。
- **2026-09-28完了**: **TE乱数** — Spresense実機3回再起動で`wg genkey`が3件すべて異なる(`scripts/kernel/verify-spresense-entropy.py`、鍵は出力せずSHA-256前置のみ)。`DEV_URANDOM_XORSHIFT128`無効/プール有効も確認。**残り**: DTRリセットであり電源断ではない。3件相違は定数シードという致命的故障の検出であって乱数品質の測定ではない。ESP32-S3では未実施。
- **2026-09-28完了**: **T7ソーク(sim)** — 620秒(`REJECT_AFTER_TIME`×3=540秒超)で**rekey 151回・失敗0**、**endpoint変更10回**(両端のポート移動+インタフェース再起動で新endpointへ発信させ毎回通信確認)、**down/up 100回**。`free`・`/proc/iobinfo`・`ps`(`CONFIG_STACK_COLORATION`)で定点観測し、ヒープ使用量と生存アロケーション数は**ブート値へ完全復帰**(116→118→116)、IOBは全サンプル24/24・`nwait`0、`wg_rx`スタック高水位は**2536 B**で初回以降不変(`RXSTACKSIZE`既定6144に対して)。`scripts/kernel/verify-sim-wg-soak.sh`、表は[evidence/sim-soak-2026-09-28.md](evidence/sim-soak-2026-09-28.md)。**残り**: simなのでヒープ値は実機に転用不可(64bitフレーム・ホストアロケータ)。約15分で耐久試験ではない。rekeyはピア主導なのでWireGuard自身のタイマ経路は手薄。IOBが枯渇しなかったためthrottle/`nwait`挙動は未検証。実機のWi-Fi/usrsockスレッドのstack高水位は未測定。
- **未実施/不足**: T7の実機再実施、代表的実機のstack高水位、SMP、**PROTECTEDの実行**(ビルドは2026-09-27に`mr-canhubk3:knsh`で成功: kernel 170KB/1MB・ksram 30KB/128KB・`nuttx_user.elf` 147KB。実行は未着手)。
  - 副産物のサイズ下限: `lm3s6965-ek:qemu-protected`(kflash 128KB / ksram 20KB)はカーネル像が115%/112%で溢れる。`nuttx_user.elf`はリンクできるので、ユーザ側ではなくカーネル側の容量が効く。
- **2026-09-28完了**: **TR (c) out-of-window / (d) ファジング** — (c) ペイロード付きtransportパケットを`tap0`で捕捉し、flood pingでカウンタを窓の外へ(実測 12 → 2661、窓2048)。**receiver indexが不変であることを判定に入れて**rekeyで別理由に落ちるのを排除し、捕捉したEthernetフレームをバイト単位でそのまま3回再注入。計測中はピアのkeepaliveを切り、復号と窓判定の**両方**を通った後にしか加算されない`rx_bytes`が厳密に不変(298272→298272)であることを確認。(d) 不正データグラム3099件(長さ0、全256種の型×12長、各実型のsize±1・全0・全1、reservedビット立て、1500/2000バイト超長)。simは無停止・アサート無し、endpoint不変、通信継続。**残り**: (c)は「拒否された事実」の確認であって理由の直接観測ではない(ドライバがdrop理由を数えていないため)。カウンタはAEAD nonceの一部なので再nonce不可・同一keypairで一度復号済み、という論拠で窓が唯一の関門と言えるが、測定ではない。drop理由の計数をドライバに持たせれば直接化できるが、flood時のログ濫発とioctl構造体の変更を伴うので別件。(d)は固定シードのコーパスでカバレッジ誘導型ではない。
- **部分完了**: TVのHKDF中間値/full-handshake KAT、T8のCMake・広い構成行列。RTC部分修正後のBUILD_KERNELも再検証する。
- **すでに確認済み**: TF、T3、TN、基本TV（ChaCha/XChaCha/X25519/BLAKE2s）、通常のT5通信。これらを未実施として再登録しない。
- **個別提出**: crypto nonce修正とKAT、GS2200M ioctl [#10](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/10)、RTC_HIRES [#9](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/9)。**2026-09-27に切り分け完了: 実機のearly-bootハングは #9 そのもの**(新規cloneに当該パッチのみ追加で起動・トンネル成立。`cxd56_rtc.c`単独では直らない)。#9の提出価値が上がったので、driver PRとは独立に出す。
- **設計合意**: ioctl/IPv4-first、将来ABI、RX worker、資源上限、対応構成は [#3](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/3) / [#13](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/13)。IPv6は固定 `sockaddr_in` のままフラグ追加だけでは対応できない。
- **初回マージと分離できる候補**: IPv6実装、Pico 2 W、userspace X25519のcryptodev共通化。見送り範囲を明示して合意する。必要な安全性試験を単に「将来」に移さない。

## 更新ルール

Issueは追跡、Discussionは成果と相談、検証表は実測、設計文書は実装の保証範囲。
完了は「コード差分・対象版・試験・結果」が揃った項目だけに付ける。
日付だけでなく、旧apps版/driver版/未コミットtimestamp版を必ず区別する。
