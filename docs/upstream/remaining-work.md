# 残りの開発・検証・提出準備

更新: 2026-09-27。現在の作業順と完了条件の正本。
実測の正本は [verification-matrix.md](verification-matrix.md)、設計相談の索引は
[open-questions.md](open-questions.md)。古い計画の未実施一覧より本書を優先する。

## 現在地

- queued-output / `d_lock` 設計でsimとrv-virt BUILD_KERNELの通信を確認済み。
- ESP32-S3とSpresenseのin-kernel版でも通常通信の実機PASS報告あり。
  これはFLATでの実機通信であり、停止競合・詰まり・電源断までの保証ではない。
- TAI64Nの実時刻化・同一起動中の単調化は**NuttX forkの未コミット変更**。
  RTCありsimは再起動後0.695秒で応答。RTCなしでは拒否が再現し、#14は未解決。
  9月26日の実機ビルド用ソースは旧TAI64N。検証対象を混ぜない。
- 管理リポジトリの文書pushと、NuttX/apps forkの公開・PRは別作業。
  後者は対応するソース版と検証証拠を揃えてから行う。

## マージ前の修正・保証

優先度はこのレビューの提案であり、upstreamメンテナの受理判断ではない。

| 優先 | 作業 / 追跡 | 現状と次の一手 | 完了条件 |
| --- | --- | --- | --- |
| P0 | TAI64N再起動保証 [#14](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/14) | RTC経路は部分修正。RTCなし・時計巻き戻し・保存領域なしの方針を確定し、永続化するなら使用前の範囲予約を設計 | 同じ鍵・相手状態維持・NuttX initiatorでTT-b/c/dを実施。対応不能構成の扱いも明示。電源断を含む保証と実装が一致 |
| P0 | 鍵・設定保存 [#17](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/17) | 保存エラー未伝播と置換前unlinkを静的確認。まず失敗注入テストを作る | エラーを成功扱いしない。旧設定を保持し、runtime/file不一致を診断。対象FSで置換・耐電源断の保証を確認 |
| P1 | 実usrsockの停止・送信詰まり [#5](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/5) / [#11](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/11) | 通常疎通とsim障害注入は済み。実バックエンドで送信待ち中のioctl/down/reapを再現 | close/destroyと実行中スレッドが競合せず、timeout報告と反復downで回収・再upできる。Wi-Fi断でも記録する |
| P1 | IOB設定制約 [#11](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/11) | forkの変更は `default IOB_NBUFFERS if NET_WIREGUARD`。明示的な `IOB_NCHAINS=0` の扱いは別確認 | 既定構成がビルドでき、不適合な明示設定が明確に拒否されるか補正される。driverコミットへ統合 |
| P1 | 最終ソース版の固定・公開 [#12](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/12) | timestamp、IOB、crypto KATに未コミット差分がある | crypto/driver/appsの対応SHA、パッチ、構成、試験結果を固定。公開したコードを新規cloneで再現可能 |

鍵保存のコード根拠と試験項目は [keyfile-correctness-review.md](keyfile-correctness-review.md)。
RTCなしの保存方式・範囲予約は [tai64n-design.md](tai64n-design.md) の**未実装提案**。
「CLOCK_REALTIMEへ変えたので#14完了」「設定ファイルへ書けば耐電源断」は採用しない。

## 発表デモの準備

| 順序 | 作業 / 追跡 | 完了条件 |
| --- | --- | --- |
| D1 | Spresenseビルドの再現性 [#12](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/12) | 動作イメージのソース・追加パッチ・config・toolchain・ハッシュ・伏字ログを保存。RTC/clock/GS2200M差分を説明し、キャッシュ依存なしでビルド |
| D2 | kernel用Docker stageとheadless rcS (#12) | 電源投入から設定読込み・Wi-Fi準備待ち・wg upまで再現。接続失敗時の再試行/診断も確認。公開物に資格情報を入れない |
| D3 | 実演リハーサル (#12) | 両ボードと使用イメージを明記し、実際にkernel版で試した操作だけを台本に採用。時間計測、ネット不調時の録画/静的ログへの切替を確認 |
| D4 | スライド・台本・文書同期 (#12) | 「両ボードで通常通信」と「全故障モードの保証」を区別。apps版のtelnet/HTTP/長時間実績をkernel版へ転用しない |

初手は**D1の証拠固定**。動作中のボードを再書込みする前に再現可能な状態を残す。
その後、発表準備はD2/D3、マージに向けた開発は#14を一件ずつ進める。
headless化は実機デモを容易にするが、#14の解決にはならない。

## 追加検証と提出判断

- **未実施/不足**: TZ鍵ゼロ化、TEコールドブート時の乱数、T7長時間rekey・endpoint変更・反復up/down、IOB/stackの推移、代表的実機のstack高水位、SMP、PROTECTED。
- **部分完了**: TRのout-of-window/fuzz、TVのHKDF中間値/full-handshake KAT、T8のCMake・広い構成行列。RTC部分修正後のBUILD_KERNELも再検証する。
- **すでに確認済み**: TF、T3、TN、基本TV（ChaCha/XChaCha/X25519/BLAKE2s）、通常のT5通信。これらを未実施として再登録しない。
- **個別提出**: crypto nonce修正とKAT、GS2200M ioctl [#10](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/10)、RTC_HIRES [#9](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/9)。実機のearly-boot原因切り分けを既存#9と無条件に同一視しない。
- **設計合意**: ioctl/IPv4-first、将来ABI、RX worker、資源上限、対応構成は [#3](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/3) / [#13](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/13)。IPv6は固定 `sockaddr_in` のままフラグ追加だけでは対応できない。
- **初回マージと分離できる候補**: IPv6実装、Pico 2 W、userspace X25519のcryptodev共通化。見送り範囲を明示して合意する。必要な安全性試験を単に「将来」に移さない。

## 更新ルール

Issueは追跡、Discussionは成果と相談、検証表は実測、設計文書は実装の保証範囲。
完了は「コード差分・対象版・試験・結果」が揃った項目だけに付ける。
日付だけでなく、旧apps版/driver版/未コミットtimestamp版を必ず区別する。
