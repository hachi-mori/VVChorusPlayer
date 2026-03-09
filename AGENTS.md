# AGENTS instructions

このリポジトリに関する応答は、原則として日本語で行ってください。

## プロジェクト概要

- 本プロジェクトは C++ / JUCE ベースのオーディオプラグインです。
- 主な実装は `Source/`（`PluginProcessor.*` / `PluginEditor.*` / `VoiceVoxClient.*`）にあります。
- `.vvproj` を読み込み、VOICEVOX Engine API（既定: `http://127.0.0.1:50021`）で歌声を生成します。

## 開発環境・ビルド

- 開発環境は Windows + Visual Studio を前提とします。
- ビルド・実行確認は原則として `.sln` から行ってください。
  - 通常利用: `Builds/VisualStudio2026/VVChorusPlayer.sln`
  - 必要時のみ: `Builds/VisualStudio2026/VVProject Synth.sln`
- 特別な理由がない限り、既存のソリューション構成（`Debug/Release`、`x64`）は変更しないでください。

## JUCE 実装ルール

- 既存実装の設計・命名・責務分割に合わせてください（`juce::String`、`juce::Array`、`juce::Result` など）。
- UI スレッドとワーカースレッドを分離し、UI 更新は `juce::MessageManager::callAsync` などメッセージスレッド経由で行ってください。
- 日本語文字列やファイルの文字コードは既存ファイルに合わせ、文字化けを避けてください。
- `Source/keyshift_table.json` と `.vvproj` 処理の互換性を壊さないようにしてください。
- 次の自動生成物は原則として手編集しないでください。
  - `JuceLibraryCode/include_juce_*`
  - `Builds/VisualStudio2026/*.vcxproj` と `*.filters`
- 生成物更新が必要な場合は、まず `VVChorusPlayer.jucer` を起点とした再生成を優先してください。

## 変更時の必須手順

コードやドキュメントを変更した場合は、次の手順を守ってください。

1. `git status` で差分を確認し、コミットを指示された場合のみ意味のある単位で `git commit` すること。コミットメッセージは日本語中心で書いてください。
2. ビルド・実行確認は Visual Studio から手動で行うため、ビルド実行確認は行わないでください。

## 優先順位

- より深い階層に別の `AGENTS.md` がある場合は、その指示を優先してください。