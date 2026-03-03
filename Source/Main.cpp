#include "stdafx.h"

void Main()
{
	Window::SetTitle(U"SHINE VOX");
	Window::Resize(1920, 1080);
	Scene::SetResizeMode(ResizeMode::Keep);
	Window::SetStyle(WindowStyle::Sizable);

	const size_t kMaxCharacters = 5;              // 上限  
	size_t charCount = 0;                         // vvproj 読込で決定  

	// 背景画像
	const Texture background{ U"Texture/background1.jpg" };
	const Texture song_title{ U"Texture/song_title.png" };

	// テキスト表示
	const FilePath fontpath = (U"C:/Program Files/Steinberg/UR-C/font/mplus-1c-medium.ttf");
	const Font font{ FontMethod::MSDF, 48 , fontpath };

	// 時間を管理する変数
	bool waitingToPlay = false;
	double waitTimer = 0.0;

	// 伴奏音声
	Audio audio_inst;

	// キャラ用コンテナ（最大数で確保）
	Array<Texture> characterTex(kMaxCharacters,
		Texture{ U"Texture/Character/VOICEVOX/ずんだもん.png" });
	Array<Audio> songAudio(kMaxCharacters);
	Array<Audio> talkAudio(kMaxCharacters);
	Array<double> talkStartSecs(kMaxCharacters, 0.0);  // 各トラックの開始秒
	Array<bool>   talkPending(kMaxCharacters, false);  // 再生待ちフラグ
	double playElapsedSec = 0.0;                       // 再生開始からの経過秒

	// ─────────────────────────────
	//  スピーカー関連のデータ配列
	// ─────────────────────────────
	Array<String> SingerLabels;   // 表示用: ずんだもん（ノーマル）
	Array<String> SingerNames;    // キャラ名 : ずんだもん
	Array<String> StyleNames;     // スタイル名 : ノーマル
	Array<int32>  SingerIDs;      // VOICEVOX スタイル ID

	// 
	const URL baseURL = U"http://localhost:50021";

	for (const auto& spk : VOICEVOX::GetSingers(baseURL))
	{
		if (!spk.styles.isEmpty())
		{
			const auto& st = spk.styles[0]; // 先頭のスタイルだけ使用
			SingerLabels << U"{}（{}）"_fmt(spk.name, st.name);
			SingerNames << spk.name;
			StyleNames << st.name;
			SingerIDs << st.id;
		}
	}

	// ListBox 状態（キャラ数ぶん）
	Array<ListBoxState> SingerUI(kMaxCharacters, ListBoxState{ SingerLabels });
	Array<Optional<uint64>> prevSel(kMaxCharacters);

	// レイアウト
	constexpr double kCenterY = 370;
	constexpr Vec2 kListOff{ -150, 180 };

	// 共通パス
	Optional<FilePath> vvprojPath;
	const FilePath singQuery = U"Query/SingQuery.json";


	// フェードイン演出（簡易）
	for (double i = 0; i > 1.0; i += 0.1)
	{
		song_title.draw(Arg::center = Scene::Center(), ColorF{ 1.0, 0.5 });
	}

	// 
	auto analyzeEnergy = [](const Audio& a) -> double
		{
			if (!a.isPlaying()) return 0.0;
			FFTResult fft;
			FFT::Analyze(fft, a);
			if (fft.buffer.empty()) return 0.0;
			double energy = std::accumulate(fft.buffer.begin(), fft.buffer.end(), 0.0);
			energy /= fft.buffer.size();
			return energy;
		};
	const double kSingingThreshold = 0.00005;

	// ─────────────────────────────
	//  メインループ
	// ─────────────────────────────
	while (System::Update())
	{
		background.draw(Arg::center = Scene::Center());

		// vvproj 選択
		if (SimpleGUI::Button(U"🎵 入力ファイルを選択", Vec2{ 1500, 830 }))
		{
			vvprojPath = Dialog::OpenFile({ { U"VOICEVOX Project", { U"vvproj" } } });
			if (vvprojPath)
			{
				charCount = Min(VOICEVOX::GetVVProjTrackCount(*vvprojPath) /*/ 2*/, kMaxCharacters);
			}
		}
		if (charCount == 0) continue;           // 未選択  

		// 横位置を均等割り
		Array<Vec2> centers;
		const double step = static_cast<double>(Scene::Width()) / (charCount + 1);

		for (size_t i = 0; i < charCount; ++i)
			centers << Vec2{ step * (i + 1), kCenterY };


		// キャラ描画 + ListBox
		Array<size_t> singingIdx;           // 歌っている i を集める
		size_t singingNow = 0; // フレームごとに初期化
		for (size_t i = 0; i < charCount; ++i)
		{
			// 旧: songAudio[i] だけで判定 → 新: song/talk の最大値で OR 判定
			double scale = 0.8;
			double alpha = 0.5;

			const double eSong = analyzeEnergy(songAudio[i]);
			const double eTalk = analyzeEnergy(talkAudio[i]);
			const double eMax = Max(eSong, eTalk);          // ★UIは OR 条件（最大値）

			if (eMax > kSingingThreshold)
			{
				++singingNow;
				singingIdx << i;
				scale = 0.9;
				alpha = 1.0;
			}

			characterTex[i].scaled(scale).drawAt(centers[i], ColorF(1.0, alpha));

			SimpleGUI::ListBox(SingerUI[i], centers[i] + kListOff, 300, 220);

			// 選択変更時に立ち絵切替
			if (SingerUI[i].selectedItemIndex != prevSel[i])
			{
				prevSel[i] = SingerUI[i].selectedItemIndex;
				const String selLabel = SingerLabels[SingerUI[i].selectedItemIndex.value()];
				// フォルダ内のファイル一覧を取得
				const FilePath charFolder = U"Texture/Character/VOICEVOX/";
				const auto files = FileSystem::DirectoryContents(charFolder, Recursive::No);
				Optional<FilePath> matchedTex;
				for (const auto& f : files)
				{
					const String fileName = FileSystem::BaseName(f); // 拡張子なし
					if (selLabel.starts_with(fileName))
					{
						matchedTex = f;
						break;
					}
				}
				FilePath tex = matchedTex.value_or(charFolder + U"ずんだもん.png");
				characterTex[i] = Texture{ tex };
			}

			// ─── 音量の表示 ───
			double volume = songAudio[i].getVolume();
			font(U"Vol: {:.2f}"_fmt(volume))
				.drawAt(centers[i] + Vec2{ 0, 150 }, ColorF(1.0));

			// ─── パンの表示 ─── ★追加ここから
			double pan = songAudio[i].getPan();                        // -1.0 〜 1.0
			font(U"Pan: {:+.2f}"_fmt(pan))                          // + を付けて左右が分かるように
				.drawAt(centers[i] + Vec2{ 0, 110 }, ColorF(1.0)); // Vol の 40px 下に描画
		}

		// ─────────── 音声合成 ───────────
		if (SimpleGUI::Button(U"🎵 音声合成", Vec2{ 1500, 880 }, unspecified, vvprojPath.has_value()))
		{
			const String base = FileSystem::BaseName(*vvprojPath);

			for (size_t i = 0; i < charCount; ++i)
			{
				// ★ ListBox 選択→speakerID は最初に決める
				const uint64 selIdx = SingerUI[i].selectedItemIndex.value_or(0);
				const int32  spkID = SingerIDs[selIdx];
				const int32  talkSpkID = spkID - 3000;

				FilePath songwav = U"Voice/" + base + U"-" + SingerLabels[selIdx]
					+ U"_track" + Format(i + 1) + U".wav";
				FilePath talkwav = U"Voice/" + base + U"-" + SingerLabels[selIdx]
					+ U"_talk_track" + Format(i + 1) + U".wav";

				// vvproj → Song Score JSON
				FilePath score = U"tmp/tmp_" + base + U"_track" + Format(i + 1) + U".json"; // ← ついでに "tmp_" に統一
				if (!VOICEVOX::ConvertVVProjToScoreJSON(*vvprojPath, score, i))
					continue;

				// ★ vvproj(talk) → Talk Query JSON（失敗しても歌は続行）
				FilePath talkOut = U"tmp/tmp_talk_" + base + U"_track" + Format(i + 1) + U".json";
				double talkStartSec = 0.0;
				const bool talkOk = VOICEVOX::ConvertVVProjToTalkQueryJSON(baseURL, *vvprojPath, talkOut, talkSpkID, &talkStartSec, i + charCount);
				talkStartSecs[i] = Max(0.0, talkStartSec - 0.155); // 念のため 0 以上にクランプ 0.155は微調整

				// ── 歌（分割あり）は必ず実行
				int keyShift = VOICEVOX::GetKeyAdjustment(SingerNames[selIdx], StyleNames[selIdx]);
				if (VOICEVOX::SynthesizeFromJSONFileWrapperSplit(
					score, songwav, spkID, baseURL, 2500, keyShift))
				{
					songAudio[i] = Audio{ songwav };
					FileSystem::Remove(score); // 一時Score掃除
				}

				// ── Talk は生成できたときだけ合成し、成功時に掃除
				if (talkOk)
				{

					// 出力プレフィックス（分割は prefix_0.wav, 連結は prefix_joined.wav）
					const FilePath talkPrefix = U"Voice/" + base + U"-" + SingerLabels[selIdx]
						+ U"_talk_track" + Format(i + 1);

					const FilePath joinedTalk = talkPrefix + U"_joined.wav";
					if (VOICEVOX::SynthesizeFromVVProjWrapperSplitTalkJoin(
						baseURL, *vvprojPath, talkPrefix, joinedTalk, talkSpkID, i + charCount, 2500 * 1.5))
					{
						if (FileSystem::Exists(joinedTalk))
						{
							talkAudio[i] = Audio{ joinedTalk };
						}
					}

				}

				// デバッグ：キー補正値
				Console << U"{}（{}） → Shift:{}"_fmt(SingerNames[selIdx], StyleNames[selIdx],
							VOICEVOX::GetKeyAdjustment(SingerNames[selIdx], StyleNames[selIdx]));
			}

			audio_inst = Audio{ U"Inst/" + base + U".mp3" };
			Console << U"「" + base + U"」の再生準備が完了しました。\t";
		}

		// ─────────── 再生 ───────────
		const bool playable = std::any_of(songAudio.begin(), songAudio.begin() + charCount,
			[](const Audio& a) { return !a.isEmpty(); });

		if (SimpleGUI::Button(U"▶️再生", Vec2{ 1500, 930 }, unspecified, playable))
		{
			waitingToPlay = true;
			waitTimer = 0.0;
		}

		if (waitingToPlay)
		{
			waitTimer += Scene::DeltaTime();
			const String base = FileSystem::BaseName(*vvprojPath);

			double alpha = 0.0;
			if (waitTimer < 2.0) alpha = waitTimer / 2.0;
			else if (waitTimer < 4.0) alpha = 1.0;
			else if (waitTimer < 6.0) alpha = (6.0 - waitTimer) / 2.0;

			song_title.draw(Arg::center = Scene::Center(), ColorF{ 1.0, alpha });
			font(base).drawAt(45, Scene::Center().x, 483, ColorF{ 1.0, alpha });

			if (waitTimer >= 6.0)
			{
				waitingToPlay = false;

				/* ─ 同時に歌う人数を数える ─ */
				size_t singers = 0;
				for (size_t i = 0; i < charCount; ++i)
					if (!songAudio[i].isEmpty())
						++singers;

				/* 伴奏と各キャラを再生 */
				audio_inst.play();
				audio_inst.setVolume(0.4);            // 伴奏の固定ゲイン

				for (size_t i = 0; i < charCount; ++i)
				{
					if (!songAudio[i].isEmpty())
					{
						if (!songAudio[i].isEmpty()) songAudio[i].play();              // 歌はすぐ再生

						// トークは「待ち」にする（音声があるトラックのみ）
						talkPending[i] = (!talkAudio[i].isEmpty());
					}
				}
			}
		}

		// ─────────── 音量調整 ───────────
		// 同時に再生する人数に応じた音量係数を返す
		auto calcSingerVolume = [](size_t n)
			{
				switch (n)
				{
				case 0:  return 1.0;  // 0人
				case 1:  return 1.0;  // 1人
				case 2:  return 0.7;  // 2人（例）
				case 3:  return 0.65;  // 3人
				default: return 0.6;  // 4人以上は控えめに
				}
			};

		// 人数に応じた音量・パンに変更
		if (audio_inst.isPlaying())
		{
			playElapsedSec += Scene::DeltaTime();

			for (size_t i = 0; i < charCount; ++i)
			{
				if (talkPending[i] && (playElapsedSec >= talkStartSecs[i]))
				{
					talkAudio[i].play();   // ここで初めて再生
					talkPending[i] = false;
				}
			}

			/* ─ 音量調整 ─ */
			const double singerVol = calcSingerVolume(singingNow);
			for (size_t i = 0; i < charCount; ++i)
			{
				if (!songAudio[i].isEmpty()) songAudio[i].setVolume(singerVol);
				if (!talkAudio[i].isEmpty()) talkAudio[i].setVolume(singerVol);
			}

			/* ─ パン振り分け ─ */
			static const Array<Array<double>> panTable = {
				{}, { 0.0 },
				{ -0.4,  0.4 },
				{ -0.4,  0.0,  0.4 },
				{ -0.8, -0.4,  0.4,  0.8 },
				{ -0.8, -0.4,  0.0,  0.4,  0.8 }
			};

			const size_t n = singingIdx.size();
			const Array<double>& pans = panTable[Min(n, static_cast<size_t>(5))];

			/* ① 歌っている人 には定位置パン */
			for (size_t j = 0; j < n; ++j)
			{
				const size_t idx = singingIdx[j];
				if (!songAudio[idx].isEmpty()) songAudio[idx].setPan(pans[j]);
				if (!talkAudio[idx].isEmpty()) talkAudio[idx].setPan(pans[j]);              // 例: -0.3, +0.3
			}

			/* ② 歌っていない人 は中央に戻す */
			for (size_t i = 0; i < charCount; ++i) {
				if (!singingIdx.contains(i)) {
					if (!songAudio[i].isEmpty()) songAudio[i].setPan(0.0);
					if (!talkAudio[i].isEmpty()) talkAudio[i].setPan(0.0);
				}
			}

		}
	}
}
