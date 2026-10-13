"""Japanese slide titles and the "what is on the slide" texts of the
Japanese script (export_ja_script.py). Kept by hand."""

TITLES = {'1': 'タイトル',
 '2': '自己紹介',
 '3': '目次',
 '4': '目次 ①Overview & background',
 '5': 'プロジェクト概要',
 '6': '経緯: 2月の思いつきから10月の本番まで',
 '7': 'A Quick Overview of Apache NuttX: NuttX とは何か、どこで使われているか',
 '8': 'Sony と NuttX の歴史',
 '9': 'Experience: SPRESENSE と AITRIOS での私の経験',
 '10': '目次 ②The Problem & Solution',
 '11': 'Problem: 遠隔の機器へ、どう安全にアクセスするか',
 '12': 'Options: 遠隔の機器へ届く方法',
 '13': 'Solution: 機器に VPN を載せる',
 '14': 'Architecture: NuttX ネットワークスタック上の WireGuard',
 '15': '目次 ③The Development Plan',
 '16': 'Porting Strategy: wireguard-lwip から始める',
 '17': 'Mapping lwIP Networking to NuttX',
 '18': '目次 ④Version 1: Application-Based Implementation',
 '19': 'Version 1: NuttX のアプリケーションとしての WireGuard',
 '20': 'Where it was hard: 6つのバグ',
 '21': 'バグ②: ping は通るのに TCP が死ぬ',
 '22': 'バグ③: 「実害なし」が実は10倍遅かった',
 '23': 'Version 1 のデモ: 最初の動画',
 '24': '目次 ⑤Version 2: The Kernel Driver',
 '25': 'Version 2: NuttX のカーネルドライバとしての WireGuard',
 '26': 'カーネル側の設計: 全体像',
 '27': 'Two Issues Found During Kernel Integration: カーネル統合で見つかった2つの問題',
 '28': '検証: どこで何が動いたか',
 '29': '目次 ⑥Live Demo',
 '30': 'ライブデモ: Stack-chan を WireGuard 越しに',
 '31': '目次 ⑦Summary & Reflections',
 '32': 'Summary',
 '33': 'What I Learned: 学んだこと',
 '34': 'Acknowledgements: 謝辞',
 '35': 'Thank You — Questions?',
 'A1': '付録: 検証: ping が見逃すもの',
 'A2': '付録: 設計判断: タイムスタンプ問題には楽な答えがない'}

SCREENS = {'1': '「WireGuard for Apache NuttX」を大きく。その下にサブタイトル「Bringing Secure Remote Access to Embedded Devices」。\n'
      '上に Community Over Code Glasgow 2026、下に Satoru Akita / Sony Semiconductor Solutions。右下に小さな QR コード\n'
      '(「Slides」、最後のスライドと同じ URL)。技術的な情報(wg0、QEMU、ビルドモード、検証環境、バージョン)は載せない。',
 '2': '見出し「About Me」。上: 本人写真・名前の1行・SNS の狐アイコン。下の左: 日本地図\n'
      '(① 名古屋 生まれ → ② 仙台 大学 → ③ 川崎 現在)と、その下に「Japan」のラベル。\n'
      '下の右: 「My background & activities」の大きな写真5枚(Rocket / Aerospace Engineering: モデルロケット /\n'
      'DNA Nanorobotics: cryo-TEM 像 / Semiconductor Research: ウェハ上の試作チップ /\n'
      'Robots: ハッカソンのヒューマノイド / Open-Source Conference Volunteer: 運営参加時の写真)。\n'
      '活動を説明する文章はない。経歴の流れ(分子ロボティクス → … → NuttX)は口頭だけ。',
 '3': '7つの部。見出しは短い名詞句、その下に「これから何を話すか」を予告する短い1行、右に所要時間 —\n'
      'Overview & background: Project origins · NuttX & Sony · my experience(~5分)/ The Problem & Solution: '
      'Remote access challenge · why WireGuard(~4分)/\n'
      'The Development Plan: Reuse wireguard-lwip · adapt to NuttX(~2分)/ Version 1: Application-Based '
      'Implementation: First working tunnel · debugging challenges(~4分)/\n'
      'Version 2: The Kernel Driver: Why a driver · integration challenges · validation(~5分)/ Live Demo: '
      'Secure remote access · Stack-chan control(~3分)/\n'
      'Summary & Reflections: Achievements · personal reflections · community(~4分)。',
 '4': '目次と同じリスト。①Overview & background だけハイライト、他は薄い。',
 '5': '`wg0` netdev の図(変更なし)— アプリケーション/NSH、NuttX ネットワークスタック、\n'
      '`eth0`/`wlan0` の隣に `wg0`、その下に UDP ソケット、インターネット、ピア。\n'
      '図の下にカード3枚: Lightweight VPN / Virtual interface / Secure communication。\n'
      '最下段に一文。',
 '6': '2月〜10月の月の横軸。上に出来事 — 2月中旬 GSoC のテーマ一覧を見て調べ始める /\n'
      '3月上旬 計画を提案 / 3月20日 Alan さんからこのカンファレンスを聞き、CfP を提出 /\n'
      '4月上旬 GSoC に応募 / 5月上旬 CfP は採択、GSoC は不採択 — それでも続ける / 10月 グラスゴー。\n'
      '下に、矢印の形をした3本の帯 — 5〜7月 NuttX と WireGuard を調べ、作り方を探る →\n'
      '8月 Version 1(アプリ・FLAT・実機検証)→ 9月〜 Version 2(カーネルドライバ)。',
 '7': '左: NuttX のロゴと4つの要点(POSIX-like な RTOS / 小さい / 幅広いハードウェア対応 —\n'
      '15+ アーキテクチャ・300+ ボード / Apache のオープンソース)、標準 API の小さな表\n'
      '(FreeRTOS: 独自のタスク API / Zephyr: 独自 API とオプションの POSIX 層 / NuttX: POSIX-like が設計方針)。\n'
      '右: 実機の写真タイル — Sony、Fitbit(Charge 3 / 4)、PX4 のフライトコントローラ、\n'
      '月面ロボット SORA-Q、Xiaomi OpenVela(1,000+ 製品)。\n'
      '最下段: 「Apache NuttX is a POSIX-like RTOS used in a wide range of real-world embedded devices.」',
 '8': '縦タイムライン — 2015 Sony audio products / 2018–19 SPRESENSE・CXD56xx\n'
      '(OSS+ELC Europe 2019 で発表) / 2020 upstream collaboration(NuttX Online Workshop、\n'
      '「A Journey from Fork to Mainline」) / 2020s sensing & edge devices。',
 '9': '見出し「My Experience with NuttX」。写真つきのカード2枚。\n'
      'SPRESENSE — Satellite Project(人工衛星 ARICA の写真)/ AITRIOS — Edge AI Sensing Platform\n'
      '(edge AI カメラの写真)。最下段: 「I wanted a secure way to access and troubleshoot remote embedded devices.」',
 '10': '目次と同じリスト。②The Problem & Solution だけハイライト、他は薄い。',
 '11': '見出し「Problem: How Can We Securely Access Remote Devices?」。\n'
       'Engineer → Internet → Router/network → Wi-Fi → NuttX device の図(変更なし)。\n'
       '経路全体に必要な安全な道と、WPA2/WPA3 が守る範囲(Wi-Fi の一区間のみ)の対比。\n'
       '下に3枚のカード: Devices are far away / Remote diagnosis / A secure path。\n'
       '最下段: 「We needed a secure way to access and troubleshoot remote NuttX devices.」',
 '12': '見出し「Options: Ways to Reach a Remote Device」。6つの手段(ポートを開ける /\n'
       'クラウド経由の HTTPS・MQTT / リバーストンネル・リレー / ルーターの VPN / キャリアの閉域網 /\n'
       '機器の VPN)を、任意の IP サービスに届くか・間にサーバーが要らないか・どこまで守られるか、\n'
       'で比べた表。最後の行(機器の VPN)が今回の選択。\n'
       '最下段: 「Every option has trade-offs. I chose a VPN on the device, so the device itself is the end of '
       'the tunnel.」',
 '13': '見出し「Solution: A VPN on the Device」。Engineer/Linux/Windows ⟷ WireGuard トンネル ⟷\n'
       'NuttX device の図(変更なし)。その下に小見出し「Why WireGuard?」と3枚のカード:\n'
       'Small & simple(コード行数の小さな棒グラフ: IPsec 約42万 / OpenVPN 約12万 / WireGuard 約3,800、2018年)/\n'
       'Modern cryptography(ChaCha20-Poly1305、Curve25519)/ UDP-based tunnel。\n'
       '最下段: 「Run a lightweight, standard VPN directly on the NuttX device.」',
 '14': 'Application → TCP/UDP/IP → wg0 (NET_LL_TUN netdev) → WireGuard\n'
       'encrypt/decrypt → UDP socket → wlan0/usrsock・物理ネットワーク、の層状図。\n'
       '左右に TX/RX 両方向の矢印。',
 '15': '目次と同じリスト。③The Development Plan だけハイライト、他は薄い。',
 '16': '見出し「Porting Strategy: Start from wireguard-lwip」。一番上に GitHub のリンク\n'
       '(github.com/smartalock/wireguard-lwip、クリックできる)と「An existing open-source WireGuard for lwIP」。\n'
       'その下に大きな3段: KEEP — Protocol & crypto(緑、小さな修正2件つき)/ ADAPT — Platform hooks(金)/\n'
       'REPLACE — lwIP network glue(赤)。最下段に一文。',
 '17': '見出し「Mapping lwIP Networking to NuttX」。lwIP と NuttX の対応表 —\n'
       'ネットワークデバイス(`struct netif` → `struct net_driver_s`)/ パケットバッファ(`pbuf` → `iob`)/\n'
       '登録(`netif_add()` → `netdev_register()`)/ 送信(output コールバック → `d_txavail` → `devif_poll()`)/\n'
       'UDP の「ワイヤ」(`udp_pcb` → UDP ソケット)。注記「考え方の対応で、API の1対1の置換ではない」。\n'
       "最下段: 「Porting meant using NuttX's own building blocks, not bringing lwIP into NuttX.」",
 '18': '目次と同じリスト。④Version 1: Application-Based Implementation だけハイライト、他は薄い。',
 '19': '見出し「Version 1: WireGuard as a NuttX application」。「GOAL」の枠(Run a standard WireGuard tunnel as a\n'
       'NuttX application. / Reuse the existing core and implement the NuttX integration.)。その下に3列の表\n'
       '(Layer / Work needed / In this port): Protocol & crypto — Almost none、暗号はバイト単位で同一、プロトコル本体は\n'
       '小さな修正2つ(約3,100行を再利用・約40行を変更)/ OS hooks — Four functions(約120行を新規)/\n'
       'Network glue — The real work、netdev + UDP socket + RX task(約1,200行を新規)。\n'
       '最下段は2行の Takeaway: Version 1 proved that WireGuard could run on NuttX. / It also exposed OS and '
       'networking\n'
       'problems that the first tunnel did not reveal.',
 '20': '見出し「Where it was hard: six bugs」。6つのバグの一覧表\n'
       '(バグ / 出た場所 / 教訓): 1 `wg` コマンドが終わると受信スレッドが止まる / 2 ping は通るのに TCP が死ぬ\n'
       '(in detail) / 3 「実害なし」が10倍(in detail) / 4 スタック94%、設定保存でボードが固まる、実機だけ /\n'
       '5 NuttX 自身の ChaCha20-Poly1305(part 5) / 6 3KB のカーネルスタックに 6KB の配列(part 5)。\n'
       'GitHub への案内と QR コードは置かない。Takeaway: 多くは通る試験の陰に隠れていた。',
 '21': '見出し「Bug ②: Ping Works. TCP Dies.」(オレンジ〜赤)。左の枠 SYMPTOM: ping は 0% loss /\n'
       'TCP のハンドシェイクは完了し、その後で接続が死ぬ / `EBADF` — “Bad file descriptor”。\n'
       '右の枠 ROOT CAUSE & FIX: `wg_rx` タスクからの送信は成功、TCP のデータを送るシステムのワークキューからは失敗 /\n'
       '原因: ファイルディスクリプタは1つのタスクグループに属し、別のタスクグループでは有効でない /\n'
       '修正: `struct socket` に対する `psock_*()` を使う(fd なし、どのタスクからも使える)。\n'
       'Takeaway は「If only some tasks fail, look at what each task owns. Here, it was the file descriptor.」',
 '22': '見出し「Bug ③: “No Real Impact” Was 10× Slower」(オレンジ〜赤)。2つの枠を並べる:\n'
       'QUICK FIX — Check every 50 ms(測定: 約10倍遅い)/ REAL FIX — Event-driven wake-up\n'
       '(`psock_poll()` のコールバックとセマフォ)。Takeaway: 測らない応急処置はタダに見える。測ること。',
 '23': '左に動画の1コマ(公式 Windows WireGuard クライアントと、ESP32-S3 上の NuttX への\n'
       'telnet セッションが並んだ画面)。右に手順 — telnet 10.10.0.2 / uname -a、ifconfig / webserver & /\n'
       'ブラウザで http://10.10.0.2/ — と条件(ESP32-S3 DevKit、apps 版 (FLAT)、USB は抜いてある、相手は\n'
       '公式 Windows クライアント)、小さく youtu.be/1kyX2av5WG4。Takeaway: Version 1 was enough for a demo.\n'
       'Next, I made it a real NuttX driver.',
 '24': '目次と同じリスト。⑤Version 2: The Kernel Driver だけハイライト、他は薄い。',
 '25': '見出し「Version 2: WireGuard as a NuttX kernel driver」。左右2枚の枠 —\n'
       '左 BEFORE — VERSION 1: 「An application」(`apps/` にあり、カーネル内部(`psock_*`)に手を入れる。FLAT ビルドでしか\n'
       '動かず、in-tree のドライバとしては合わない)/ 右 AFTER — VERSION 2: 「A kernel driver」(`drivers/net/wireguard/` に\n'
       '`wg0`、小さな `wg` コマンドが ioctl で設定。FLAT・PROTECTED・KERNEL のために作った)。\n'
       'その下に BUILD MODES の3行: FLAT — アプリとカーネルが1つのアドレス空間 / PROTECTED — MPU がアプリとカーネルの\n'
       'メモリを分ける / KERNEL — MMU が別々のアドレス空間を与える。\n'
       '最下段: 「The goal: a proper kernel driver for every NuttX build mode, in a form that is easier to '
       'review upstream.」',
 '26': '見出し「The Kernel Design: The Big Picture」。図は2つの帯 — 上が USER SPACE(青みがかった地、\n'
       '`wg` コマンドだけ。apps/system/wg、genkey・set・show)、下が NUTTX KERNEL(緑がかった地)。\n'
       'ioctl(ポインタを含まない平らな構造体1つ。秘密鍵は書き込み専用)がユーザー/カーネルの境界を越えて、カーネル内の\n'
       '`wg0`(drivers/net/wireguard)に届く。`wg0` は自分の UDP ソケットと受信スレッドを持ち、タイマー・ピア・\n'
       'クッキーを管理し、NuttX の暗号(crypto/)を使う。左に NuttX のネットワークスタック、右に暗号化された\n'
       'UDP(wlan0 / usrsock → ピア)。',
 '27': '見出し「Two Issues Found During Kernel Integration」。左右2つの枠にラベル。\n'
       '左(青い縁、少し強調)CRYPTOGRAPHY — ChaCha20-Poly1305(NuttX `crypto/chachapoly.c`: ナンスのカウンタが RFC 8439 /\n'
       'WireGuard の並びと違うバイトにある。カウンタ0では一致するので、ハンドシェイクは通るが、最初より後のデータパケットが\n'
       '失敗する。テストベクタつきの2行の修正を、独立した変更に)/ 右(灰色の縁)KERNEL STACK — Stack Overflow(このドライバ、\n'
       'KERNEL ビルドのみ: 3KB のカーネルスタックに 6KB の配列 → あふれてパニック。sim では出ない。配列はヒープへ。\n'
       'ドライバ自身のバグ)。背景は両方ともニュートラル。SPRESENSE の RTC_HIRES は画面に載せず、ノートだけ。\n'
       '最下段: 「Porting WireGuard also surfaced findings worth sharing with the NuttX community.」',
 '28': '見出し「Testing: What Ran Where」。表(Build mode / Environment / Result): FLAT — ESP32-S3・SPRESENSE\n'
       '(実機)✓ Verified / KERNEL — QEMU rv-virt(Linux へのトンネル、4 CPU で負荷)✓ Verified /\n'
       'PROTECTED — QEMU rv-virt(1件の例外を見つけて修正、20/20)✓ Verified / KERNEL・PROTECTED — 実機 — Not tested yet。\n'
       '小さな注記「✓ は、ここに書いた条件で通ったという意味で、他のハードウェアへの保証ではない」。\n'
       'GitHub への案内と QR コードは置かない。最下段は2行: 実機の FLAT と QEMU の KERNEL / PROTECTED で検証した /\n'
       'それらの実機での検証は残っている。',
 '29': '目次と同じリスト。⑥Live Demo だけハイライト、他は薄い。',
 '30': '見出し「Live Demo: Stack-chan over WireGuard」。1枚だけで、デモ中はこの1枚を出したまま、端末を横に並べる。\n'
       '上半分 NETWORK SETUP: Laptop(WireGuard peer、VPN IP 10.10.0.1)と Stack-chan(Apache NuttX + WireGuard、\n'
       'VPN IP 10.10.0.2)が、Smartphone(Wi-Fi hotspot、ローカルネットワークを提供)に点線の Wi-Fi でつながり、\n'
       'ラップトップと Stack-chan の間に太い赤の「Encrypted WireGuard Tunnel」が1本。スマホは WireGuard のピアとして描かない。\n'
       '下半分 LIVE DEMO STEPS: 1 VPN Connection(`wg show`)/ 2 Remote Login(`telnet 10.10.0.2`)/\n'
       '3 Network Interface(`ifconfig` → `wg0`)/ 4 Control Stack-chan(表情を変える・声を出す)。\n'
       '隅に小さな Stack-chan の写真(GitHub へのリンク)。Version の表記、Takeaway の枠、動画へのリンクは置かない。',
 '31': '目次と同じリスト。⑦Summary & Reflections だけハイライト、他は薄い。',
 '32': '見出し「Summary」。3つの行で、それぞれ短い見出しと1文: ① Secure Communication(WireGuard を Apache NuttX に持ち込み、\n'
       '標準の WireGuard ピアと安全な IP 通信ができるようにした)/ ② Application to Kernel Driver(アプリケーションからカーネルの\n'
       'ネットワークドライバへ発展させ、実機と QEMU で検証した)/ ③ Toward Upstream Contribution(ドライバと関連する修正を、\n'
       'Apache NuttX コミュニティのレビューに向けて準備している。小さく「小さな別々の変更として用意した。まだ何も提出していない」)。\n'
       '`wg0` などの内部用語や、PR の分け方・ファイルパスは載せない。',
 '33': '見出し「What I learned」。カード3枚: 1 アプリのユーザーから OS のコントリビューターへ / 2 AI はコードを書くのを助けて\n'
       'くれたが、理解するのは私の仕事だった(強調)/ 3 歓迎してくれるオープンソースコミュニティ(良いドキュメントと貢献ガイド、\n'
       '活発な議論とレビュー、人が責任を持つ AI 支援開発への前向きさ、CI・ハッカソン・イベントが新しい人の参加を助ける)。\n'
       '最下段: 「I hope to keep contributing to Apache NuttX and the open-source community, and I look forward '
       'to seeing NuttX\n'
       'continue to grow.」',
 '34': '見出し「Acknowledgements」。カードをやめ、縦のシンプルな一覧。各項目は、名前(太字、少し大きく)と短い1行:\n'
       'Alan Carvalho de Assis(この発表を Community Over Code に出すよう勧めてくれた)/ The Apache NuttX Community\n'
       "(議論、ドキュメント、支援)/ Sony's NuttX Developers, Past and Present(NuttX への貢献と、築いてくれた土台)/\n"
       'The Apache Software Foundation & Community Over Code Organizers(オープンソースのコミュニティをつなげてくれている。\n'
       '運営者もボランティアも)。順番は 個人 → コミュニティ → 関係する組織。',
 '35': '見出し「Thank You — Questions?」。大きな QR コード(「Scan to view the slides」)と、その横にクリックできるリンク2つ:\n'
       'スライド(https://wwlapaki310.github.io/gsoc2026-nuttx-wireguard/docs/presentation/coc-glasgow-reveal/index.html)と\n'
       'GitHub のリポジトリ。表紙にも同じ QR コード(「Slides」)。動画のリンクは置かない。',
 'A1': '見出し「Three problems that ping would never notice.」。カード3枚 — 起動のたびに同じ鍵?\n'
       '(SPRESENSE: 3回のリセットで3つの違う鍵。簡単な確認で、乱数の質の証明ではない)/ down の後に鍵が\n'
       'メモリに残る?(sim: セッション鍵は down の後すべてゼロ。静的鍵は up をやり直すために意図的に残す)/\n'
       '数時間後に出るリーク?(sim: 151回の再鍵化、100回の down/up、何も失われない。メモリは起動時の数に戻る。\n'
       '1週間の試験ではない)。',
 'A2': '規則(再起動の後でも、相手が見たものより新しいタイムスタンプが要る)。Chosen(リアルタイム\n'
       'クロック + この起動で使った最大値。Linux・wireguard-go と同じ規則 + 時計が未設定なら警告)/ Not chosen\n'
       '(毎回保存: flash が摩耗 / flash に範囲を予約: 確かめられなかった書き込み保証が要る / 待つ: 前回動いて\n'
       'いた時間だけ)。実測のペア(時計未設定 → 75秒でハンドシェイクなし、時計設定 → 4.1秒)。'}
