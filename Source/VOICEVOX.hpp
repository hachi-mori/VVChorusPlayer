#pragma once
#include <Siv3D.hpp>

namespace VOICEVOX
{
	struct Singer {
		String name;
		struct Style {
			String name;
			int32 id;
		};
		Array<Style> styles;
	};

	// 歌手名とスタイル名から移調補正値を取得する（該当なしは 0）
	int32 GetKeyAdjustment(const String& singer, const String& style);

	// ScoreQuery.json のノートを ±semitone だけ移調して保存する（key / notelen を更新）
	[[nodiscard]]
	bool TransposeScoreJSON(const FilePath& inPath,
							const FilePath& outPath,
							int semitone);

	// SingQuery.json を ±semitone だけ移調する（f0 と phonemes.note_id を調整）
	[[nodiscard]]
	bool TransposeSingQueryJSON(const FilePath& inPath,
								const FilePath& outPath,
								int semitone);

	// VOICEVOX サーバから Singer 一覧を取得する（タイムアウト時は空配列）
	Array<Singer> GetSingers(const URL& baseURL, Duration timeout = SecondsF{ 5.0 });

	// vvproj のトラック数を取得する（trackOrder があればそのサイズ）
	size_t GetVVProjTrackCount(const FilePath& vvprojPath);

	// vvproj(song) の指定トラックを Score JSON に変換して保存する（frame_length 付き）
	[[nodiscard]]
	bool ConvertVVProjToScoreJSON(const FilePath& vvprojPath,
								  const FilePath& outJsonPath,
								  size_t trackIndex = 0);

	// JSON を VOICEVOX に送信して音声合成し、WAV を保存する
	[[nodiscard]]
	bool SynthesizeFromJSONFile(const FilePath& jsonFilePath,
								const FilePath& savePath,
								const URL& synthesisURL,
								const Duration timeout = SecondsF{ 60.0 });

	// 分割合成ラッパー：ScoreQuery → 分割 SingQuery → WAV 合成 → 連結（keyShift に応じて移調）
	[[nodiscard]]
	bool SynthesizeFromJSONFileWrapperSplit(
		const FilePath& inputPath,
		const FilePath& outputPath,
		const int32& speakerID,
		const URL& baseURL,
		size_t maxFrames = 2500,
		int keyShift = 0
	);

	// talk 音声合成の分割合成ラッパー
	[[nodiscard]]
	bool SynthesizeFromVVProjWrapperSplitTalkJoin(
		const URL& baseURL,
		const FilePath& vvprojPath,
		const FilePath& outputPrefix,
		const FilePath& joinedOutPath,
		int32           speakerID,
		size_t          talkTrackIndex,
		size_t          maxFrames);

	// テキストを VOICEVOX に送信して Talk Query JSON を取得する
	[[nodiscard]]
	JSON CreateQuery(const URL& baseURL, const String text, const int32 speakerID,
		const double intonationScale, const double speedScale, const double volumeScale, const double pitchScale,
		const Duration timeout = SecondsF{ 5.0 });

	// vvproj(talk) を Talk Query JSON に変換して保存する
	[[nodiscard]]
	bool ConvertVVProjToTalkQueryJSON(const URL& baseURL, const FilePath& vvprojPath,
									  const FilePath& outJsonPath,
									  const int32 speakerID, double* outTalkStartSecm, size_t talkTrackIndex);
}
