#!/usr/bin/env python3
"""Regenerate coc-glasgow-script-ja.md, the Japanese practice script.

    python export_ja_script.py

It is the Japanese twin of coc-glasgow-script-en.md. Structure, order, times
and totals come from the deck's speaker notes through export_en_script.py;
the Japanese sentences are in ja_notes.py (one to one with the English
notes), the Japanese titles and "what is on the slide" texts in
ja_screens.py. The generator stops if a slide has a different number of
Essential / Optional / Presenter-note paragraphs than the English notes, so
the two scripts cannot drift apart. Change the English notes first, then
ja_notes.py, then run export_en_script.py and this.
"""

import sys
from pathlib import Path

import export_en_script as en
import ja_notes
import ja_screens

HERE = Path(__file__).resolve().parent
OUT = HERE / "coc-glasgow-script-ja.md"

TAGS = {
    "core — do not cut": "核心・削らない",
    "cut candidate 2": "削る候補2",
    "cut candidate 3": "削る候補3",
    "my favorite": "お気に入り",
}

# "what is on the slide" texts that changed after ja_screens.py was made
SCREEN_FIX = {
    "23": ("左に動画(静止画 + 再生ボタン。クリックするとスライドの中で YouTube の動画が再生され、スライドを離れると止まる。"
           "公式 Windows WireGuard クライアントと、ESP32-S3 上の NuttX への telnet セッションが並んだ画面)。"
           "右に手順 — telnet 10.10.0.2 / uname -a、ifconfig / webserver & / ブラウザで http://10.10.0.2/ — と条件(ESP32-S3 DevKit、apps 版 (FLAT)、"
           "USB は抜いてある、相手は公式 Windows クライアント)、小さく YouTube へのリンク。Takeaway: Version 1 was enough for a demo. "
           "Next, I made it a real NuttX driver."),
    "8": ("見出し「Sony's history with NuttX」。左に縦のタイムライン — 2013–15 Early adoption(LC823425 / LC823450 に移植、最初の商用の音声製品は2015年)/ "
          "2016–17 Commercial products(Walkman NW-WS620、SMR-10、ICD-TX800、ICD-PX470)/ 2018 Xperia Ear Duo & SPRESENSE / "
          "2019–20 Upstream collaboration / 2020s Edge sensing。右に製品の写真3枚(Walkman NW-WS620、Xperia Ear Duo、SPRESENSE)。"
          "最下段: 「Sony's NuttX journey began with audio products, expanded through SPRESENSE, and evolved into long-term open-source collaboration.」"
          "出典はスライド下の行にリンクで置く。"),
}

HEADER = """\
# 読み上げ台本(日本語) — WireGuard for Apache NuttX (Community Over Code, Glasgow)

本番は英語で話す前提。この日本語版は[英語版](coc-glasgow-script-en.md)と**1対1で対応する、リハーサル・理解確認用**の台本です。
このファイルは [`export_ja_script.py`](export_ja_script.py) が作ります(構成・時間・合計は英語版と同じ計算、日本語の文は
[`ja_notes.py`](ja_notes.py))。手で直さず、英語のノート → `ja_notes.py` の順に直して、`export_en_script.py` と
`export_ja_script.py` を実行してください。スライドごとの段落の数が英語版と違うと、生成が止まります。

**2026-10-13:** 英語版を読み上げ用に作り直したのに合わせて、日本語版も作り直した(3区分、話速 {wpm} 語/分、短い文、
アプリ版とデモの扱いの整理)。日本語は英語の語数に合わせたものではないので、時間は**英語版の値**を使う。

## この台本の読み方

各スライドには最大3つの部分があります。

- **話す — Essential(必ず話す):** 上から順に読む。それだけで、スライド間のつなぎも含めて、完結した発表になる。ゆっくり、はっきり話す前提。
- **話す — Optional(時間があれば):** 時間があれば足す。飛ばしても、次の文は成り立つ。
- **Presenter Notes(読み上げない):** 出典、デモの手順、確認事項、注意。

## 時間(英語版と同じ)

話速 {wpm} 語/分(英語の語数で計算)、目次のスライドは各10秒、ライブデモは {demo}(話すことと操作)。スライドの切り替えと短い間は、この数字に含む。

| | 語数(英語) | 時間 |
|---|---:|---:|
| **Essential のみ**(目次の間とライブデモを含む) | {e_words} | **{e_time}** |
| Essential + Optional | {eo_words} | {eo_time} |
| うちライブデモ | {demo_words} | {demo} |
| 30分枠の余り(Essential のみ) | | {room_e} |
| 30分枠の余り(Essential + Optional) | | {room_eo} |

| 部 | スライド | Essential | + Optional |
|---|---|---:|---:|
{chapters}

各スライドの見出しの時間は、Essential の累積(「このあたりにいるはず」)。

## 削ってはいけないもの

**一番大事なのはスライド{core}(Testing: What Ran Where)**: 査読者が確認してくる主張が集中している。注記は削らない: FLAT は実機、
KERNEL と PROTECTED は QEMU のみ、KERNEL と PROTECTED の実機はまだ。
直後にあった「ping が見逃すもの」と「時計を信じるか」は付録(A1・A2)で、Thank You の後ろ。上の時間には含めず、質問で必要になったときだけ、最後のスライドから下矢印で開く。

**前半の物語は削らない**(スライド2と4〜{arch}: About Me、Overview から Architecture まで): なぜ始めたか、Sony と NuttX、私自身の経験、課題、
なぜ機器に VPN か、なぜ WireGuard か。後半すべての前提になる。**スライド{learned}(What I learned)も残す** — 発表者自身のメッセージ。

**時間が足りないときに削る順番:**
1. Optional をすべて。
2. スライド{cut2}(Version 1 の動画の静止画。ライブデモが同じ種類のものを見せる)。
3. スライド{cut3}(バグ③、50ms ポーリング。1行の教訓は一覧表にすでにある)。

ライブデモが止まったら、そう言って、次のスライド(バックアップの動画。時間に含めない)を開くか、まとめへ進む。**目次に戻るスライド({dividers})は各10秒の一呼吸**:
ハイライトされた行を指して、一文だけ言う。本当に時間がなければ飛ばしてよい。

---
"""


def check_same_shape(en_notes):
    problems = []
    for i, (ess, opt, note) in enumerate(en_notes, 1):
        ja = ja_notes.JA.get(i)
        if ja is None:
            problems.append("slide %d: no Japanese text" % i)
            continue
        for kind, e, j in (("ess", ess, ja.get("ess", [])),
                           ("opt", opt, ja.get("opt", [])),
                           ("note", note, ja.get("note", []))):
            if len(e) != len(j):
                problems.append("slide %d: %s has %d English and %d Japanese paragraphs"
                                % (i, kind, len(e), len(j)))
        if len(en.SLIDES[i - 1][3]) != len(ja.get("extra", [])):
            problems.append("slide %d: %d English and %d Japanese cue lines"
                            % (i, len(en.SLIDES[i - 1][3]), len(ja.get("extra", []))))
    if problems:
        raise SystemExit("English and Japanese notes differ:\n  " + "\n  ".join(problems))


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    en_notes = en.parse_notes()
    check_same_shape(en_notes)
    d = en.analyse(en_notes)
    count = len(en_notes)
    napp = d["napp"]

    def find(pred):
        return [i for i, sl in enumerate(en.SLIDES, 1) if pred(sl)]

    rows = []
    names = ["最初: 表紙・自己紹介・目次", "① 概要と背景", "② 課題と解決", "③ 開発計画",
             "④ Version 1: アプリケーション版", "⑤ Version 2: カーネルドライバ",
             "⑥ ライブデモ", "⑦ まとめと振り返り"]
    bounds = d["bounds"]
    for k in range(len(bounds) - 1):
        a, b = bounds[k], bounds[k + 1] - 1
        es = sum(d["per"][i - 1][0] for i in range(a, b + 1))
        os_ = sum(d["per"][i - 1][1] for i in range(a, b + 1))
        rows.append("| %s | %d–%d | %s | %s |" % (names[k], a, b, en.mmss(es), en.mmss(es + os_)))

    core = find(lambda sl: sl[1].startswith("core"))
    body = [HEADER.format(
        wpm=en.WPM, demo=en.mmss(en.DEMO_S),
        e_words=d["e_words"], e_time=en.mmss(d["e_time"]),
        eo_words=d["eo_words"], eo_time=en.mmss(d["eo_time"]),
        demo_words=d["demo_words"],
        room_e=en.mmss(1800 - d["e_time"]), room_eo=en.mmss(1800 - d["eo_time"]),
        chapters="\n".join(rows), core=core[0],
        arch=find(lambda sl: sl[0].startswith("Architecture"))[0],
        learned=find(lambda sl: sl[0] == "What I Learned")[0],
        cut2=find(lambda sl: sl[1] == "cut candidate 2")[0],
        cut3=find(lambda sl: sl[1] == "cut candidate 3")[0],
        dividers="・".join(map(str, d["dividers"])))]

    k = 0
    for i in range(1, count + 1):
        heading, tag, on_slide, extra = en.SLIDES[i - 1]
        a, b = d["spans"][i - 1]
        ja = ja_notes.JA[i]
        if tag == "appendix":
            k += 1
            key = "A%d" % k
            body.append("## %s — 付録: %s (付録・時間に含めない)\n"
                        % (key, ja_screens.TITLES[key].replace("付録: ", "")))
            body.append("**付録:** 本編には含めない。Thank You の後、質問で必要になったら、最後のスライドから下矢印で開く。\n")
        else:
            key = str(i)
            suffix = ""
            if tag in TAGS:
                suffix = " *(%s)*" % TAGS[tag]
            body.append("## %d — %s (%s → %s)%s\n" % (i, ja_screens.TITLES[key], en.mmss(a), en.mmss(b), suffix))
        screen = SCREEN_FIX.get(key, ja_screens.SCREENS[key])
        if screen:
            body.append("**画面:** %s\n" % screen)
        if tag == "divider":
            body.append("**話す(一言):** %s\n" % " ".join(ja["ess"]))
            body.append("**間:** ハイライトされた行を指して一拍。\n")
            continue
        body.append("**話す — Essential(必ず話す):**")
        body.extend("- %s" % p for p in ja["ess"])
        body.append("")
        if ja.get("opt"):
            body.append("**話す — Optional(時間があれば):**")
            body.extend("- %s" % p for p in ja["opt"])
            body.append("")
        notes = ja.get("note", []) + ja.get("extra", [])
        if notes:
            body.append("**Presenter Notes(読み上げない):**")
            body.extend("- %s" % p for p in notes)
            body.append("")

    OUT.write_text("\n".join(body).rstrip() + "\n", encoding="utf-8", newline="\n")
    print("wrote %s (%d slides + %d appendix)" % (OUT.name, count - napp, napp))


if __name__ == "__main__":
    main()
