# 他 LLM へのレビュー依頼テンプレート

カーネル移行の設計・実装を他 LLM（ChatGPT / Gemini / 別の Claude など）や
人間にレビューしてもらうための定型文。ラウンドごとに使い回す。

- 入口: GitHub Issue [#11](https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/11)（追跡）
- 資料: [in-kernel-review-brief.md](in-kernel-review-brief.md)（自己完結ブリーフ。論点10問が末尾）
- 生 URL:
  - Issue: `https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/11`
  - ブリーフ(blob): `https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/blob/main/docs/upstream/in-kernel-review-brief.md`
  - ブリーフ(raw): `https://raw.githubusercontent.com/wwlapaki310/gsoc2026-nuttx-wireguard/main/docs/upstream/in-kernel-review-brief.md`

> ⚠️ 多くの LLM チャットは URL を踏めない（コールドスタート）。Web 閲覧が使える
> 相手には **A**、踏めない相手には **B**（ブリーフ本文を丸ごと貼る）を使う。

---

## 運用ループ

1. この Issue に、レビュー依頼コメント（下の「Issue コメント文面」）を1回貼っておく。
2. 他 LLM に **A** か **B** を投げる。
3. 返答を Issue に「ラウンド N: <LLM 名>」として要約 + 自分の採否を記録。
4. 採用分を plan / コード / ブリーフに反映 → commit → 次のラウンド。

---

## Issue コメント文面（#11 に貼る）

```markdown
## レビュー依頼(相互レビュー用)

カーネル移行の設計・実装レビューを募集します。自己完結ブリーフはこちら:

- ブリーフ: https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/blob/main/docs/upstream/in-kernel-review-brief.md

ブリーフ末尾の「レビュー論点」10問に、賛否 + 根拠の形で答えてもらえると助かります。
設計上の懸念・見落とし・代替案があれば具体的に(NuttX / WireGuard / 組込みネットワーク観点)。

各レビューは本 Issue に「ラウンド N: <レビュアー/LLM 名>」として要約 + 採否を記録していきます。
```

---

## A. Web 閲覧が使える LLM に投げる文

```text
あなたは Apache NuttX(独自 TCP/IP スタックの RTOS)と WireGuard に詳しい、
カーネル/ネットワークのレビュアーです。

次の GitHub Issue と、そこからリンクされたレビューブリーフを読んでください。
NuttX に WireGuard を「カーネル内 netdev(wg0) + ユーザー空間 ioctl クライアント」
として実装する設計・実装のレビューをお願いします。

- Issue: https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/issues/11
- ブリーフ: https://github.com/wwlapaki310/gsoc2026-nuttx-wireguard/blob/main/docs/upstream/in-kernel-review-brief.md

依頼:
1. ブリーフ末尾の「レビュー論点」10問に、各問「賛成/反対/条件付き」+ 根拠で答える。
2. ブリーフに書かれていない設計上の懸念・見落とし・代替案があれば具体的に指摘する。
3. upstream(apache/nuttx)にマージする観点で、通りやすさ・粒度・検証の十分性を評価する。

ページを取得できない場合は「取得できなかった」とだけ返してください。本文を貼り直します。
```

## B. URL を踏めない LLM に投げる文(ブリーフ本文を貼る)

```text
あなたは Apache NuttX(独自 TCP/IP スタックの RTOS)と WireGuard に詳しい、
カーネル/ネットワークのレビュアーです。

以下は、NuttX に WireGuard を「カーネル内 netdev(wg0) + ユーザー空間 ioctl
クライアント」として実装する設計・実装のレビューブリーフです。読んだうえで:

1. 末尾の「レビュー論点」10問に、各問「賛成/反対/条件付き」+ 根拠で答える。
2. 書かれていない設計上の懸念・見落とし・代替案があれば具体的に指摘する。
3. upstream(apache/nuttx)にマージする観点で、通りやすさ・粒度・検証の十分性を評価する。

--- ここからブリーフ本文 ---
<in-kernel-review-brief.md の中身をここに貼る>
--- ここまで ---
```

## C. コード実装まで見せる場合の追記(論点9/10 用)

diff を添付するときの前置き:

```text
上記に加えて、実装 diff を添付します。メモリ安全性・スレッド安全性・エンディアン・
ライフサイクル(ifup/ifdown での socket と RX kthread の起動停止)を重点的に見てください。

diff は各 fork で:
  git diff <merge-base with upstream/master>..HEAD
で生成したものです(nuttx 側 +4470 行 / apps 側 +2042 行)。長い場合は
wireguard.c / wg_noise.c / wireguard.h に絞って貼ります。
```
