# talkscript/

発表資料の「話す方」をまとめた場所。デッキ本体(HTML / PDF)は一つ上の
[docs/presentation/](../) にあり、台本はデッキと同じ名前で置く。

| デッキ | 台本 | 中身 |
|---|---|---|
| [slides.html](../slides.html)(本編 27 枚) | [slides.md](slides.md) | 読み上げ台本。時間配分と、枠に合わせて削る順番つき |
| [returns-slides.html](../returns-slides.html)(Sechack365 Returns、8 枚) | [returns-slides.md](returns-slides.md) | 構成・各スライドに載せるもの・**話すこと**・出典。Returns デッキの単一の情報源 |
| [short-slides.html](../short-slides.html)(9 枚 = Returns + Spresense / NuttX 3 版) | [short-slides.md](short-slides.md) | 読み上げ台本。10 分 / 8 分 / 5 分のルートつき |
| 同上(英語で話す場合) | [short-slides-en.md](short-slides-en.md) | 同じ 9 枚・同じ削る順番の英語版。140 words/min 換算 |
| (共通) | [presentation-script.md](presentation-script.md) | 進行表と想定 Q&A。「聞かれたら何を答えるか」 |

台本の型はどれも同じ: `〔秒数〕` / **[スライド]**(画面にあるもの、読まない)/ **[話す]**(読み上げ文)/
**[間]**(間・操作・視線)。秒数は 330 字/分換算。
