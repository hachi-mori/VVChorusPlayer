# VVChorusPlayer

> 現在のベース実装名： **VVProject Synth**  
> 将来的に「複数キャラ合唱対応」へ発展させるための開発プロジェクトです。

---

## ✨ これは何？

**VOICEVOX のプロジェクトファイル「 `.vvproj`」 を直接読み込み、  
DAW内で歌声を生成・再生できる VST3 プラグインです。**

DAW上で使いやすいワークフローを目指し、   
「キャラ選択 → 生成 → 再生 → 書き出し」までをシンプルに扱える構成になっています。

---

## 🎯 できること

- `.vvproj` のソングトラック読み込み
- 歌唱キャラクター／スタイル選択
- VOICEVOX Engine API による歌声生成
- プレビュー再生
- WAV書き出し

---

## ⚙ 動作に必要なもの

- ソングトラックに楽譜情報が格納されたVOICEVOXプロジェクトファイル（`.vvproj`）
- 本プラグイン：[リリース](https://github.com/hachi-mori/VVChorusPlayer/releases/tag/vvps-v1.0.0)からVST3形式でDLできます
- VST3対応DAW
- VOICEVOX Engine が起動していること  
  （[VOICEVOX](https://voicevox.hiroshiba.jp/)をインストールし、ローカルで起動してください）

---

## 🚀 使い方（クイックスタート）

## ① VOICEVOX を起動
<img width="1024" height="630" alt="image" src="https://github.com/user-attachments/assets/30344115-1d30-425b-9250-3a2751db4f4e" />

---

## ② DAWでプラグインを開く
<img width="782" height="758" alt="image" src="https://github.com/user-attachments/assets/e4749118-37b1-41c1-998c-a6e3f0d57007" />

---

## ③ `.vvproj` を選択
<img width="770" height="268" alt="image" src="https://github.com/user-attachments/assets/f46ff726-5634-4850-950a-a1c979b46a12" />

---

## ④ キャラクターを選択
<img width="753" height="434" alt="image" src="https://github.com/user-attachments/assets/f0ab04d8-71e5-495d-b4cf-05f53dc2c2ca" />

> 💡 **TIP**  
> 「表示」ボタンで「全スタイル表示／先頭スタイルのみ」を切り替えられます  
<img width="390" height="51" alt="image" src="https://github.com/user-attachments/assets/ffedfd4e-48a8-4903-a4f5-ae1af3347331" />

---

## ⑤ 歌声を生成
<img width="741" height="278" alt="image" src="https://github.com/user-attachments/assets/b2a8d578-3bb5-48d4-8c61-3640e1b359f9" />

---

## ⑥ プレビュー再生
<img width="732" height="230" alt="image" src="https://github.com/user-attachments/assets/618a495d-e61b-475a-aeb6-3bb1f672fac4" />

---

## ⑦ 必要に応じて WAV 書き出し
<img width="632" height="296" alt="image" src="https://github.com/user-attachments/assets/61f8abe0-f8b0-4338-83e8-e6f1b8518a38" />

---

## 🔎 現在の仕様メモ

- 読み込んだ `.vvproj` の **1トラック目のみ** が対象
- 音域調整量は各キャラクター／スタイルのデフォルト値を使用  
  →https://github.com/Hiroshiba/voicevox/blob/89e31d1e9e7ae3a2eb4a93ac02c6c483dc1d1070/src/sing/workaroundKeyRangeAdjustment.ts

---

## 技術構成

- **Language**: C++
- **Framework**: JUCE
- **Format**: VST3
- **External**: VOICEVOX Engine API（HTTP）

---

## 🎼 今後の構想：VVChorusPlayer

- 複数キャラによる合唱生成
  - パート管理の強化
  - 歌のばらつきをパラメータとして管理
  - コーラス特化UIの再設計
