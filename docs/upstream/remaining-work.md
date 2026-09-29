# 残りの開発・検証・提出準備

更新: 2026-09-28。現在の作業順と完了条件の正本。
**次に着手する人(人でもエージェントでも)向けの手順・受入条件・既知の罠は
[handoff.md](handoff.md)** にまとめた。
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
  その後 PROTECTED のrv-virt実行、SMPの4CPU試験、Spresenseの資源測定、
  TVの追加KATも実施済み。実機PROTECTED、ESP32-S3の資源測定、日単位の
  連続運転は別の未実施条件であり、これらを混同しない。
- 管理リポジトリの文書pushと、NuttX/apps forkの公開・PRは別作業。
  後者は対応するソース版と検証証拠を揃えてから行う。

## マージ前の修正・保証

優先度はこのレビューの提案であり、upstreamメンテナの受理判断ではない。

| 優先 | 作業 / 追跡 | 現状と次の一手 | 完了条件 |
| --- | --- | --- | --- |
| P0 | TAI64N再起動保証 [#14](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/14) | **2026-09-28: 実機で対照実験として測定完了**(`verify-spresense-timestamp.py`)。同一ボード・ピア・イメージ、どちらのアームでもボードには何も送らない条件で、(A) 起動時の時計(`Jan 01 1970`)→ ドライバが警告し**75秒経ってもハンドシェイク成立せず**、(B) ホストから時計を設定 → 警告は出ず**4.1秒で成立**。変数は時計だけなので、拒否された timestamp が原因と特定。**重要な落とし穴も記録**: 素朴な「再起動後に ping」は16秒で成功してしまい何も証明しない — ping によって**ピア側が開始**し、responder は自分の timestamp を必要としないため。証跡は[evidence/spresense-timestamp-2026-09-28.md](evidence/spresense-timestamp-2026-09-28.md)。**#14は「証拠不足」ではなく「判断として」OPENのまま**: 高水位はRAMのみが設計、範囲予約はNuttXに耐久性の契約が無いので意図的に未実装 | 同じ鍵・相手状態維持・NuttX initiatorでTT-b/c/dを実施。対応不能構成の扱いも明示。電源断を含む保証と実装が一致 |
| P0 | 鍵・設定保存 [#17](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/17) | **追加修正 `37f04cff`**: NuttX VFS内のunlinkを確認し、rename先行だけで保持できるという説明を撤回。排他的な `.bak` に旧設定を確保し、公開失敗時は復旧ファイルを残して次の保存を拒否。mkstemp・読取エラー・重複PrivateKey行も対応。sim T1/TF/ENOSPC回帰、実ヘルパーのホスト11ケースと破壊的変異を拒否する対照試験がPASS。[証跡](evidence/sim-followup-2026-09-28.md)。**残: SmartFS実機・電源断** | 通常の失敗を成功扱いせず、旧設定から復旧可能。電源断時の耐久性は別条件として対象FSで確認 |
| P1 | 実usrsockの停止・送信詰まり [#5](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/5) / [#11](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/11) | **完了(2026-09-27、実機Spresense)**: `DEBUG_TX_STALL` で実GS2200M/usrsock上に送信を保持した状態で、参照(`wg show`)・制御更新(peer keepalive)が通り、`wg down`はETIMEDOUTを報告、保持中のciphertextは不変(アサート不発火)、反復downで回収し`wg up`で復旧。証跡は[evidence](evidence/spresense-kernel-2026-09-26.md#forced-usrsock-fault-held-send-vs-concurrent-control-2026-09-27) | close/destroyと実行中スレッドが競合せず、timeout報告と反復downで回収・再upできる。**2026-09-28: IOB枯渇をsimで試験しPASS**(下記)。**残: 送信中のWi-Fi断(AP操作が必要)、持続負荷。ストールは意図的遅延でありusrsock内部のブロック挙動そのものではない** |
| P1 | IOB設定制約 [#11](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/11) | `default IOB_NBUFFERS if NET_WIREGUARD` に加え、**明示的な `IOB_NCHAINS=0` を `wireguard.c` の `#error` で明示拒否(2026-09-27)**。2026-09-29の提出用driverコミット `c0ead14d83` へ統合し、既定sim・BUILD_KERNEL・SMP・PROTECTED・CMakeでビルド確認済み | 既定構成がビルドでき、不適合な明示設定が明確に拒否されるか補正される。driverコミットへ統合 |
| P1 | 最終ソース版の固定・公開 [#12](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/12) | **fork公開完了(2026-09-29)**: 最新upstreamへ競合なしでrebaseし、署名済みcrypto `a2dd121201` / driver `c0ead14d83` / apps `e4f910dc18` の3単位へ整理。`checkpatch.sh -m -g`、クリーンbaseへのpatch適用、全sim系列・単独依存ビルド・1/4 CPU kernel・PROTECTED runtime・CMakeを確認。fork branchは`wireguard-crypto` / `wireguard-driver` / `wireguard-wg`。[証跡](evidence/upstream-series-2026-09-29.md) / [patches](patches/2026-09-29/README.md)。**残: Apache本家へのPR作成。実機結果はこのrebase後候補では未再実行** | crypto/driver/appsの対応SHA、パッチ、構成、試験結果を固定。公開したコードを新規cloneで再現可能 |

鍵保存のコード根拠と試験項目は [keyfile-correctness-review.md](keyfile-correctness-review.md)。
**追加監査による訂正:** 以前の「rename先行」は安全性の根拠にはならない。
NuttX VFSはrenameの途中で保存先をunlinkする。今回の追加対応と復旧手順は
[追加レビュー](keyfile-correctness-review.md#follow-up-2026-09-28-vfs-replacement-is-not-atomic)を優先する。
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

- **2026-09-29完了**: **TZ鍵ゼロ化** — simの生プロセスメモリを読み、ヘッダから実行時に算出したオフセットで秘密フィールドを名前ごとに確認。未応答initiationで`handshake`のephemeral private/chaining key/hashを非ゼロにし、UDP relayでtransport confirmationを保留して`next_keypair`の送受信鍵も非ゼロにした上で、down後のゼロ化を実測。`curr`/`prev`も自然rekeyを含めて保持中とdown後を確認し、再upでは新鍵を導出。静的鍵は設計どおり1コピー保持。`scripts/kernel/verify-sim-wg-zeroize.sh`。**残り**: simの指定フィールドと制御した配送順序の観測であり、全メモリ残留や実機allocatorの証明ではない。
- **2026-09-28完了**: **TE乱数** — Spresense実機3回再起動で`wg genkey`が3件すべて異なる(`scripts/kernel/verify-spresense-entropy.py`、鍵は出力せずSHA-256前置のみ)。`DEV_URANDOM_XORSHIFT128`無効/プール有効も確認。**残り**: DTRリセットであり電源断ではない。3件相違は定数シードという致命的故障の検出であって乱数品質の測定ではない。ESP32-S3では未実施。
- **2026-09-28完了**: **T7ソーク(sim)** — 620秒(`REJECT_AFTER_TIME`×3=540秒超)で**rekey 151回・失敗0**、**endpoint変更10回**(両端のポート移動+インタフェース再起動で新endpointへ発信させ毎回通信確認)、**down/up 100回**。`free`・`/proc/iobinfo`・`ps`(`CONFIG_STACK_COLORATION`)で定点観測し、ヒープ使用量と生存アロケーション数は**ブート値へ完全復帰**(116→118→116)、IOBは全サンプル24/24・`nwait`0、`wg_rx`スタック高水位は**2536 B**で初回以降不変(`RXSTACKSIZE`既定6144に対して)。`scripts/kernel/verify-sim-wg-soak.sh`、表は[evidence/sim-soak-2026-09-28.md](evidence/sim-soak-2026-09-28.md)。**残り**: simなのでヒープ値は実機に転用不可(64bitフレーム・ホストアロケータ)。約15分で耐久試験ではない。rekeyはピア主導なのでWireGuard自身のタイマ経路は手薄。IOBが枯渇しなかったためthrottle/`nwait`挙動は未検証。実機のWi-Fi/usrsockスレッドのstack高水位は未測定。
- **2026-09-28完了**: **PROTECTED実行 (TP)** — `rv-virt:pnsh64` をQEMUで起動し、MPU分割下で`wg`を`nuttx_user`側に置いたまま、設定ioctlが境界を越えて**例外・アサート・panicなし**(`EXCEPTION`/`riscv_exception`/`Data access`/`Instruction access`/`PANIC`/`Assertion failed`/`_assert:`を実行中3箇所で検査)。virtio-net経由でLinux kernel WireGuardと実トンネル(host ping 3/3)、guestの`wg show`がハンドシェイクと`464 B received, 448 B sent`を返す(ABIの読み方向)。`wg down`/`wg up`も無故障で復帰。**既定のメモリ分割を変更していない**(kernel `.text` 209,832 B / user `.text` 21,576 B、各256 KB枠)。ポインタを持たないioctl ABIが「リンクできる」ではなく「実際の特権境界を越える」ことの確認。`scripts/kernel/build-pnsh-wg.sh` + `verify-pnsh-wg.sh`、証跡は[evidence/protected-runtime-2026-09-28.md](evidence/protected-runtime-2026-09-28.md)。**残り**: QEMUのMPUモデルであり実シリコンではない。RISC-Vなので`mr-canhubk3:knsh`(Cortex-M)はビルドのみ据え置き。down/upは1回のみ。
  - 副産物: この実行で **#17の修正が実機相当で発火**(書き込み不能な`/tmp`で「保存できない・再起動で失われる」と報告。修正前は成功扱いだった)。また **時計未設定警告(#14)が実ブートで発火**(pnsh64はRTCなし・`START_YEAR=2021`)。どちらも狙って作った状況ではない。
- **2026-09-28完了**: **SMP (TS)** — `rv-virt:knetnsh64_smp`(`CONFIG_SMP=y` / `SMP_NCPUS=4`、QEMU `-smp 4`)でLinux kernel WireGuardと双方向トンネル成立後、2000パケットのflood pingと**並行**に`wg show`×5・`wg set peer ... persistent-keepalive`×5・`wg down`/`wg up`を実行。例外・アサート・panic・アクセス違反なし、トンネル復帰。同じスクリプトと負荷を1 CPU構成でも回帰確認しPASS。`d_lock`規律が初めて**実際の並列性で競合**した(これまでコード論拠のみ)。**残り**: QEMU上の1つの決まったインタリーブでありレース探索ではない。`CONFIG_TESTING_SMP`の付属ストレスは併走させていない。実機はどちらもシングルコアなのでSMP実機なし。
- **2026-09-28完了**: **実機の資源測定とライフサイクル (TH)** — `--measure` ビルド(`STACK_COLORATION`+procfs、既定オフ: スタック塗りはタスク起動ごとに費用がかかるのでデモ像には載せない)でSpresenseを測定。**負荷**: ヘッドレス起動後、ホストから3本のping送信を120秒(1000/1000/200バイト) → **375/375応答**、`wg show`で296336 B受信/296320 B送信。ヒープ使用 **43760**・生存アロケーション **112** がidle/負荷中/停止後で完全一致、IOBは常時 **8/8**・`nwait`とthrottleは0。**`wg_rx`スタック高水位 1472 B / 6096 B (24.1%) で不変** — `RXSTACKSIZE`既定6144がCortex-M4F実測で裏付けられ、simの2536 Bは64bitフレーム由来の上界だったと確認。**ライフサイクル**: `wg down`/`wg up` 40回(毎回GS2200Mのソケットをusrsock経由で閉じ直し実Wi-Fiで再ハンドシェイク) — 10回ごとの通信確認4/4、アサート・fault無し、ヒープ/アロケーション/IOB/スタックが前後で**完全一致**。証跡は[evidence/spresense-resources-2026-09-28.md](evidence/spresense-resources-2026-09-28.md)。**残り**: 1ボード1アーキのみ(ESP32-S3は未測定、あちらはネイティブWi-Fiでusrsockではない)。分単位で日単位ではない。プールが枯渇しなかったのでIOB throttleは実機でも未検証。負荷は単一ホストからのICMPでスループット試験ではなく、ボード内の同時送信者も作っていない。rekeyは強制していないので151回はsim結果のまま。`STACK_COLORATION`は**観測された**最深であって最悪ケースではない。
- **未実施/不足**: **実機のPROTECTED**(ビルドは2026-09-27に`mr-canhubk3:knsh`で成功: kernel 170KB/1MB・ksram 30KB/128KB・`nuttx_user.elf` 147KB。実行は未着手)。
  - 副産物のサイズ下限: `lm3s6965-ek:qemu-protected`(kflash 128KB / ksram 20KB)はカーネル像が115%/112%で溢れる。`nuttx_user.elf`はリンクできるので、ユーザ側ではなくカーネル側の容量が効く。
- **2026-09-28完了**: **TR (c) out-of-window / (d) ファジング** — (c) ペイロード付きtransportパケットを`tap0`で捕捉し、flood pingでカウンタを窓の外へ(実測 12 → 2661、窓2048)。**receiver indexが不変であることを判定に入れて**rekeyで別理由に落ちるのを排除し、捕捉したEthernetフレームをバイト単位でそのまま3回再注入。計測中はピアのkeepaliveを切り、復号と窓判定の**両方**を通った後にしか加算されない`rx_bytes`が厳密に不変(298272→298272)であることを確認。(d) 不正データグラム3099件(長さ0、全256種の型×12長、各実型のsize±1・全0・全1、reservedビット立て、1500/2000バイト超長)。simは無停止・アサート無し、endpoint不変、通信継続。**残り**: (c)は「拒否された事実」の確認であって理由の直接観測ではない(ドライバがdrop理由を数えていないため)。カウンタはAEAD nonceの一部なので再nonce不可・同一keypairで一度復号済み、という論拠で窓が唯一の関門と言えるが、測定ではない。drop理由の計数をドライバに持たせれば直接化できるが、flood時のログ濫発とioctl構造体の変更を伴うので別件。(d)は固定シードのコーパスでカバレッジ誘導型ではない。
- **2026-09-28完了**: **TVのHKDF中間値** — `scripts/kernel/wg_crypto_kat.c` がドライバ自身の層(NuttXのプリミティブではなく)を検査: `wg_hmac`(32バイト鍵と**100バイト鍵** — RFC 2104で鍵を先にハッシュする経路。現在の呼び出し元は使わないので、相互接続では永遠に気づけない)、`wg_kdf1/2/3`の全チェーン(kdf2・kdf3がkdf1とt1で、互いにt2で一致することも確認)、whitepaper 5.4の2定数、`Hash(Label-Mac1 || Spub)`。11/11一致。参照値はPythonの`hmac`+`hashlib.blake2s`で独立算出(別実装なので言い換えではなく相互検証)。**full-handshake KATは意図的に不実施**: その参照実装はLinux kernel WireGuardそのもので、T1/T6/TPが3構成で実ハンドシェイクを通している方が強い。transcriptが足すのは故障箇所の局所化だけで、それはプリミティブ別・導出別のKATが担うようになった。
- **2026-09-28完了**: **T8のCMake経路** — `scripts/kernel/build-cmake-wg.sh` が `sim:nsh` をCMake/Ninjaで構成しドライバとコマンドを有効化してビルド、さらに**オブジェクトの存在を検査**(`wireguard.c.o`・`wg_crypto.c.o`・`wg_noise.c.o`・`wg_tai64n.c.o`・`wg_main.c.o`)。この検査が本質で、`drivers/net/CMakeLists.txt` は wireguard ディレクトリを名前で書かず `nuttx_add_subdirectory()` のグロブ(`*/CMakeLists.txt`)に依存しているため、グロブが外れても**ビルドは通り、デバイスは登録されない**という壊れ方をする。**別件の上流所見**: `netnsh` 系のCMakeビルドはこのツリーでは成立しない — `rv-virt:netnsh64`・`rv-virt:netnsh` がいずれも `ninja: error: 'libm.a', needed by 'nuttx', missing and no known rule to make it` で停止(**WireGuard抜きのstockでも同じことを確認**、`CONFIG_LIBM_TOOLCHAIN`由来)。`sim:nsh`・`rv-virt:nsh64`はクリーンにビルドできる。**残り**: `testbuild.sh`の広い構成行列と上流CI自体は未実施。
- **2026-09-28完了**: **#17の二次バグと残件試験** — 「同時に2つ書いたらどうなるか」を問うて**2件目の実バグ**を発見。両方の置換経路が固定名 `"<path>.tmp"` を共有しており、(a) 後発の `fopen("w")` が先発の書き込み中ファイルを truncate、(b) 一方が rename を済ませた後、他方の `rename()` は `ENOENT` で失敗し、そこで `wg_replace_file` が**正しい設定ファイルを unlink してから**やり直していた — つまり**何も入れずに良い設定を消す**。ドライバは秘密鍵を返さないのでそのファイルが唯一の複製。修正: 一時名に pid を入れて共有しない(以後の競合は原子的な `rename()` だけなので敗者は単に負けるだけで、どちらでもファイルは完全)、および unlink フォールバックを**ソース欠落時には取らない**。受入試験を拡張し9/9 PASS: 障害注入を**ファイルシステム満杯**に変更(simの`/tmp`は約500 KBのVFAT ramdiskなので`dd`で実際の書き込み失敗を起こせ、一時ファイル名に依存しない)。確認項目は、容量不足の保存が非ゼロ終了で「保存されていない」と報告する / デバイスとファイルの乖離を隠さず明示する / 旧設定が残る / `saveconf`も同様 / **一時ファイルを残さない**(残すと小容量FSが失敗保存ごとに埋まる) / 同時2ライタ後もロード可能でどちらかの鍵を保持 / 空き確保後に復旧。T1回帰もPASS。**残り**: 実機SmartFSでのrename・耐電源断。
- **2026-09-29完了**: **IOB枯渇 (TI)** — `IOB_NBUFFERS=12`・`IOB_THROTTLE=2`で負荷中24サンプルの最小free 0、アサート・死亡なし、静止時12/12復帰・通信復活を確認。`scripts/kernel/verify-sim-wg-iob-exhaustion.sh`。さらに[iob-wait-audit.md](iob-wait-audit.md)でdriverの直接buffered UDP TX/RXがtry-allocation (`throttled=false` / `timeout=0`)であることを追跡したため、`nwait=0`は未到達ではなく期待動作で、**driver内に試験すべきIOB待機経路はない**。実socket backendのブロックやAP切断は別のusrsock条件として残る。手法上 `/proc/iobinfo` は瞬時値なので負荷中の反復採取が必要。8バッファではhandshake自体が不安定だったため試験は12で実施し、実機側の8バッファpoolは満杯を下回っていない。
- **2026-09-29完了**: RTC/TAI64N部分修正を含む提出候補でBUILD_KERNELを再検証。`rv-virt:knetnsh64`の1 CPUと`rv-virt:knetnsh64_smp`の4 CPUで、Linux peerとの双方向トンネル、負荷中の設定操作、down/up復帰がPASS。実機ハードウェア結果はrebase後候補では未再実行。
- **すでに確認済み**: TF、T3、TN、基本TV（ChaCha/XChaCha/X25519/BLAKE2s）、通常のT5通信。これらを未実施として再登録しない。
- **個別提出**: crypto nonce修正とKAT、GS2200M ioctl [#10](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/10)、RTC_HIRES [#9](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/9)。**2026-09-27に切り分け完了: 実機のearly-bootハングは #9 そのもの**(新規cloneに当該パッチのみ追加で起動・トンネル成立。`cxd56_rtc.c`単独では直らない)。#9の提出価値が上がったので、driver PRとは独立に出す。
- **設計合意**: ioctl/IPv4-first、将来ABI、RX worker、資源上限、対応構成は [#3](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/3) / [#13](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/13)。IPv6は固定 `sockaddr_in` のままフラグ追加だけでは対応できない。
- **初回マージと分離できる候補**: IPv6実装、Pico 2 W、userspace X25519のcryptodev共通化。見送り範囲を明示して合意する。必要な安全性試験を単に「将来」に移さない。

## 更新ルール

Issueは追跡、Discussionは成果と相談、検証表は実測、設計文書は実装の保証範囲。
完了は「コード差分・対象版・試験・結果」が揃った項目だけに付ける。
日付だけでなく、旧apps版/driver版/未コミットtimestamp版を必ず区別する。
