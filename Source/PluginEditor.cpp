/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <thread>

namespace
{
    juce::String jp(const char *utf8)
    {
        return juce::String::fromUTF8(utf8);
    }

    juce::String pickUiFontName()
    {
        const auto names = juce::Font::findAllTypefaceNames();

        const juce::StringArray preferredPatterns{
            "Rounded M+ 1c",
            "Rounded M+ 1m",
            "Rounded M+",
            "Rounded Mplus",
            "M PLUS Rounded 1c",
            "M PLUS Rounded 1m",
            "M PLUS Rounded",
            "M+ 1c"};

        for (const auto &pattern : preferredPatterns)
            for (const auto &name : names)
                if (name.containsIgnoreCase(pattern))
                    return name;

        return {};
    }

    juce::String makeProgressBar(int percent, int width)
    {
        const auto clamped = juce::jlimit(0, 100, percent);
        const auto filled = (clamped * width) / 100;

        juce::String bar;
        bar.preallocateBytes(width + 2);
        bar << "[";
        for (int i = 0; i < width; ++i)
            bar << (i < filled ? u8"■" : u8"□");
        bar << "]";

        return bar;
    }

    juce::String sanitizeFileNamePart(juce::String text)
    {
        text = text.trim();
        const juce::String forbidden = "\\/:*?\"<>|";

        for (int i = 0; i < forbidden.length(); ++i)
            text = text.replaceCharacter(forbidden[i], '_');

        for (juce::juce_wchar c = 0; c < 32; ++c)
            text = text.replaceCharacter(c, '_');

        return text.trim();
    }
}

//==============================================================================
VVChorusPlayerAudioProcessorEditor::VVChorusPlayerAudioProcessorEditor(VVChorusPlayerAudioProcessor &p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    if (const auto uiFont = pickUiFontName(); uiFont.isNotEmpty())
        juce::LookAndFeel::getDefaultLookAndFeel().setDefaultSansSerifTypefaceName(uiFont);

    selectVvprojButton.setButtonText(jp(u8"vvprojを選択"));
    generateVoicevoxButton.setButtonText(jp(u8"生成スタート"));

    fileStepLabel.setText(jp(u8"1. VOICEVOXプロジェクトファイルを選択"), juce::dontSendNotification);
    singerStepLabel.setText(jp(u8"2. 歌唱キャラクターを選ぶ"), juce::dontSendNotification);
    generateStepLabel.setText(jp(u8"3. 歌声生成を開始"), juce::dontSendNotification);
    selectedVvprojLabel.setText(jp(u8"未選択"), juce::dontSendNotification);
    selectedVvprojLabel.setJustificationType(juce::Justification::centredLeft);

    addAndMakeVisible(singerStepLabel);
    addAndMakeVisible(fileStepLabel);
    addAndMakeVisible(generateStepLabel);

    addAndMakeVisible(singerComboBox);
    singerComboBox.setTextWhenNothingSelected(jp(u8"キャラクターを選択"));
    singerComboBox.onChange = [this]
    {
        updateActionState();
    };

    addAndMakeVisible(showAllStylesToggle);
    showAllStylesToggle.setButtonText(jp(u8"表示: 先頭スタイルのみ"));
    showAllStylesToggle.setToggleState(isShowingAllStyles, juce::dontSendNotification);
    showAllStylesToggle.onClick = [this]
    {
        if (isLoadingSingers || isGeneratingVoicevox)
            return;

        isShowingAllStyles = showAllStylesToggle.getToggleState();
        showAllStylesToggle.setButtonText(isShowingAllStyles
                                              ? jp(u8"表示: 全スタイル")
                                              : jp(u8"表示: 先頭スタイルのみ"));
        startFetchSingers();
    };

    addAndMakeVisible(selectVvprojButton);
    selectVvprojButton.onClick = [this]
    {
        if (isGeneratingVoicevox || isLoadingSingers)
            return;

        vvprojChooser = std::make_unique<juce::FileChooser>(jp(u8"vvprojを選択"), juce::File{}, "*.vvproj");
        vvprojChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                   [this](const juce::FileChooser &chooser)
                                   {
                                       selectedVvprojFile = chooser.getResult();
                                       vvprojChooser.reset();

                                       if (selectedVvprojFile.existsAsFile())
                                           selectedVvprojLabel.setText(selectedVvprojFile.getFileName(), juce::dontSendNotification);
                                       else
                                           selectedVvprojLabel.setText(jp(u8"未選択"), juce::dontSendNotification);

                                       updateActionState();
                                   });
    };

    addAndMakeVisible(selectedVvprojLabel);

    addAndMakeVisible(voicevoxWarningLabel);
    voicevoxWarningLabel.setJustificationType(juce::Justification::centred);
    voicevoxWarningLabel.setText(jp(u8"VOICEVOXを起動してください（起動後に自動で再接続します）"), juce::dontSendNotification);
    voicevoxWarningLabel.setVisible(false);

    addAndMakeVisible(statusLabel);
    statusLabel.setText(jp(u8"準備中..."), juce::dontSendNotification);
    statusLabel.setJustificationType(juce::Justification::topLeft);

    addAndMakeVisible(previewLabel);
    previewLabel.setText(jp(u8"プレビュー再生"), juce::dontSendNotification);
    previewLabel.setJustificationType(juce::Justification::centredLeft);

    addAndMakeVisible(playButton);
    playButton.setButtonText(jp(u8"▶ 再生"));
    playButton.onClick = [this]
    {
        audioProcessor.setPreviewPlaying(true);
        updateActionState();
    };

    addAndMakeVisible(stopButton);
    stopButton.setButtonText(jp(u8"■ 停止"));
    stopButton.onClick = [this]
    {
        audioProcessor.setPreviewPlaying(false);
        updateActionState();
    };

    addAndMakeVisible(exportAudioButton);
    exportAudioButton.setButtonText(jp(u8"生成した歌声をWAVで書き出し"));
    exportAudioButton.onClick = [this]
    {
        if (!audioProcessor.hasLoadedFile())
        {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                   jp(u8"書き出し"),
                                                   jp(u8"先に歌声を生成してください"));
            return;
        }

        auto baseName = selectedVvprojFile.existsAsFile()
                            ? selectedVvprojFile.getFileNameWithoutExtension()
                            : juce::File(audioProcessor.getLoadedFileName()).getFileNameWithoutExtension();

        if (baseName.isEmpty())
            baseName = "voice";

        juce::String singerName = jp(u8"unknown");
        juce::String styleName = jp(u8"style");
        const auto selectedIndex = singerComboBox.getSelectedItemIndex();
        if (juce::isPositiveAndBelow(selectedIndex, availableSingers.size()))
        {
            const auto &selectedSinger = availableSingers.getReference(selectedIndex);
            if (selectedSinger.singerName.isNotEmpty())
                singerName = selectedSinger.singerName;
            if (selectedSinger.styleName.isNotEmpty())
                styleName = selectedSinger.styleName;
        }

        baseName = sanitizeFileNamePart(baseName);
        singerName = sanitizeFileNamePart(singerName);
        styleName = sanitizeFileNamePart(styleName);

        auto defaultName = baseName + "-" + singerName + jp(u8"（") + styleName + jp(u8"）") + ".wav";

        const auto initialFile = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile(defaultName);

        exportWavChooser = std::make_unique<juce::FileChooser>(jp(u8"書き出し先を選択"),
                                                               initialFile,
                                                               "*.wav");
        exportWavChooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                                      [this](const juce::FileChooser &chooser)
                                      {
                                          auto target = chooser.getResult();
                                          exportWavChooser.reset();

                                          if (target == juce::File())
                                              return;

                                          if (!target.hasFileExtension("wav"))
                                              target = target.withFileExtension("wav");

                                          const auto result = audioProcessor.exportLoadedAudioToWav(target);
                                          if (result.failed())
                                          {
                                              juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                                                     jp(u8"書き出し失敗"),
                                                                                     result.getErrorMessage());
                                              return;
                                          }

                                          juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,
                                                                                 jp(u8"書き出し完了"),
                                                                                 jp(u8"保存しました:\n") + target.getFullPathName());
                                      });
    };

    addAndMakeVisible(previewPositionSlider);
    previewPositionSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    previewPositionSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    previewPositionSlider.onDragStart = [this]
    {
        isScrubbingPreview = true;
    };
    previewPositionSlider.onValueChange = [this]
    {
        if (isScrubbingPreview)
            audioProcessor.setPreviewPositionSeconds(previewPositionSlider.getValue());
    };
    previewPositionSlider.onDragEnd = [this]
    {
        isScrubbingPreview = false;
        audioProcessor.setPreviewPositionSeconds(previewPositionSlider.getValue());
    };

    addAndMakeVisible(previewTimeLabel);
    previewTimeLabel.setJustificationType(juce::Justification::centredRight);
    previewTimeLabel.setText("00:00 / 00:00", juce::dontSendNotification);

    addAndMakeVisible(generateVoicevoxButton);
    generateVoicevoxButton.onClick = [this]
    {
        if (isGeneratingVoicevox || isLoadingSingers)
            return;

        const auto selectedIndex = singerComboBox.getSelectedItemIndex();
        if (!juce::isPositiveAndBelow(selectedIndex, availableSingers.size()))
        {
            statusLabel.setText(jp(u8"先にキャラを選択してください"), juce::dontSendNotification);
            return;
        }

        if (!selectedVvprojFile.existsAsFile())
        {
            statusLabel.setText(jp(u8"先にvvprojを選択してください"), juce::dontSendNotification);
            return;
        }

        startVoicevoxGeneration(availableSingers.getReference(selectedIndex));
    };

    applyTheme();
    setResizable(true, true);
    setResizeLimits(560, 640, 1100, 980);
    setSize(780, 760);

    startTimerHz(30);
    startFetchSingers();
}

VVChorusPlayerAudioProcessorEditor::~VVChorusPlayerAudioProcessorEditor()
{
}

void VVChorusPlayerAudioProcessorEditor::startFetchSingers()
{
    if (isLoadingSingers)
        return;

    isLoadingSingers = true;
    updateActionState();

    const auto safeThis = juce::Component::SafePointer<VVChorusPlayerAudioProcessorEditor>(this);
    const auto baseUrl = voicevoxBaseUrl;
    const auto includeAllStyles = isShowingAllStyles;

    std::thread([safeThis, baseUrl, includeAllStyles]
                {
        juce::Array<voicevox::SingerStyle> singers;
        const auto result = voicevox::fetchSingers (baseUrl, singers, includeAllStyles);

        juce::MessageManager::callAsync ([safeThis, result, singers]
        {
            if (safeThis == nullptr)
                return;

            safeThis->isLoadingSingers = false;
            safeThis->singerComboBox.clear();
            safeThis->availableSingers = singers;

            if (result.failed())
            {
                juce::Logger::writeToLog (jp(u8"キャラ取得失敗: ") + result.getErrorMessage());
                safeThis->isVoicevoxUnavailable = true;
                safeThis->voicevoxWarningLabel.setVisible (true);
                safeThis->nextVoicevoxRetryTick = juce::Time::getMillisecondCounter() + 2500;
                safeThis->updateActionState();
                safeThis->resized();
                safeThis->repaint();
                return;
            }

            safeThis->isVoicevoxUnavailable = false;
            safeThis->voicevoxWarningLabel.setVisible (false);
            safeThis->rebuildSingerComboItems();
            safeThis->updateActionState();
            safeThis->resized();
            safeThis->repaint();
        }); })
        .detach();
}

void VVChorusPlayerAudioProcessorEditor::rebuildSingerComboItems()
{
    const auto selectedText = singerComboBox.getText();

    singerComboBox.clear(juce::dontSendNotification);

    int itemId = 1;
    for (const auto &singer : availableSingers)
    {
        const auto displayText = isShowingAllStyles
                                     ? (singer.singerName + " (" + singer.styleName + ")")
                                     : singer.singerName;
        singerComboBox.addItem(displayText, itemId++);
    }

    if (availableSingers.isEmpty())
        return;

    int restoredIndex = -1;
    for (int i = 0; i < singerComboBox.getNumItems(); ++i)
    {
        if (singerComboBox.getItemText(i) == selectedText)
        {
            restoredIndex = i;
            break;
        }
    }

    if (restoredIndex >= 0)
        singerComboBox.setSelectedItemIndex(restoredIndex, juce::dontSendNotification);
    else
        singerComboBox.setSelectedItemIndex(0, juce::dontSendNotification);
}

void VVChorusPlayerAudioProcessorEditor::startVoicevoxGeneration(const voicevox::SingerStyle &selectedSinger)
{
    const auto keyShift = voicevox::getKeyAdjustment(selectedSinger.singerName, selectedSinger.styleName);

    isGeneratingVoicevox = true;
    displayedProgress = 0.0;
    lastRawProgress = 0.0f;
    lastTimerSeconds = juce::Time::getMillisecondCounterHiRes() * 0.001;
    lastRawProgressSeconds = lastTimerSeconds;
    secondsPerPercentEstimate = 0.12;

    updateActionState();
    statusLabel.setText(jp(u8"生成開始...\n") + selectedSinger.singerName + " (" + selectedSinger.styleName + ")" + " keyShift=" + juce::String(keyShift),
                        juce::dontSendNotification);

    auto *processor = &audioProcessor;
    const auto safeThis = juce::Component::SafePointer<VVChorusPlayerAudioProcessorEditor>(this);

    std::thread([safeThis, processor, selectedFile = selectedVvprojFile, selectedSinger, baseUrl = voicevoxBaseUrl]
                {
        const auto result = processor->generateFromVvproj (selectedFile,
                                                            0,
                                                            selectedSinger.speakerId,
                                                            selectedSinger.singerName,
                                                            selectedSinger.styleName,
                                                            baseUrl);

        juce::MessageManager::callAsync ([safeThis, result]
        {
            if (safeThis == nullptr)
                return;

            safeThis->isGeneratingVoicevox = false;
            safeThis->updateActionState();

            if (result.wasOk())
                safeThis->statusLabel.setText (jp(u8"生成完了: 再生準備ができました"), juce::dontSendNotification);
            else
            {
                const auto message = jp(u8"VOICEVOX生成失敗: ") + result.getErrorMessage();
                juce::Logger::writeToLog (message);
                safeThis->statusLabel.setText (jp(u8"生成失敗（詳細はポップアップ/ログ）"), juce::dontSendNotification);
                juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                                                        jp(u8"VOICEVOX エラー"),
                                                        message);
            }
        }); })
        .detach();
}

void VVChorusPlayerAudioProcessorEditor::updateActionState()
{
    const auto hasSingerSelection = juce::isPositiveAndBelow(singerComboBox.getSelectedItemIndex(), availableSingers.size());
    const auto hasVvproj = selectedVvprojFile.existsAsFile();
    const auto isUiLocked = isVoicevoxUnavailable;
    const auto canGenerate = !isUiLocked && !isLoadingSingers && !isGeneratingVoicevox && hasSingerSelection && hasVvproj;

    singerComboBox.setEnabled(!isUiLocked && !isLoadingSingers && !isGeneratingVoicevox && !availableSingers.isEmpty());
    showAllStylesToggle.setEnabled(!isUiLocked && !isLoadingSingers && !isGeneratingVoicevox);
    selectVvprojButton.setEnabled(!isUiLocked && !isLoadingSingers && !isGeneratingVoicevox);
    generateVoicevoxButton.setEnabled(canGenerate);
    generateVoicevoxButton.setColour(juce::TextButton::buttonColourId,
                                     canGenerate ? juce::Colour::fromRGB(108, 176, 122) : juce::Colour::fromRGB(188, 201, 192));
    generateVoicevoxButton.setColour(juce::TextButton::textColourOffId,
                                     canGenerate ? juce::Colours::white : juce::Colour::fromRGB(100, 112, 104));
    generateVoicevoxButton.setAlpha(canGenerate ? 1.0f : 0.78f);

    const auto hasAudio = audioProcessor.hasLoadedFile();
    const auto previewPlaying = audioProcessor.isPreviewPlaying();
    playButton.setEnabled(!isUiLocked && hasAudio && !previewPlaying);
    stopButton.setEnabled(!isUiLocked && hasAudio && previewPlaying);
    previewPositionSlider.setEnabled(!isUiLocked && hasAudio);
    exportAudioButton.setEnabled(!isUiLocked && hasAudio && !isGeneratingVoicevox);
}

void VVChorusPlayerAudioProcessorEditor::applyTheme()
{
    const auto accent = juce::Colour::fromRGB(165, 212, 173);
    const auto panel = juce::Colour::fromRGB(233, 245, 236);
    const auto text = juce::Colour::fromRGB(35, 66, 45);

    selectVvprojButton.setColour(juce::TextButton::buttonColourId, accent);
    selectVvprojButton.setColour(juce::TextButton::textColourOffId, text);

    generateVoicevoxButton.setColour(juce::TextButton::buttonColourId, accent.darker(0.15f));
    generateVoicevoxButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);

    singerComboBox.setColour(juce::ComboBox::backgroundColourId, juce::Colours::white);
    singerComboBox.setColour(juce::ComboBox::textColourId, text);
    singerComboBox.setColour(juce::ComboBox::outlineColourId, accent.darker(0.2f));

    showAllStylesToggle.setColour(juce::ToggleButton::textColourId, text);
    showAllStylesToggle.setColour(juce::ToggleButton::tickColourId, accent.darker(0.25f));
    showAllStylesToggle.setColour(juce::ToggleButton::tickDisabledColourId, accent.darker(0.45f));

    playButton.setColour(juce::TextButton::buttonColourId, accent.darker(0.05f));
    playButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    stopButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(180, 207, 186));
    stopButton.setColour(juce::TextButton::textColourOffId, text);

    exportAudioButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(129, 186, 140));
    exportAudioButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);

    previewPositionSlider.setColour(juce::Slider::trackColourId, accent.darker(0.15f));
    previewPositionSlider.setColour(juce::Slider::thumbColourId, accent.darker(0.25f));
    previewPositionSlider.setColour(juce::Slider::backgroundColourId, juce::Colours::white);

    selectedVvprojLabel.setColour(juce::Label::backgroundColourId, juce::Colours::white);
    selectedVvprojLabel.setColour(juce::Label::textColourId, text);
    selectedVvprojLabel.setColour(juce::Label::outlineColourId, accent.darker(0.2f));

    statusLabel.setColour(juce::Label::backgroundColourId, panel);
    statusLabel.setColour(juce::Label::textColourId, text);
    voicevoxWarningLabel.setColour(juce::Label::backgroundColourId, juce::Colour::fromRGB(255, 239, 204));
    voicevoxWarningLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(135, 72, 16));
    voicevoxWarningLabel.setColour(juce::Label::outlineColourId, juce::Colour::fromRGB(210, 160, 94));
    previewLabel.setColour(juce::Label::textColourId, text);
    previewTimeLabel.setColour(juce::Label::textColourId, text);

    singerStepLabel.setColour(juce::Label::textColourId, text);
    fileStepLabel.setColour(juce::Label::textColourId, text);
    generateStepLabel.setColour(juce::Label::textColourId, text);
}

void VVChorusPlayerAudioProcessorEditor::timerCallback()
{
    if (isVoicevoxUnavailable && !isLoadingSingers && juce::Time::getMillisecondCounter() >= nextVoicevoxRetryTick)
    {
        nextVoicevoxRetryTick = juce::Time::getMillisecondCounter() + 2500;
        startFetchSingers();
    }

    refreshPreviewUi();
    updateActionState();

    if (!isGeneratingVoicevox)
        return;

    const auto nowSeconds = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const auto deltaSeconds = juce::jmax(0.0, nowSeconds - lastTimerSeconds);
    lastTimerSeconds = nowSeconds;

    const auto rawProgress = juce::jlimit(0.0f, 1.0f, audioProcessor.getVoicevoxProgress());
    if (rawProgress > lastRawProgress)
    {
        const auto progressDeltaPercent = (rawProgress - lastRawProgress) * 100.0;
        const auto timeDelta = juce::jmax(0.001, nowSeconds - lastRawProgressSeconds);
        const auto observedSecondsPerPercent = timeDelta / juce::jmax(0.1, progressDeltaPercent);
        secondsPerPercentEstimate = juce::jlimit(0.02, 2.0, secondsPerPercentEstimate * 0.7 + observedSecondsPerPercent * 0.3);

        lastRawProgress = rawProgress;
        lastRawProgressSeconds = nowSeconds;
    }
    else if (rawProgress < lastRawProgress)
    {
        lastRawProgress = rawProgress;
        lastRawProgressSeconds = nowSeconds;
        displayedProgress = rawProgress;
    }

    const auto progressStep = deltaSeconds / juce::jmax(0.01, secondsPerPercentEstimate) / 100.0;
    displayedProgress = juce::jmin(static_cast<double>(rawProgress), displayedProgress + progressStep);

    const auto percent = juce::jlimit(0, 100, static_cast<int>(std::floor(displayedProgress * 100.0 + 1.0e-6)));
    const auto status = audioProcessor.getVoicevoxStatus();
    const auto bar = makeProgressBar(percent, 24);
    const auto percentText = juce::String(percent).paddedLeft(' ', 3);

    statusLabel.setText(jp(u8"生成中... ") + percentText + "%\n" + bar + "\n" + status,
                        juce::dontSendNotification);
}

void VVChorusPlayerAudioProcessorEditor::refreshPreviewUi()
{
    const auto duration = audioProcessor.getLoadedDurationSeconds();
    const auto isPreviewPlayingNow = audioProcessor.isPreviewPlaying();
    const auto isHostPlayingNow = audioProcessor.isHostPlayingNow();

    double position = audioProcessor.getPreviewPositionSeconds();
    displayFollowingHost = false;
    if (isHostPlayingNow)
    {
        position = audioProcessor.getHostPositionSeconds();
        displayFollowingHost = true;
    }

    displayDurationSeconds = duration;
    displayPositionSeconds = juce::jlimit(0.0, duration, position);

    previewPositionSlider.setRange(0.0, juce::jmax(0.001, duration), 0.0);
    if (!isScrubbingPreview)
        previewPositionSlider.setValue(displayPositionSeconds, juce::dontSendNotification);

    previewTimeLabel.setText((displayFollowingHost ? jp(u8"DAW同期  ") : jp(u8"手動  ")) + formatSeconds(displayPositionSeconds) + " / " + formatSeconds(duration),
                             juce::dontSendNotification);

    const auto sourceName = audioProcessor.getLoadedFileName();
    if (sourceName != waveformSourceName)
    {
        waveformSourceName = sourceName;
        waveformPeaks.clearQuick();
        if (waveformSourceName.isNotEmpty())
            audioProcessor.getWaveformPeaks(300, waveformPeaks);
    }

    repaint(waveformArea);
}

juce::String VVChorusPlayerAudioProcessorEditor::formatSeconds(double seconds) const
{
    const auto total = juce::jmax(0, static_cast<int>(std::floor(seconds + 0.5)));
    const auto minutes = total / 60;
    const auto sec = total % 60;
    return juce::String(minutes).paddedLeft('0', 2) + ":" + juce::String(sec).paddedLeft('0', 2);
}

//==============================================================================
void VVChorusPlayerAudioProcessorEditor::paint(juce::Graphics &g)
{
    const auto background = juce::Colour::fromRGB(165, 212, 173).darker(0.25f);
    g.fillAll(background);

    g.setColour(juce::Colours::white);
    const auto titleHeight = juce::jlimit(24, 40, getHeight() / 12);
    const auto titleFontSize = juce::jlimit(16.0f, 32.0f, static_cast<float>(titleHeight) * 0.58f);
    g.setFont(juce::Font(juce::FontOptions(titleFontSize)).boldened());
    g.drawFittedText("VVProject Synth", getLocalBounds().removeFromTop(titleHeight), juce::Justification::centred, 1);

    if (!waveformArea.isEmpty())
    {
        g.setColour(juce::Colours::white.withAlpha(0.93f));
        g.fillRoundedRectangle(waveformArea.toFloat(), 6.0f);

        g.setColour(juce::Colour::fromRGB(106, 154, 116));
        g.drawRoundedRectangle(waveformArea.toFloat(), 6.0f, 1.0f);

        g.setColour(juce::Colour::fromRGB(140, 170, 148));
        g.drawHorizontalLine(waveformArea.getCentreY(), static_cast<float>(waveformArea.getX() + 2), static_cast<float>(waveformArea.getRight() - 2));

        if (waveformPeaks.isEmpty())
        {
            g.setColour(juce::Colour::fromRGB(79, 113, 88));
            g.drawFittedText(jp(u8"生成後にここへ波形を表示します"), waveformArea, juce::Justification::centred, 1);
        }
        else
        {
            const auto centerY = waveformArea.getCentreY();
            const auto halfHeight = waveformArea.getHeight() * 0.45f;
            const auto width = waveformArea.getWidth();
            const auto points = waveformPeaks.size();

            g.setColour(juce::Colour::fromRGB(67, 125, 81));
            for (int x = 0; x < width; ++x)
            {
                const auto index = juce::jlimit(0, points - 1, x * points / juce::jmax(1, width));
                const auto amp = waveformPeaks[index];
                const auto lineHalf = static_cast<int>(std::round(amp * halfHeight));
                g.drawVerticalLine(waveformArea.getX() + x, static_cast<float>(centerY - lineHalf), static_cast<float>(centerY + lineHalf));
            }

            if (displayDurationSeconds > 0.0)
            {
                const auto ratio = juce::jlimit(0.0, 1.0, displayPositionSeconds / displayDurationSeconds);
                const auto playheadX = waveformArea.getX() + static_cast<int>(std::round(ratio * (waveformArea.getWidth() - 1)));
                g.setColour(displayFollowingHost ? juce::Colours::orange : juce::Colour::fromRGB(34, 87, 214));
                g.drawVerticalLine(playheadX, static_cast<float>(waveformArea.getY() + 2), static_cast<float>(waveformArea.getBottom() - 2));
            }
        }
    }
}

void VVChorusPlayerAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(juce::jlimit(14, 34, getWidth() / 24));
    const auto titleHeight = juce::jlimit(24, 40, getHeight() / 12);
    area.removeFromTop(titleHeight + juce::jlimit(1, 6, getHeight() / 90));

    const auto warningHeight = juce::jlimit(24, 40, getHeight() / 16);
    if (isVoicevoxUnavailable)
    {
        voicevoxWarningLabel.setBounds(area.removeFromTop(warningHeight));
        area.removeFromTop(juce::jlimit(4, 10, getHeight() / 80));
    }
    else
    {
        voicevoxWarningLabel.setBounds({});
    }

    const auto controlHeight = juce::jlimit(24, 46, getHeight() / 13);
    const auto spacing = juce::jlimit(6, 18, getHeight() / 50);
    const auto labelWidth = juce::jlimit(120, 180, getWidth() / 4);
    const auto buttonWidth = juce::jlimit(150, 240, getWidth() / 3);

    {
        auto row = area.removeFromTop(controlHeight);
        fileStepLabel.setBounds(row.removeFromLeft(labelWidth));
        row.removeFromLeft(spacing);
        selectVvprojButton.setBounds(row.removeFromRight(buttonWidth));
    }
    area.removeFromTop(spacing);

    selectedVvprojLabel.setBounds(area.removeFromTop(controlHeight));
    area.removeFromTop(spacing);

    {
        auto row = area.removeFromTop(controlHeight);
        singerStepLabel.setBounds(row.removeFromLeft(labelWidth));
        row.removeFromLeft(spacing);
        showAllStylesToggle.setBounds(row.removeFromRight(buttonWidth));
    }
    area.removeFromTop(spacing);

    singerComboBox.setBounds(area.removeFromTop(controlHeight));
    area.removeFromTop(spacing);

    {
        auto row = area.removeFromTop(controlHeight);
        generateStepLabel.setBounds(row.removeFromLeft(labelWidth));
        row.removeFromLeft(spacing);
        generateVoicevoxButton.setBounds(row.removeFromRight(buttonWidth));
    }
    area.removeFromTop(spacing);

    const auto remainingForBottom = area.getHeight();
    const auto previewHeaderHeight = juce::jmax(26, controlHeight - 2);
    const auto sliderHeight = juce::jmax(18, controlHeight - 10);
    const auto exportButtonHeight = juce::jmax(26, controlHeight - 4);
    const auto minWaveHeight = 72;
    const auto fixedBottomCost = previewHeaderHeight + sliderHeight + exportButtonHeight + spacing * 3;
    const auto baseStatusHeight = juce::jmax(56, remainingForBottom - fixedBottomCost - minWaveHeight);
    const auto statusHeight = juce::jmax(40, (baseStatusHeight * 5) / 7);

    statusLabel.setBounds(area.removeFromTop(statusHeight));
    area.removeFromTop(spacing);

    {
        auto row = area.removeFromTop(previewHeaderHeight);
        previewLabel.setBounds(row.removeFromLeft(labelWidth));
        row.removeFromLeft(spacing);
        playButton.setBounds(row.removeFromLeft(juce::jmin(130, row.getWidth() / 4)));
        row.removeFromLeft(spacing / 2);
        stopButton.setBounds(row.removeFromLeft(juce::jmin(130, row.getWidth() / 3)));
        row.removeFromLeft(spacing);
        previewTimeLabel.setBounds(row);
    }
    area.removeFromTop(spacing / 2);

    previewPositionSlider.setBounds(area.removeFromTop(sliderHeight));
    area.removeFromTop(spacing / 2);

    const auto waveformHeight = juce::jmax(minWaveHeight, area.getHeight() - exportButtonHeight - spacing / 2);
    waveformArea = area.removeFromTop(waveformHeight);
    area.removeFromTop(spacing / 2);

    exportAudioButton.setBounds(area.removeFromTop(exportButtonHeight).withSizeKeepingCentre(juce::jmin(360, area.getWidth()), exportButtonHeight));
}
