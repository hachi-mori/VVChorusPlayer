# include "../stdafx.h"

namespace VOICEVOX
{
	// 共有ユーティリティ
	namespace
	{
		constexpr double kFrameRate = 93.75;

		String MidiToName(int midi)
		{
			static const String names[12] =
			{ U"C", U"C#", U"D", U"D#", U"E", U"F",
			  U"F#", U"G", U"G#", U"A", U"A#", U"B" };

			const int octave = midi / 12 - 1;
			return names[midi % 12] + Format(octave);
		}

		int CalcFrameLen(int64 ticks, double bpm, double tpqn, double& carry)
		{
			const double beats = ticks / tpqn;
			const double sec = beats * (60.0 / bpm);
			const double frames = sec * kFrameRate + carry;

			const int   flen = Max(1, static_cast<int>(std::floor(frames + 0.5)));
			carry = frames - flen;        // 誤差キャリー
			return flen;
		}

		double GetFirstBPM(const JSON& song)
		{
			if (song[U"tempos"].isArray()
			 && (song[U"tempos"].arrayView().begin() != song[U"tempos"].arrayView().end())
			 && song[U"tempos"][0][U"bpm"].isNumber())
			{
				return song[U"tempos"][0][U"bpm"].get<double>();
			}
			return 120.0;  // デフォルト
		}
	} // unnamed-namespace

	// 音域調整テーブル（キャラ名 → スタイル名 → 音域調整値）
	static const HashTable<String, HashTable<String, int32>> kKeyAdjustmentTable = {
		{ U"四国めたん",    {{U"ノーマル",-4},{U"あまあま",-4},{U"ツンツン",-5},{U"セクシー",-4},{U"ヒソヒソ",-9}} },
		{ U"ずんだもん",    {{U"ノーマル",-2},{U"あまあま",0},{U"ツンツン",-3},{U"セクシー",0},{U"ヒソヒソ",-7},{U"ヘロヘロ",-3},{U"なみだめ",6}} },
		{ U"春日部つむぎ",  {{U"ノーマル",-2}} },
		{ U"雨晴はう",      {{U"ノーマル",0}} },
		{ U"波音リツ",      {{U"ノーマル",-8},{U"クイーン",-5}} },
		{ U"玄野武宏",      {{U"ノーマル",-17},{U"喜び",-9},{U"ツンギレ",-14},{U"悲しみ",-18}} },
		{ U"白上虎太郎",    {{U"ふつう",-14},{U"わーい",-8},{U"びくびく",-7},{U"おこ",-9},{U"びえーん",-3}} },
		{ U"青山龍星",      {{U"ノーマル",-22},{U"熱血",-18},{U"不機嫌",-23},{U"喜び",-21},{U"しっとり",-27},{U"かなしみ",-22}} },
		{ U"冥鳴ひまり",    {{U"ノーマル",-7}} },
		{ U"九州そら",      {{U"ノーマル",-7},{U"あまあま",-2},{U"ツンツン",-6},{U"セクシー",-4}} },
		{ U"もち子さん",    {{U"ノーマル",-5},{U"セクシー／あん子",-7},{U"泣き",-2},{U"怒り",-3},{U"喜び",-2},{U"のんびり",-5}} },
		{ U"剣崎雌雄",      {{U"ノーマル",-18}} },
		{ U"WhiteCUL",      {{U"ノーマル",-6},{U"たのしい",-3},{U"かなしい",-7},{U"びえーん",0}} },
		{ U"後鬼",          {{U"人間ver.",-7},{U"ぬいぐるみver.",-2}} },
		{ U"No.7",         {{U"ノーマル",-8},{U"アナウンス",-10},{U"読み聞かせ",-9}} },
		{ U"ちび式じい",    {{U"ノーマル",-18}} },
		{ U"櫻歌ミコ",      {{U"ノーマル",-6},{U"第二形態",-12},{U"ロリ",-7}} },
		{ U"小夜/SAYO",     {{U"ノーマル",-4}} },
		{ U"ナースロボ＿タイプＴ", {{U"ノーマル",-6},{U"楽々",-3},{U"恐怖",-4}} },
		{ U"†聖騎士 紅桜†", {{U"ノーマル",-15}} },
		{ U"雀松朱司",      {{U"ノーマル",-21}} },
		{ U"麒ヶ島宗麟",    {{U"ノーマル",-17}} },
		{ U"春歌ナナ",      {{U"ノーマル",-2}} },
		{ U"猫使アル",      {{U"ノーマル",-8},{U"おちつき",-9},{U"うきうき",-7}} },
		{ U"猫使ビィ",      {{U"ノーマル",-1},{U"おちつき",-3}} },
		{ U"中国うさぎ",    {{U"ノーマル",-8},{U"おどろき",-4},{U"こわがり",-2},{U"へろへろ",-4}} },
		{ U"栗田まろん",    {{U"ノーマル",-14}} },
		{ U"あいえるたん",  {{U"ノーマル",-2}} },
		{ U"満別花丸",      {{U"ノーマル",-4},{U"元気",2},{U"ささやき",-33},{U"ぶりっ子",0},{U"ボーイ",-10}} },
		{ U"琴詠ニア",      {{U"ノーマル",-4}} },
	};

	/// @brief 歌手名+スタイル名から音域調整量を取得します。
	/// @param singer 歌手名（例: U"ずんだもん"）
	/// @param style  スタイル名（例: U"ノーマル"）
	/// @return 音域調整量。見つからない場合は 0 を返します。
	int32 GetKeyAdjustment(const String& singer,
							const String& style)
	{
		auto itSinger = kKeyAdjustmentTable.find(singer);
		if (itSinger != kKeyAdjustmentTable.end()) {
			const auto& styleTable = itSinger->second;

			auto itStyle = styleTable.find(style);
			if (itStyle != styleTable.end()) {
				return itStyle->second;
			}
		}
		return 0;
	}

	/// @brief song スコア(JSON)の全ノートを半音単位で移調して保存します。
	/// @param inPath  入力の song スコア JSON ファイルパス
	/// @param outPath 出力（移調後）の JSON ファイルパス
	/// @param semitone 移調量（半音）。上げる=正、下げる=負。
	/// @return 保存に成功した場合 true、失敗した場合 false を返します。
	bool TransposeScoreJSON(const FilePath& inPath,
							const FilePath& outPath,
							int semitone)
	{
		JSON score = JSON::Load(inPath);
		if (!score || !score.contains(U"notes") || !score[U"notes"].isArray())
			return false;

		const size_t noteCount = score[U"notes"].size();

		for (size_t i = 0; i < noteCount; ++i)
		{
			JSON n = score[U"notes"][i];                 // 値で取得（コピー）

			if (auto midiOpt = n[U"key"].getOpt<int32>())
			{
				int32 midi = Clamp<int32>(*midiOpt + semitone, 0, 127);
				n[U"key"] = midi;
				n[U"notelen"] = MidiToName(midi);
			}
			score[U"notes"][i] = n;                      // 書き戻し
		}
		return score.save(outPath);
	}

	/// @brief song クエリ(JSON)の全ノートを半音単位で移調して保存します。
	/// @param inPath  入力の song クエリ JSON ファイルパス
	/// @param outPath 出力（移調後）の JSON ファイルパス
	/// @param semitone 移調量（半音）。上げる=正、下げる=負。
	/// @return 保存に成功した場合 true、失敗した場合 false を返します。
	bool TransposeSingQueryJSON(const FilePath& inPath,
								const FilePath& outPath,
								int semitone)
	{
		JSON q = JSON::Load(inPath);
		if (!q) return false;

		const double ratio = std::pow(2.0, semitone / 12.0);

		/* --- f0 カーブ --- */
		if (q.contains(U"f0") && q[U"f0"].isArray())
		{
			const size_t f0Count = q[U"f0"].size();
			for (size_t i = 0; i < f0Count; ++i)
			{
				if (auto d = q[U"f0"][i].getOpt<double>())
				{
					q[U"f0"][i] = (*d) * ratio;         // 直接上書き
				}
			}
		}

		/* --- phonemes.note_id --- */
		if (q.contains(U"phonemes") && q[U"phonemes"].isArray())
		{
			const size_t phCount = q[U"phonemes"].size();
			for (size_t i = 0; i < phCount; ++i)
			{
				JSON p = q[U"phonemes"][i];             // コピー
				if (auto nOpt = p[U"note_id"].getOpt<int32>())
				{
					p[U"note_id"] = *nOpt + semitone;
				}
				q[U"phonemes"][i] = p;                  // 書き戻し
			}
		}
		return q.save(outPath);
	}

	/// @brief VOICEVOX API /singers から歌手リストを取得します。
	/// @param baseURL VOICEVOX エンジンのベースURL
	/// @param timeout タイムアウト時間
	/// @return Singer の配列。失敗した場合は空配列を返します。
	Array<Singer> GetSingers(const URL& baseURL, const Duration timeout)
	{
		const URL url = U"{}/singers"_fmt(baseURL);

		AsyncHTTPTask task = SimpleHTTP::GetAsync(url, {});
		Stopwatch     sw{ StartImmediately::Yes };

		while (!task.isReady())
		{
			if (timeout <= sw)
			{
				task.cancel();
				return{};
			}
			System::Sleep(1ms);
		}
		if (!task.getResponse().isOK())
			return{};

		const JSON json = task.getAsJSON();
		Array<Singer> out;

		for (auto&& [_, sp] : json)
		{
			Singer s;  s.name = sp[U"name"].get<String>();

			for (auto&& [__, st] : sp[U"styles"])
			{
				Singer::Style tmp;
				tmp.name = st[U"name"].get<String>();
				tmp.id = st[U"id"].get<int32>();
				s.styles << tmp;
			}
			out << s;
		}
		return out;
	}

	/// @brief VOICEVOX プロジェクトファイル（.vvproj）からトラック数を取得します。
	/// @param vvprojPath VOICEVOXプロジェクトファイルのパス
	/// @return トラック数。取得に失敗した場合は 0 を返します。
	size_t GetVVProjTrackCount(const FilePath& vvprojPath)
	{
		const JSON src = JSON::Load(vvprojPath);
		const JSON& song = src[U"song"];

		if (!song || !song[U"tracks"].isObject())
			return 0;

		if (song[U"trackOrder"].isArray())
			return song[U"trackOrder"].size();

		return song[U"tracks"].size();
	}

	/// @brief VOICEVOX プロジェクトファイル（.vvproj）から指定トラックを抽出し、song スコア JSON に変換して保存します。
	/// @param vvprojPath 入力の VOICEVOX プロジェクトファイルのパス
	/// @param outJsonPath 出力（変換後）の song スコア JSON ファイルのパス
	/// @param trackIndex 抽出するトラックのインデックス（0～）
	/// @return 変換に成功した場合 true、失敗した場合 false を返します。
	bool ConvertVVProjToScoreJSON(const FilePath& vvprojPath,
								  const FilePath& outJsonPath,
								  size_t trackIndex)
	{
		const JSON src = JSON::Load(vvprojPath);
		const JSON& song = src[U"song"];

		if (!song || !song[U"tracks"].isObject())
			return false;

		// ---------------------------------- ① 目的トラック抽出
		JSON track;
		bool found = false;

		if (song[U"trackOrder"].isArray() &&
			trackIndex < song[U"trackOrder"].size())
		{
			const String key = song[U"trackOrder"][trackIndex].getString();
			if (song[U"tracks"][key].isObject())
			{
				track = song[U"tracks"][key];
				found = true;
			}
		}
		if (!found)                                   // trackOrder 無し fallback
		{
			size_t cur = 0;
			for (auto&& [__, tr] : song[U"tracks"])
			{
				if (cur == trackIndex)
				{
					track = tr;
					found = true;
					break;
				}
				++cur;
			}
		}
		if (!found || !track[U"notes"].isArray())
			return false;

		// ---------------------------------- ② スコア変換
		const double tpqn = song[U"tpqn"].isNumber()
			? song[U"tpqn"].get<double>() : 480.0;
		const double bpm = GetFirstBPM(song);

		Array<JSON> outNotes;
		double carry = 0.0;

		auto putRest = [&](int f)
			{
				JSON r;
				r[U"frame_length"] = f;
				r[U"key"] = JSON();
				r[U"lyric"] = U"";
				r[U"notelen"] = U"R";
				outNotes << r;
			};
		putRest(2);                                   // 開頭休符 2frame

		int64 prevEnd = 0;
		for (auto&& note : track[U"notes"].arrayView())
		{
			const int64 pos = note[U"position"].get<int64>();
			const int64 dur = note[U"duration"].get<int64>();
			const int   midi = note[U"noteNumber"].get<int>();
			const String lyr = note[U"lyric"].getString();

			if (const int64 gap = pos - prevEnd; gap > 0)
				putRest(CalcFrameLen(gap, bpm, tpqn, carry));

			JSON n;
			n[U"frame_length"] = CalcFrameLen(dur, bpm, tpqn, carry);
			n[U"key"] = midi;
			n[U"lyric"] = lyr;
			n[U"notelen"] = MidiToName(midi);
			outNotes << n;

			prevEnd = pos + dur;
		}
		putRest(2);                                   // 終端休符

		JSON res; res[U"notes"] = outNotes;
		res.save(outJsonPath);
		return true;
	}

	/// @brief VOICEVOX API /frame_synthesis に song クエリ JSON ファイルを POST して歌声合成し、WAV ファイルとして保存します。
	/// @param jsonFilePath 入力の song クエリ JSON ファイルパス
	/// @param savePath 出力の WAV ファイルパス
	/// @param synthesisURL 歌声合成の API エンドポイント URL
	/// @param timeout タイムアウト時間
	/// @return 成功した場合 true、失敗した場合 false を返します。
	bool SynthesizeFromJSONFile(const FilePath& jsonFilePath,
								const FilePath& savePath,
								const URL& synthesisURL,
								const Duration timeout)
	{
		// JSON ファイルを読み込む
		const JSON query = JSON::Load(jsonFilePath);

		if (not query)
		{
			Console(U"JSON ファイルの読み込みに失敗しました。");
			return false;
		}

		// JSON データを UTF-8 フォーマットに変換
		const std::string data = query.formatUTF8Minimum();
		const HashTable<String, String> headers{ { U"Content-Type", U"application/json" } };

		// 非同期 POST リクエストを送信
		AsyncHTTPTask task = SimpleHTTP::PostAsync(synthesisURL, headers, data.data(), data.size(), savePath);

		Stopwatch stopwatch{ StartImmediately::Yes };

		// リクエスト完了またはタイムアウトまで待機
		while (not task.isReady())
		{
			if (timeout <= stopwatch)
			{
				task.cancel();

				// タイムアウト時に不完全なファイルを削除
				if (FileSystem::IsFile(savePath))
				{
					FileSystem::Remove(savePath);
				}

				Console(U"リクエストがタイムアウトしました: " + savePath);
				return false;
			}

			System::Sleep(1ms);
		}

		// レスポンスが成功したかを確認
		if (not task.getResponse().isOK())
		{
			// 失敗時に不完全なファイルを削除
			if (FileSystem::IsFile(savePath))
			{
				FileSystem::Remove(savePath);
			}

			Console(U"リクエストが失敗しました: " + savePath);
			return false;
		}

		Console(U"ファイルが保存されました: " + savePath);
		return true;
	}

	/// @brief song 歌声合成の分割合成ラッパー。
	/// @param inputPath 入力　の song クエリ JSON ファイルパス
	/// @param outputPath 出力 の WAV ファイルパス
	/// @param speakerID 歌声ID
	/// @param baseURL VOICEVOX エンジンのベースURL
	/// @param maxFrames song クエリ分割時の 1 セグメントあたりの最大フレーム数
	/// @param keyShift 移調量（半音）。上げる=正、下げる=負。
	/// @return 成功した場合 true、失敗した場合 false を返します。
	bool VOICEVOX::SynthesizeFromJSONFileWrapperSplit(
		const FilePath& inputPath,
		const FilePath& outputPath,
		const int32& speakerID,
		const URL& baseURL,
		size_t          maxFrames,
		int             keyShift)
	{
		//──────────────────── ⓪ 条件付きで、Score をオク下にする ────────────────────
		const int octave = 12;
		if (keyShift < -12) // 基準値以下はオク下
		{
			if (!VOICEVOX::TransposeScoreJSON(inputPath, inputPath, -octave)) // -12でオク下になる
			{
				Console(U"Score 移調に失敗しました");
				return false;
			}
		}
		if (keyShift < -20) // 基準値以下はさらにオク下
		{
			if (!VOICEVOX::TransposeScoreJSON(inputPath, inputPath, -octave)) // -12でオク下になる
			{
				Console(U"Score 移調に失敗しました");
				return false;
			}
		}

		//──────────────────── ① Score を移調（+方向 = -keyShift） ────────────────────
		if (keyShift != 0)
		{
			// 例: keyShift = -3 → Score を +3 半音上げる
			if (!VOICEVOX::TransposeScoreJSON(inputPath, inputPath, -keyShift))
			{
				Console(U"Score 移調に失敗しました");
				return false;
			}
		}

		//──────────────────── ② Score JSON 読込 & notes 分割 ────────────────────────
		JSON score = JSON::Load(inputPath);
		if (!score)
		{
			Console(U"Score JSON の読み込み失敗");
			return false;
		}

		const auto& notes = score[U"notes"];
		if (!notes.isArray())
		{
			Console(U"notes が配列ではありません");
			return false;
		}

		Array<JSON> segments;
		Array<JSON> currentSegment;
		size_t frameSum = 0;
		Optional<JSON> carriedOverRest;

		for (const auto& note : notes.arrayView())
		{
			currentSegment << note;
			frameSum += note[U"frame_length"].get<size_t>();
			const bool isRest = (note[U"notelen"].getString() == U"R");

			if (frameSum >= maxFrames && isRest)
			{
				// ─ 休符を 2 分割してセグメント境界にする ─
				const size_t restFrame = note[U"frame_length"].get<size_t>();
				const size_t half1 = restFrame / 2;
				const size_t half2 = restFrame - half1;

				if (!currentSegment.isEmpty())
				{
					currentSegment.pop_back(); // 元休符を除去

					JSON rest1;
					rest1[U"frame_length"] = half1;
					rest1[U"key"] = JSON();
					rest1[U"lyric"] = U"";
					rest1[U"notelen"] = U"R";
					currentSegment << rest1;
				}

				JSON rest2;
				rest2[U"frame_length"] = half2;
				rest2[U"key"] = JSON();
				rest2[U"lyric"] = U"";
				rest2[U"notelen"] = U"R";
				carriedOverRest = rest2;

				JSON segJson;
				segJson[U"notes"] = currentSegment;
				segments << segJson;

				// リセット
				currentSegment.clear();
				frameSum = 0;

				currentSegment << *carriedOverRest;
			}
		}

		if (!currentSegment.isEmpty())
		{
			JSON segJson;
			segJson[U"notes"] = currentSegment;
			segments << segJson;
		}

		//──────────────────── ③ 各セグメントで合成 ───────────────────────────────
		Array<FilePath> tempWavs;
		for (size_t i = 0; i < segments.size(); ++i)
		{
			// 一時ファイル群
			const FilePath tmpScore = U"tmp/tmp_score_" + Format(i) + U".json";
			const FilePath tmpQuery = U"tmp/tmp_query_" + Format(i) + U".json";
			const FilePath tmpWav = U"tmp/tmp_part_" + Format(i) + U".wav";
			segments[i].save(tmpScore);

			// ScoreQuery → SingQuery
			const URL queryURL = U"{}/sing_frame_audio_query?speaker=6000"_fmt(baseURL);
			if (!VOICEVOX::SynthesizeFromJSONFile(tmpScore, tmpQuery, queryURL))
			{
				Console(U"SingQuery 作成失敗 (分割 " + Format(i) + U")");
				return false;
			}

			//──────────────── ④ SingQuery を -keyShift 半音戻す ────────────────
			if (keyShift != 0 &&
				!VOICEVOX::TransposeSingQueryJSON(tmpQuery, tmpQuery, keyShift))
			{
				Console(U"SingQuery 移調に失敗しました");
				return false;
			}

			// 音量などのメタ調整
			if (JSON query = JSON::Load(tmpQuery))
			{
				query[U"volumeScale"] = 1.0;
				query[U"outputSamplingRate"] = 44100;
				query[U"outputStereo"] = true;
				query.save(tmpQuery);
			}

			// SingQuery → WAV
			const URL songsynthURL = U"{}/frame_synthesis?speaker={}"_fmt(baseURL,speakerID);
			if (!VOICEVOX::SynthesizeFromJSONFile(tmpQuery, tmpWav, songsynthURL))
			{
				Console(U"音声合成失敗 (分割 " + Format(i) + U")");
				return false;
			}
			tempWavs << tmpWav;
		}

		//──────────────────── ⑤ 分割 WAV を連結 ──────────────────────────────
		Wave joined;
		for (const auto& wav : tempWavs)
		{
			Wave part{ wav };
			joined.append(part);
		}
		joined.save(outputPath);

		//──────────────────── ⑥ 一時ファイルを掃除 ────────────────────────────
		for (size_t i = 0; i < segments.size(); ++i)
		{
			FileSystem::Remove(U"tmp/tmp_score_" + Format(i) + U".json");
			FileSystem::Remove(U"tmp/tmp_query_" + Format(i) + U".json");
			FileSystem::Remove(U"tmp/tmp_part_" + Format(i) + U".wav");
		}

		Console(U"分割合成＆連結が完了: " + outputPath);
		return true;
	}

	namespace {
		// 共通：index 指定で 1 トラック取得（trackOrder 優先）
		static bool SelectTrackByIndex(const JSON& song, size_t index, JSON& outTrack)
		{
			if (!song || !song[U"tracks"].isObject()) return false;

			if (song[U"trackOrder"].isArray() && index < song[U"trackOrder"].size())
			{
				const String key = song[U"trackOrder"][index].getString();
				if (song[U"tracks"][key].isObject())
				{
					outTrack = song[U"tracks"][key];
					return true;
				}
			}
			// fallback: trackOrder が無い or index 超過時は tracks を列挙
			size_t cur = 0;
			for (auto&& [__, tr] : song[U"tracks"])
			{
				if (cur == index) { outTrack = tr; return true; }
				++cur;
			}
			return false;
		}
	}

	/// @brief talk 音声合成の分割合成ラッパー。
	/// @param baseURL VOICEVOX エンジンのベースURL
	/// @param vvprojPath 入力の VOICEVOX プロジェクトファイルパス（.vvproj）
	/// @param outputPrefix 出力の分割 WAV ファイル接頭辞（prefix_0.wav, ...）
	/// @param joinedOutPath 出力の連結 WAV ファイルパス
	/// @param speakerID 使用する talk 話者 ID
	/// @param talkTrackIndex 対象トラックのインデックス
	/// @param maxFrames 分割合成時の1セグメントあたりの最大フレーム数（93.75fps換算）
	/// @return 成功した場合 true、失敗した場合 false を返します。
	bool SynthesizeFromVVProjWrapperSplitTalkJoin(
		const URL& baseURL,
		const FilePath& vvprojPath,
		const FilePath& outputPrefix,
		const FilePath& joinedOutPath,
		const int32     speakerID,
		const size_t    talkTrackIndex,
		const size_t    maxFrames)
	{
		FileSystem::CreateDirectories(U"tmp");

		// 1) vvproj 読み込み
		const JSON src = JSON::Load(vvprojPath);
		if (!src || !src.contains(U"song") || !src[U"song"].isObject()) return false;
		const JSON& song = src[U"song"];
		if (!song.contains(U"tracks") || !song[U"tracks"].isObject()) return false;

		// 2) 対象トラック抽出（trackOrder 優先）
		JSON track;
		if (!SelectTrackByIndex(song, talkTrackIndex, track) || !track[U"notes"].isArray()) return false;

		// 3) notes を position 昇順に
		Array<JSON> notesVec;
		for (auto&& n : track[U"notes"].arrayView()) notesVec << n;
		if (notesVec.isEmpty()) return false;
		std::sort(notesVec.begin(), notesVec.end(),
			[](const JSON& a, const JSON& b) { return a[U"position"].get<int64>() < b[U"position"].get<int64>(); });

		// 4) 変換器
		const double tpqn = song[U"tpqn"].isNumber() ? song[U"tpqn"].get<double>() : 480.0;
		const double bpm = VOICEVOX::GetFirstBPM(song);
		auto ticksToSec = [&](int64 ticks) { return (static_cast<double>(ticks) / tpqn) * (60.0 / bpm); };
		auto ticksToFrames = [&](int64 ticks, double& carry) { return static_cast<size_t>(CalcFrameLen(ticks, bpm, tpqn, carry)); };

		// 5) 休符でのみ分割（gapsSec も収集）
		Array<Array<JSON>> segments;
		Array<double>      gapsSec;   // segments.size()-1 と一致
		Array<JSON>        current;
		size_t             frameSum = 0;
		double             carry = 0.0;

		{
			const JSON& n0 = notesVec.front();
			const int64 dur = n0[U"duration"].get<int64>();
			current << n0;
			frameSum += ticksToFrames(dur, carry);
		}
		int64 prevEnd = notesVec.front()[U"position"].get<int64>() + notesVec.front()[U"duration"].get<int64>();

		for (size_t i = 1; i < notesVec.size(); ++i)
		{
			const JSON& note = notesVec[i];
			const int64 pos = note[U"position"].get<int64>();
			const int64 dur = note[U"duration"].get<int64>();

			if (pos > prevEnd)
			{
				const int64 gapTick = (pos - prevEnd);
				const size_t gapFrame = ticksToFrames(gapTick, carry);
				const double gapSec = ticksToSec(gapTick);

				if (frameSum + gapFrame >= maxFrames)
				{
					segments << current;
					current.clear();
					frameSum = 0;
					carry = 0.0;
					gapsSec << (Max(0.0, gapSec) - 0.2585); // この境界の休符秒 0.260は微調整
				}
				else
				{
					frameSum += gapFrame;
				}
			}

			current << note;
			frameSum += ticksToFrames(dur, carry);
			prevEnd = Max(prevEnd, pos + dur);
		}
		if (!current.isEmpty()) segments << current;

		if (segments.isEmpty() || (gapsSec.size() + 1 != segments.size())) return false;

		// 6) 各セグメント: 一時 vvproj → ConvertVVProjToTalkQueryJSON → /synthesis
		Array<FilePath> partWavs;
		for (size_t si = 0; si < segments.size(); ++si)
		{
			JSON tmpVVProj = src;
			JSON tmpTrack = track;
			tmpTrack[U"notes"] = segments[si];

			static constexpr StringView kSegKey = U"__seg__";
			tmpVVProj[U"song"][U"tracks"][kSegKey] = tmpTrack;
			tmpVVProj[U"song"][U"trackOrder"] = Array<String>{ String{kSegKey} };

			const FilePath tmpVvprojPath = U"tmp/tmp_talkseg_" + Format(si) + U".vvproj";
			if (!tmpVVProj.save(tmpVvprojPath)) continue;

			const FilePath tmpQueryPath = U"tmp/tmp_talkquery_" + Format(si) + U".json";
			double dummyStart = 0.0;
			if (!VOICEVOX::ConvertVVProjToTalkQueryJSON(baseURL, tmpVvprojPath, tmpQueryPath, speakerID, &dummyStart, 0)) continue;

			const FilePath outWav = outputPrefix + U"_" + Format(si) + U".wav";
			const URL synthesisURL = U"{}/synthesis?speaker={}"_fmt(baseURL, speakerID);
			if (!VOICEVOX::SynthesizeFromJSONFile(tmpQueryPath, outWav, synthesisURL)) continue;

			partWavs << outWav;
		}
		if (partWavs.isEmpty()) return false;

		// 7) 無音を挟んで連結（Siv3D: Wave は常にステレオ / ch 指定は不要）
		auto makeSilence = [](double seconds, uint32 sampleRate)->Wave
			{
				const size_t samples = static_cast<size_t>(Max(0.0, seconds) * sampleRate);
				return Wave{ samples, sampleRate }; // ゼロ初期化のステレオ無音
			};

		Wave joined;
		uint32 sr = 44100;

		for (size_t i = 0; i < partWavs.size(); ++i)
		{
			Wave part{ partWavs[i] };
			if (part.isEmpty()) return false;

			if (i == 0)
			{
				sr = part.sampleRate();
				joined = part;              // 1本目をそのまま基準に
			}
			else
			{
				joined.append(part);        // 2本目以降を連結
			}

			if (i + 1 < partWavs.size())
			{
				const double gap = gapsSec[i];
				if (gap > 0.0)
				{
					joined.append(makeSilence(gap, sr)); // 無音を挟む
				}
			}
		}

		return joined.save(joinedOutPath);
	}

	/// @brief テキストから talk 音声合成用 クエリ JSON ファイルを作成して保存します。
	/// @param baseURL VOICEVOX エンジンのベースURL
	/// @param text 音声合成するテキスト
	/// @param speakerID talk 音声合成に使用する話者 ID
	/// @param intonationScale 抑揚
	/// @param speedScale 話速
	/// @param volumeScale 音量
	/// @param pitchScale ピッチ
	/// @param timeout タイムアウト時間
	/// @return クエリ作成に成功した場合 取得したクエリ JSON。失敗した場合は JSON::Invalid() を返します。
	JSON CreateQuery(const URL& baseURL, const String text, const int32 speakerID,
		const double intonationScale, const double speedScale, const double volumeScale, const double pitchScale,
		const Duration timeout)
	{
		const URL url = U"{}/audio_query?text={}&speaker={}"_fmt(baseURL, PercentEncode(text), speakerID);
		const std::string data = JSON{}.formatUTF8Minimum();
		AsyncHTTPTask task = SimpleHTTP::PostAsync(url, {}, data.data(), data.size());
		Stopwatch stopwatch{ StartImmediately::Yes };

		while (not task.isReady())
		{
			if (timeout <= stopwatch)
			{
				task.cancel();
				return JSON::Invalid();
			}

			System::Sleep(1ms);
		}

		if (not task.getResponse().isOK())
		{
			return JSON::Invalid();
		}

		JSON query = task.getAsJSON();
		query[U"intonationScale"] = intonationScale;
		query[U"speedScale"] = speedScale;
		query[U"volumeScale"] = volumeScale;
		query[U"pitchScale"] = pitchScale;

		return query;
	}

	/// @brief VOICEVOX プロジェクトファイル（.vvproj）を解析し、talk 音声合成用のクエリ JSON を作成して保存します。
	/// @param baseURL VOICEVOX エンジンのベースURL
	/// @param vvprojPath 入力の VOICEVOX プロジェクトファイルのパス
	/// @param outJsonPath 出力のクエリ JSON ファイルパス
	/// @param speakerID 使用する話者 ID
	/// @param outTalkStartSec トーク開始位置（秒）を格納するポインタ（不要なら nullptr）
	/// @param talkTrackIndex 解析対象トラックのインデックス（0～）
	/// @return 成功した場合 true、失敗した場合 false を返します。
	bool ConvertVVProjToTalkQueryJSON(const URL& baseURL, const FilePath& vvprojPath,
									  const FilePath& outJsonPath,
									  const int32 speakerID, double* outTalkStartSec, size_t talkTrackIndex)
	{
		// ① vvproj ロード & talk 存在チェック
		const JSON src = JSON::Load(vvprojPath);
		if (!src) return false;

		// --- song から talkStartSec 算出（talk 用に選んだトラックの最初のノート基準）
		double talkStartSecLocal = 0.0;
		if (src.contains(U"song") && src[U"song"].isObject())
		{
			const JSON& song = src[U"song"];
			const double tpqn = song[U"tpqn"].isNumber() ? song[U"tpqn"].get<double>() : 480.0;
			const double bpm = VOICEVOX::GetFirstBPM(song);
			JSON track;
			if (SelectTrackByIndex(song, talkTrackIndex, track) && track[U"notes"].isArray())
			{
				Optional<int64> earliestTick;
				for (auto&& n : track[U"notes"].arrayView())
				{
					if (!n[U"position"].isNumber()) continue;
					const int64 pos = n[U"position"].get<int64>();
					if (!earliestTick || pos < *earliestTick) earliestTick = pos;
				}
				if (earliestTick)
				{
					const double beats = static_cast<double>(*earliestTick) / tpqn;
					talkStartSecLocal = beats * (60.0 / bpm);
				}
			}
		}
		if (outTalkStartSec) *outTalkStartSec = talkStartSecLocal;

		// ③★ 指定された 1 トラックだけから lyric を結合し、ノート間の休符には「、」を挿入
		String text;
		int insertedCommas = 0;       // デバッグ用: 追加した読点の数
		bool appendedLyric = false;    // 少なくとも1つは歌詞を追加できたか

		if (src.contains(U"song") && src[U"song"].isObject())
		{
			const JSON& song = src[U"song"];
			JSON track;
			if (SelectTrackByIndex(song, talkTrackIndex, track) && track[U"notes"].isArray())
			{
				// position 昇順に並べ替えたノート列（歌詞が空でも並べる）
				struct NoteRow { int64 pos; int64 dur; String lyr; bool hasLyric; };
				Array<NoteRow> rows;

				for (auto&& n : track[U"notes"].arrayView())
				{
					const auto posOpt = n[U"position"].getOpt<int64>();
					const auto durOpt = n[U"duration"].getOpt<int64>();
					const auto lyricOpt = n[U"lyric"].getOpt<String>();
					if (!posOpt || !durOpt) continue;

					NoteRow r;
					r.pos = *posOpt;
					r.dur = *durOpt;
					r.hasLyric = (lyricOpt && !lyricOpt->isEmpty());
					r.lyr = r.hasLyric ? *lyricOpt : U"";
					rows << r;
				}

				if (!rows.empty())
				{
					std::sort(rows.begin(), rows.end(),
							  [](const NoteRow& a, const NoteRow& b) { return a.pos < b.pos; });

					// シーケンスをなめて、ノート間ギャップがあれば「、」を挿入してから次の歌詞を連結
					int64 prevEnd = rows.front().pos + rows.front().dur;

					// 先頭ノートの歌詞
					if (rows.front().hasLyric) { text += rows.front().lyr; appendedLyric = true; }

					for (size_t i = 1; i < rows.size(); ++i)
					{
						const auto& cur = rows[i];

						// 休符検出: 現在ノートの開始posが直前ノートの終端より後ろならギャップあり
						if (cur.pos > prevEnd)
						{
							text += U"、";
							++insertedCommas;
						}

						// 現在ノートの歌詞（空はスキップ）
						if (cur.hasLyric) { text += cur.lyr; appendedLyric = true; }

						// 次のための終端更新
						prevEnd = Max(prevEnd, cur.pos + cur.dur);
					}
				}
			}
		}

		// ★ 歌詞が1つも無かった → 以降処理を行わずスキップ（読点だけのtextは無効）
		if (!appendedLyric)
		{
			const String base = FileSystem::BaseName(vvprojPath);
			Console << U"[Talk] skip: lyricなし (file={}, trackIndex={}トラック目, insertedComma={})"_fmt(base, talkTrackIndex + 1, insertedCommas);
			return false;
		}

		// 参考: デバッグログ（必要なら）
		Console << U"[Talk] text完成 (len={}, insertedComma={}, trackIndex={}トラック目)"_fmt(text.size(), insertedCommas, talkTrackIndex + 1);


		// ④ /audio_query に投げてベースクエリを取得
		JSON query = CreateQuery(
			baseURL,
			text, speakerID,
			/*intonationScale*/ 1.0,
			/*speedScale*/      1.0,
			/*volumeScale*/     1.0,
			/*pitchScale*/      0.0,
			SecondsF{ 5.0 });

		if (!query)
		{
			return false;
		}

		// ⑤’ notes（position/duration）から mora 長さ／pause_mora を再計算して、④で作成した query に上書き
		do
		{
			// 前提チェック
			if (!src.contains(U"song") || !src[U"song"].isObject()) break;
			if (!query.contains(U"accent_phrases") || !query[U"accent_phrases"].isArray()) break;

			const JSON& song = src[U"song"];

			// ---- talk 用に参照する 1 トラックを特定（trackOrder 優先）----
			JSON track;
			bool found = false;
			if (song[U"trackOrder"].isArray() && (talkTrackIndex < song[U"trackOrder"].size()))
			{
				const String key = song[U"trackOrder"][talkTrackIndex].getString();
				if (song[U"tracks"][key].isObject()) { track = song[U"tracks"][key]; found = true; }
			}
			if (!found)
			{
				size_t cur = 0;
				for (auto&& [__, tr] : song[U"tracks"])
				{
					if (cur == talkTrackIndex) { track = tr; found = true; break; }
					++cur;
				}
			}
			if (!found || !track.contains(U"notes") || !track[U"notes"].isArray()) break;

			// ---- tick→秒 （先頭テンポのみ使用。可変テンポ対応が必要ならここを差し替え）----
			const double tpqn = song[U"tpqn"].isNumber() ? song[U"tpqn"].get<double>() : 480.0;
			const double bpm = VOICEVOX::GetFirstBPM(song);
			auto ticksToSec = [&](int64 ticks) -> double
				{
					return (static_cast<double>(ticks) / tpqn) * (60.0 / bpm);
				};
			auto round6 = [](double x) -> double { return std::round(x * 1'000'000.0) / 1'000'000.0; };

			// ---- query 側：配分対象の mora を列挙（読点「、」は除外）。pause_mora は初期化 ----
			const size_t apCount = query[U"accent_phrases"].size();
			Array<std::pair<size_t, size_t>> moraMap; // (apIdx, moraIdx)
			moraMap.reserve(256);

			for (size_t i = 0; i < apCount; ++i)
			{
				JSON ap = query[U"accent_phrases"][i];  // コピー

				// pause_mora はあとで休符から再構成するため一旦クリア
				ap[U"pause_mora"] = JSON();             // null
				query[U"accent_phrases"][i] = ap;       // 書き戻し

				if (!ap.contains(U"moras") || !ap[U"moras"].isArray()) continue;

				const size_t mCount = ap[U"moras"].size();
				for (size_t j = 0; j < mCount; ++j)
				{
					const String t = ap[U"moras"][j][U"text"].getOr<String>(U"");
					if (t == U"、") continue;            // 読点モーラは配分対象外
					moraMap << std::make_pair(i, j);
				}
			}

			// ---- 楽譜ノート列（position 昇順）。歌詞の有無は mora 進行に使う ----
			struct NoteRow { int64 pos; int64 dur; bool hasLyric; };
			Array<NoteRow> rows;

			for (auto&& n : track[U"notes"].arrayView())
			{
				const auto posOpt = n[U"position"].getOpt<int64>();
				const auto durOpt = n[U"duration"].getOpt<int64>();
				const bool hasLyric = n[U"lyric"].getOpt<String>().has_value()
					&& !n[U"lyric"].get<String>().isEmpty();
				if (!posOpt || !durOpt) continue;
				rows << NoteRow{ *posOpt, *durOpt, hasLyric };
			}
			if (rows.isEmpty()) break;

			std::sort(rows.begin(), rows.end(),
				[](const NoteRow& a, const NoteRow& b) { return a.pos < b.pos; });

			// ---- モーラへの長さ反映ユーティリティ（子音有無で分岐）----
			auto applyLengthToMora = [&](JSON& ap, size_t moraIdx, double sec)
				{
					JSON mora = ap[U"moras"][moraIdx]; // 値で取得（コピー）

					const bool hasCons =
						mora.contains(U"consonant") &&
						!mora[U"consonant"].isNull() &&
						mora[U"consonant"].isString();

					const double c0 = (hasCons && mora.contains(U"consonant_length") && mora[U"consonant_length"].isNumber())
						? mora[U"consonant_length"].get<double>() : 0.0;
					const double v0 = (mora.contains(U"vowel_length") && mora[U"vowel_length"].isNumber())
						? mora[U"vowel_length"].get<double>() : 0.0;
					const double sum0 = (c0 + v0);

					if (hasCons)
					{
						if (sum0 > 0.0)
						{
							const double c = round6(sec * (c0 / sum0));
							const double v = round6(sec - c); // c+v=sec を厳密維持
							mora[U"consonant_length"] = c;
							mora[U"vowel_length"] = v;
						}
						else
						{
							// 既定値が 0:0 の場合は全量を母音へ
							mora[U"consonant_length"] = 0.0;
							mora[U"vowel_length"] = round6(sec);
						}
					}
					else
					{
						// 子音が無いモーラ：consonant_length は null（数値 0.0 ではなく）
						mora[U"consonant_length"] = JSON();  // null
						mora[U"vowel_length"] = round6(sec);
					}

					ap[U"moras"][moraIdx] = mora; // 書き戻し
				};

			// ---- ノート秒を mora に、ノート間ギャップ秒を pause_mora に配分 ----
			Array<double> pauseSec(apCount, 0.0);  // AP ごとの休符秒合計（正確な秒数をそのまま）
			size_t moraPtr = 0;                    // 次に埋める mora（moraMap のインデックス）
			Optional<size_t> lastApOfMora;         // 直前に埋めた mora が属する AP

			// 先頭ノートの終端（先頭休符は直前モーラが無いので加算しない）
			int64 prevEnd = rows.front().pos + rows.front().dur;

			// 先頭ノート：歌詞があれば最初の mora へ
			if (rows.front().hasLyric && moraPtr < moraMap.size())
			{
				const double sec = ticksToSec(rows.front().dur);
				const size_t apIdx = moraMap[moraPtr].first;
				const size_t moraIdx = moraMap[moraPtr].second;

				JSON ap = query[U"accent_phrases"][apIdx];   // コピー
				applyLengthToMora(ap, moraIdx, sec);
				query[U"accent_phrases"][apIdx] = ap;        // 書き戻し

				lastApOfMora = apIdx;
				++moraPtr;
			}

			// 2 ノート目以降
			for (size_t i = 1; i < rows.size(); ++i)
			{
				const auto& cur = rows[i];

				// ノート間ギャップ（休符）→ 直前モーラの属する AP の pause_mora に加算
				if (cur.pos > prevEnd && lastApOfMora.has_value())
				{
					const int64 gap = cur.pos - prevEnd;
					pauseSec[*lastApOfMora] += ticksToSec(gap);   // 圧縮せず正確な秒数
				}

				// 現在ノートの歌詞 → 次の mora に割当
				if (cur.hasLyric && moraPtr < moraMap.size())
				{
					const double sec = ticksToSec(cur.dur);
					const size_t apIdx = moraMap[moraPtr].first;
					const size_t moraIdx = moraMap[moraPtr].second;

					JSON ap = query[U"accent_phrases"][apIdx];   // コピー
					applyLengthToMora(ap, moraIdx, sec);
					query[U"accent_phrases"][apIdx] = ap;        // 書き戻し

					lastApOfMora = apIdx;
					++moraPtr;
				}

				// 終端更新
				prevEnd = Max(prevEnd, cur.pos + cur.dur);
			}

			// ---- pause_mora を書き戻し（0 のときは null のまま）----
			for (size_t i = 0; i < apCount; ++i)
			{
				JSON ap = query[U"accent_phrases"][i];   // コピー
				const double ps = round6(pauseSec[i]);

				if (ps > 0.0)
				{
					JSON pm;
					pm[U"consonant"] = JSON();     // null
					pm[U"consonant_length"] = JSON();     // null
					pm[U"text"] = U"、";
					pm[U"vowel"] = U"pau";
					pm[U"vowel_length"] = ps;         // 正確な秒数
					pm[U"pitch"] = 0.0;
					ap[U"pause_mora"] = pm;
				}
				else
				{
					ap[U"pause_mora"] = JSON();           // null
				}
				query[U"accent_phrases"][i] = ap;        // 書き戻し
			}

			// （任意）検証とログ
			if (moraPtr < moraMap.size())
			{
				const String base = FileSystem::BaseName(vvprojPath);
				Console << U"[Talk⑤] mora割当 未完了: {} / {} (file={}, trackIndex={})"_fmt(
					moraPtr, moraMap.size(), base, talkTrackIndex);
			}
		} while (false);


		// ⑥ 取得したクエリを保存
		return query.save(outJsonPath);
	}
}
