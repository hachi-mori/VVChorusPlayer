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

    bool containsSpeakerId(const juce::Array<int> &speakerIds, int speakerId)
    {
        return speakerIds.contains(speakerId);
    }

    void setSpeakerSelected(juce::Array<int> &speakerIds, int speakerId, bool shouldSelect)
    {
        if (shouldSelect)
        {
            if (!speakerIds.contains(speakerId))
                speakerIds.add(speakerId);
            return;
        }

        speakerIds.removeFirstMatchingValue(speakerId);
    }
}

class VVChorusPlayerAudioProcessorEditor::StyleSwitchLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawToggleButton(juce::Graphics &g,
                          juce::ToggleButton &button,
                          bool shouldDrawButtonAsHighlighted,
                          bool shouldDrawButtonAsDown) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced(1.0f);
        const auto enabledAlpha = button.isEnabled() ? 1.0f : 0.55f;
        const auto isOn = button.getToggleState();

        const auto textColour = button.findColour(juce::ToggleButton::textColourId).withMultipliedAlpha(enabledAlpha);
        auto onColour = button.findColour(juce::ToggleButton::tickColourId);
        auto offColour = button.findColour(juce::ToggleButton::tickDisabledColourId);

        auto trackColour = (isOn ? onColour : offColour).withMultipliedAlpha(enabledAlpha);
        if (shouldDrawButtonAsHighlighted)
            trackColour = trackColour.brighter(0.08f);
        if (shouldDrawButtonAsDown)
            trackColour = trackColour.darker(0.08f);

        auto trackArea = bounds.reduced(4.0f, 6.0f);
        trackArea.setWidth(juce::jlimit(84.0f, 140.0f, trackArea.getWidth()));
        trackArea = trackArea.withX(bounds.getRight() - trackArea.getWidth());

        const auto labelRightPadding = 8.0f;
        const juce::Rectangle<float> labelArea(bounds.getX(),
                                               bounds.getY(),
                                               juce::jmax(0.0f, trackArea.getX() - bounds.getX() - labelRightPadding),
                                               bounds.getHeight());
        g.setColour(textColour);
        g.setFont(juce::Font(juce::FontOptions(14.0f)).boldened());
        g.drawFittedText(button.getButtonText(), labelArea.toNearestInt(), juce::Justification::centredRight, 1);

        const auto corner = trackArea.getHeight() * 0.5f;
        g.setColour(trackColour);
        g.fillRoundedRectangle(trackArea, corner);

        g.setColour(juce::Colours::white.withMultipliedAlpha(enabledAlpha * 0.95f));
        const auto stateText = isOn ? jp(u8"全スタイル") : jp(u8"先頭のみ");
        g.setFont(juce::Font(juce::FontOptions(12.0f)).boldened());
        g.drawFittedText(stateText, trackArea.toNearestInt(), juce::Justification::centred, 1);

        const auto knobMargin = 3.0f;
        const auto knobDiameter = juce::jmax(8.0f, trackArea.getHeight() - knobMargin * 2.0f);
        auto knobX = trackArea.getX() + knobMargin;
        if (isOn)
            knobX = trackArea.getRight() - knobMargin - knobDiameter;
        const juce::Rectangle<float> knob(knobX, trackArea.getY() + knobMargin, knobDiameter, knobDiameter);

        g.setColour(juce::Colours::white.withMultipliedAlpha(enabledAlpha));
        g.fillEllipse(knob);
        g.setColour(juce::Colours::black.withAlpha(0.15f * enabledAlpha));
        g.drawEllipse(knob, 1.0f);
    }
};

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

    addAndMakeVisible(selectionModeTabs);
    selectionModeTabs.addTab(jp(u8"手動選択"), juce::Colour::fromRGB(168, 212, 176), 0);
    selectionModeTabs.addTab(jp(u8"自動編成"), juce::Colour::fromRGB(168, 212, 176), 1);
    selectionModeTabs.setCurrentTabIndex(static_cast<int>(singerSelectionMode));

    addAndMakeVisible(singerSelectionLabel);
    singerSelectionLabel.setJustificationType(juce::Justification::centredLeft);
    singerSelectionLabel.setText(jp(u8"0キャラ選択中"), juce::dontSendNotification);

    addAndMakeVisible(singerListBox);
    singerListBox.setModel(this);
    singerListBox.setRowHeight(30);
    singerListBox.setMultipleSelectionEnabled(false);

    addAndMakeVisible(selectAllSingersButton);
    selectAllSingersButton.setButtonText(jp(u8"全選択"));
    selectAllSingersButton.onClick = [this]
    {
        if (isGeneratingVoicevox || isLoadingSingers)
            return;

        selectedSpeakerIds.clearQuick();
        for (const auto &singer : availableSingers)
            selectedSpeakerIds.addIfNotAlreadyThere(singer.speakerId);

        singerListBox.updateContent();
        singerListBox.repaint();
        updateSingerSelectionLabel();
        updateActionState();
    };

    addAndMakeVisible(clearSingerSelectionButton);
    clearSingerSelectionButton.setButtonText(jp(u8"選択解除"));
    clearSingerSelectionButton.onClick = [this]
    {
        if (isGeneratingVoicevox || isLoadingSingers)
            return;

        selectedSpeakerIds.clearQuick();
        singerListBox.updateContent();
        singerListBox.repaint();
        updateSingerSelectionLabel();
        updateActionState();
    };

    addAndMakeVisible(autoSelectionMethodLabel);
    autoSelectionMethodLabel.setText(jp(u8"選択方式"), juce::dontSendNotification);
    autoSelectionMethodLabel.setJustificationType(juce::Justification::centredLeft);

    addAndMakeVisible(autoSelectionMethodCombo);
    autoSelectionMethodCombo.addItem(jp(u8"ランダム"), 1);
    autoSelectionMethodCombo.setSelectedId(1, juce::dontSendNotification);
    autoSelectionMethodCombo.setEnabled(false);

    addAndMakeVisible(autoSingerCountLabel);
    autoSingerCountLabel.setText(jp(u8"合唱人数"), juce::dontSendNotification);
    autoSingerCountLabel.setJustificationType(juce::Justification::centred);

    addAndMakeVisible(autoSingerCountSlider);
    autoSingerCountSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    autoSingerCountSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 56, 18);
    autoSingerCountSlider.setRange(1.0, 2.0, 1.0);
    autoSingerCountSlider.setValue(1.0, juce::dontSendNotification);

    addAndMakeVisible(autoPanWidthLabel);
    autoPanWidthLabel.setText(jp(u8"Pan幅"), juce::dontSendNotification);
    autoPanWidthLabel.setJustificationType(juce::Justification::centred);

    addAndMakeVisible(autoPanWidthSlider);
    autoPanWidthSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    autoPanWidthSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 56, 18);
    autoPanWidthSlider.setRange(0.0, 100.0, 1.0);
    autoPanWidthSlider.setValue(40.0, juce::dontSendNotification);

    addAndMakeVisible(showAllStylesToggle);
    showAllStylesToggle.setButtonText(jp(u8"表示"));
    showAllStylesToggle.setClickingTogglesState(true);
    showAllStylesToggle.setToggleState(isShowingAllStyles, juce::dontSendNotification);
    showAllStylesToggleLookAndFeel = std::make_unique<StyleSwitchLookAndFeel>();
    showAllStylesToggle.setLookAndFeel(showAllStylesToggleLookAndFeel.get());
    showAllStylesToggle.onClick = [this]
    {
        if (isLoadingSingers || isGeneratingVoicevox)
            return;

        isShowingAllStyles = showAllStylesToggle.getToggleState();
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

        const auto selectedSingers = getSelectedSingers();
        juce::String suffix;
        if (selectedSingers.isEmpty())
        {
            suffix = jp(u8"chorus");
        }
        else if (selectedSingers.size() == 1)
        {
            const auto &singer = selectedSingers.getReference(0);
            suffix = singer.singerName + jp(u8"（") + singer.styleName + jp(u8"）");
        }
        else
        {
            suffix = jp(u8"chorus_") + juce::String(selectedSingers.size()) + jp(u8"キャラ");
        }

        baseName = sanitizeFileNamePart(baseName);
        suffix = sanitizeFileNamePart(suffix);

        auto defaultName = baseName + "-" + suffix + ".wav";

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

        juce::Array<voicevox::SingerStyle> selectedSingers;
        juce::Array<float> panPositions;

        if (singerSelectionMode == SingerSelectionMode::manual)
        {
            selectedSingers = getSelectedSingers();
        }
        else
        {
            selectedSingers = getAutoSelectedSingers();
            panPositions = buildPanPositions(selectedSingers.size());
        }

        if (selectedSingers.isEmpty())
        {
            statusLabel.setText(singerSelectionMode == SingerSelectionMode::manual
                                    ? jp(u8"先にキャラを1人以上選択してください")
                                    : jp(u8"自動編成に使えるキャラがありません"),
                                juce::dontSendNotification);
            return;
        }

        if (!selectedVvprojFile.existsAsFile())
        {
            statusLabel.setText(jp(u8"先にvvprojを選択してください"), juce::dontSendNotification);
            return;
        }

        startVoicevoxGeneration(selectedSingers, panPositions);
    };

    applyTheme();
    setResizable(true, true);
    setResizeLimits(600, 760, 1240, 1180);
    setSize(860, 960);
    updateSelectionModeUi();

    startTimerHz(30);
    startFetchSingers();
}

VVChorusPlayerAudioProcessorEditor::~VVChorusPlayerAudioProcessorEditor()
{
    singerListBox.setModel(nullptr);
    showAllStylesToggle.setLookAndFeel(nullptr);
}

int VVChorusPlayerAudioProcessorEditor::getNumRows()
{
    return availableSingers.size();
}

void VVChorusPlayerAudioProcessorEditor::paintListBoxItem(int rowNumber, juce::Graphics &g, int width, int height, bool rowIsSelected)
{
    if (!juce::isPositiveAndBelow(rowNumber, availableSingers.size()))
        return;

    const auto &singer = availableSingers.getReference(rowNumber);
    const auto checked = containsSpeakerId(selectedSpeakerIds, singer.speakerId);

    auto bounds = juce::Rectangle<int>(0, 0, width, height);
    if (rowIsSelected)
        g.fillAll(juce::Colour::fromRGBA(120, 170, 130, 22));

    auto content = bounds.reduced(8, 4);
    const auto checkSize = juce::jlimit(14, 18, content.getHeight());
    auto checkBounds = content.removeFromLeft(checkSize).withSizeKeepingCentre(checkSize, checkSize);
    content.removeFromLeft(8);

    g.setColour(juce::Colours::white.withAlpha(0.96f));
    g.fillRoundedRectangle(checkBounds.toFloat(), 3.0f);
    g.setColour(juce::Colour::fromRGB(103, 150, 114));
    g.drawRoundedRectangle(checkBounds.toFloat(), 3.0f, 1.2f);

    if (checked)
    {
        juce::Path tick;
        tick.startNewSubPath(static_cast<float>(checkBounds.getX() + 3), static_cast<float>(checkBounds.getCentreY()));
        tick.lineTo(static_cast<float>(checkBounds.getX() + checkBounds.getWidth() / 2 - 1), static_cast<float>(checkBounds.getBottom() - 4));
        tick.lineTo(static_cast<float>(checkBounds.getRight() - 3), static_cast<float>(checkBounds.getY() + 4));
        g.setColour(juce::Colour::fromRGB(56, 118, 72));
        g.strokePath(tick, juce::PathStrokeType(2.0f));
    }

    g.setColour(juce::Colour::fromRGB(35, 66, 45));
    g.setFont(juce::Font(juce::FontOptions(15.0f)));
    g.drawFittedText(getSingerDisplayText(singer), content, juce::Justification::centredLeft, 1, 0.92f);
}

void VVChorusPlayerAudioProcessorEditor::listBoxItemClicked(int rowNumber, const juce::MouseEvent &event)
{
    juce::ignoreUnused(event);

    if (singerSelectionMode != SingerSelectionMode::manual)
        return;

    if (isGeneratingVoicevox || isLoadingSingers)
        return;

    if (!juce::isPositiveAndBelow(rowNumber, availableSingers.size()))
        return;

    const auto &singer = availableSingers.getReference(rowNumber);
    const auto wasSelected = containsSpeakerId(selectedSpeakerIds, singer.speakerId);
    setSpeakerSelected(selectedSpeakerIds, singer.speakerId, !wasSelected);
    updateSingerSelectionLabel();
    updateActionState();
    singerListBox.repaintRow(rowNumber);
}

juce::Component *VVChorusPlayerAudioProcessorEditor::refreshComponentForRow(int rowNumber, bool isRowSelected, juce::Component *existingComponentToUpdate)
{
    juce::ignoreUnused(rowNumber, isRowSelected, existingComponentToUpdate);
    return nullptr;
}

void VVChorusPlayerAudioProcessorEditor::syncSelectionModeFromTab()
{
    const auto newCurrentTabIndex = selectionModeTabs.getCurrentTabIndex();
    const auto newMode = (newCurrentTabIndex == static_cast<int>(SingerSelectionMode::autoArrange))
                             ? SingerSelectionMode::autoArrange
                             : SingerSelectionMode::manual;
    if (singerSelectionMode == newMode)
        return;

    singerSelectionMode = newMode;
    if (singerSelectionMode == SingerSelectionMode::autoArrange)
    {
        isShowingAllStyles = false;
        showAllStylesToggle.setToggleState(false, juce::dontSendNotification);
    }

    updateSelectionModeUi();
    startFetchSingers();
}

void VVChorusPlayerAudioProcessorEditor::updateSelectionModeUi()
{
    const auto isManual = singerSelectionMode == SingerSelectionMode::manual;

    singerSelectionLabel.setVisible(isManual);
    singerListBox.setVisible(isManual);
    selectAllSingersButton.setVisible(isManual);
    clearSingerSelectionButton.setVisible(isManual);
    showAllStylesToggle.setVisible(isManual);

    autoSelectionMethodLabel.setVisible(!isManual);
    autoSelectionMethodCombo.setVisible(!isManual);
    autoSingerCountLabel.setVisible(!isManual);
    autoSingerCountSlider.setVisible(!isManual);
    autoPanWidthLabel.setVisible(!isManual);
    autoPanWidthSlider.setVisible(!isManual);

    resized();
    repaint();
}

void VVChorusPlayerAudioProcessorEditor::startFetchSingers()
{
    if (isLoadingSingers)
        return;

    isLoadingSingers = true;
    updateActionState();

    const auto safeThis = juce::Component::SafePointer<VVChorusPlayerAudioProcessorEditor>(this);
    const auto baseUrl = voicevoxBaseUrl;
    const auto includeAllStyles = (singerSelectionMode == SingerSelectionMode::manual) && isShowingAllStyles;

    std::thread([safeThis, baseUrl, includeAllStyles]
                {
        juce::Array<voicevox::SingerStyle> singers;
        const auto result = voicevox::fetchSingers (baseUrl, singers, includeAllStyles);

        juce::MessageManager::callAsync ([safeThis, result, singers]
        {
            if (safeThis == nullptr)
                return;

            safeThis->isLoadingSingers = false;
            safeThis->availableSingers = singers;

            if (result.failed())
            {
                juce::Logger::writeToLog (jp(u8"キャラ取得失敗: ") + result.getErrorMessage());
                safeThis->isVoicevoxUnavailable = true;
                safeThis->voicevoxWarningLabel.setVisible (true);
                safeThis->nextVoicevoxRetryTick = juce::Time::getMillisecondCounter() + 2500;
                safeThis->rebuildSingerListItems();
                safeThis->updateActionState();
                safeThis->resized();
                safeThis->repaint();
                return;
            }

            safeThis->isVoicevoxUnavailable = false;
            safeThis->voicevoxWarningLabel.setVisible (false);
            safeThis->rebuildSingerListItems();
            safeThis->updateActionState();
            safeThis->resized();
            safeThis->repaint();
        }); })
        .detach();
}

void VVChorusPlayerAudioProcessorEditor::rebuildSingerListItems()
{
    juce::Array<int> validSelectedSpeakerIds;
    for (const auto &singer : availableSingers)
    {
        if (containsSpeakerId(selectedSpeakerIds, singer.speakerId))
            validSelectedSpeakerIds.addIfNotAlreadyThere(singer.speakerId);
    }

    selectedSpeakerIds.swapWith(validSelectedSpeakerIds);

    singerListBox.updateContent();
    singerListBox.repaint();
    updateSingerSelectionLabel();
}

juce::Array<voicevox::SingerStyle> VVChorusPlayerAudioProcessorEditor::getSelectedSingers() const
{
    juce::Array<voicevox::SingerStyle> selectedSingers;

    for (const auto &singer : availableSingers)
        if (containsSpeakerId(selectedSpeakerIds, singer.speakerId))
            selectedSingers.add(singer);

    return selectedSingers;
}

juce::Array<voicevox::SingerStyle> VVChorusPlayerAudioProcessorEditor::getAutoSelectedSingers() const
{
    juce::Array<voicevox::SingerStyle> selected;
    if (availableSingers.isEmpty())
        return selected;

    const auto requestedCount = static_cast<int>(std::lround(autoSingerCountSlider.getValue()));
    const auto targetCount = juce::jlimit(1, availableSingers.size(), requestedCount);

    juce::Array<int> indices;
    indices.ensureStorageAllocated(availableSingers.size());
    for (int i = 0; i < availableSingers.size(); ++i)
        indices.add(i);

    auto &random = juce::Random::getSystemRandom();
    for (int i = indices.size() - 1; i > 0; --i)
    {
        const auto swapIndex = random.nextInt(i + 1);
        indices.swap(i, swapIndex);
    }

    for (int i = 0; i < targetCount; ++i)
        selected.add(availableSingers.getReference(indices[i]));

    return selected;
}

juce::Array<float> VVChorusPlayerAudioProcessorEditor::buildPanPositions(int singerCount) const
{
    juce::Array<float> panPositions;
    if (singerCount <= 0)
        return panPositions;

    panPositions.ensureStorageAllocated(singerCount);
    const auto normalizedWidth = static_cast<float>(autoPanWidthSlider.getValue() / 100.0);
    const auto maxPan = juce::jlimit(0.0f, 1.0f, normalizedWidth * 0.3f);

    if (singerCount == 1)
    {
        panPositions.add(0.0f);
        return panPositions;
    }

    for (int i = 0; i < singerCount; ++i)
    {
        const auto ratio = static_cast<float>(i) / static_cast<float>(singerCount - 1);
        panPositions.add(-maxPan + ratio * (maxPan * 2.0f));
    }

    return panPositions;
}

juce::String VVChorusPlayerAudioProcessorEditor::getSingerDisplayText(const voicevox::SingerStyle &singer) const
{
    if (isShowingAllStyles)
        return singer.singerName + " (" + singer.styleName + ")";
    return singer.singerName;
}

void VVChorusPlayerAudioProcessorEditor::updateSingerSelectionLabel()
{
    singerSelectionLabel.setText(juce::String(selectedSpeakerIds.size()) + jp(u8"キャラ選択中"), juce::dontSendNotification);
}

void VVChorusPlayerAudioProcessorEditor::startVoicevoxGeneration(const juce::Array<voicevox::SingerStyle> &selectedSingers,
                                                                 const juce::Array<float> &panPositions)
{
    isGeneratingVoicevox = true;
    displayedProgress = 0.0;
    lastRawProgress = 0.0f;
    lastTimerSeconds = juce::Time::getMillisecondCounterHiRes() * 0.001;
    lastRawProgressSeconds = lastTimerSeconds;
    secondsPerPercentEstimate = 0.12;

    updateActionState();
    statusLabel.setText(jp(u8"合唱生成を開始...\n選択キャラ数: ") + juce::String(selectedSingers.size()),
                        juce::dontSendNotification);

    auto *processor = &audioProcessor;
    const auto safeThis = juce::Component::SafePointer<VVChorusPlayerAudioProcessorEditor>(this);

    std::thread([safeThis, processor, selectedFile = selectedVvprojFile, selectedSingers, panPositions, baseUrl = voicevoxBaseUrl]
                {
        const auto result = processor->generateChorusFromVvproj (selectedFile,
                                                                  0,
                                                                  selectedSingers,
                                                                  panPositions,
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
    const auto isManualMode = singerSelectionMode == SingerSelectionMode::manual;
    const auto hasSingerSelection = isManualMode
                                        ? !selectedSpeakerIds.isEmpty()
                                        : !availableSingers.isEmpty();
    const auto hasVvproj = selectedVvprojFile.existsAsFile();
    const auto isUiLocked = isVoicevoxUnavailable;
    const auto canGenerate = !isUiLocked && !isLoadingSingers && !isGeneratingVoicevox && hasSingerSelection && hasVvproj;

    if (!isManualMode)
    {
        const auto maxCount = juce::jmax(1, availableSingers.size());
        const auto sliderMax = juce::jmax(2, maxCount);
        autoSingerCountSlider.setRange(1.0, static_cast<double>(sliderMax), 1.0);
        if (autoSingerCountSlider.getValue() > maxCount)
            autoSingerCountSlider.setValue(static_cast<double>(maxCount), juce::dontSendNotification);
    }

    selectionModeTabs.setEnabled(!isUiLocked && !isLoadingSingers && !isGeneratingVoicevox);
    singerListBox.setEnabled(isManualMode && !isUiLocked && !isLoadingSingers && !isGeneratingVoicevox && !availableSingers.isEmpty());
    singerSelectionLabel.setEnabled(isManualMode && !isUiLocked);
    selectAllSingersButton.setEnabled(isManualMode && !isUiLocked && !isLoadingSingers && !isGeneratingVoicevox && !availableSingers.isEmpty());
    clearSingerSelectionButton.setEnabled(isManualMode && !isUiLocked && !isLoadingSingers && !isGeneratingVoicevox && !selectedSpeakerIds.isEmpty());
    showAllStylesToggle.setEnabled(isManualMode && !isUiLocked && !isLoadingSingers && !isGeneratingVoicevox);
    autoSelectionMethodCombo.setEnabled(!isManualMode && !isUiLocked && !isLoadingSingers && !isGeneratingVoicevox);
    autoSingerCountSlider.setEnabled(!isManualMode && !isUiLocked && !isLoadingSingers && !isGeneratingVoicevox && availableSingers.size() > 1);
    autoPanWidthSlider.setEnabled(!isManualMode && !isUiLocked && !isLoadingSingers && !isGeneratingVoicevox);
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
    const auto accent = juce::Colour::fromRGB(98, 168, 125);
    const auto accentSoft = juce::Colour::fromRGB(171, 214, 183);
    const auto panel = juce::Colour::fromRGB(241, 248, 243);
    const auto text = juce::Colour::fromRGB(24, 51, 35);

    selectVvprojButton.setColour(juce::TextButton::buttonColourId, accentSoft);
    selectVvprojButton.setColour(juce::TextButton::textColourOffId, text.darker(0.05f));

    generateVoicevoxButton.setColour(juce::TextButton::buttonColourId, accent.darker(0.05f));
    generateVoicevoxButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);

    singerListBox.setColour(juce::ListBox::backgroundColourId, juce::Colours::white);
    singerListBox.setColour(juce::ListBox::outlineColourId, accent.darker(0.12f));
    singerSelectionLabel.setColour(juce::Label::backgroundColourId, juce::Colours::white);
    singerSelectionLabel.setColour(juce::Label::textColourId, text);
    singerSelectionLabel.setColour(juce::Label::outlineColourId, accent.darker(0.2f));
    autoSelectionMethodLabel.setColour(juce::Label::textColourId, text);
    autoSingerCountLabel.setColour(juce::Label::textColourId, text);
    autoPanWidthLabel.setColour(juce::Label::textColourId, text);
    autoSelectionMethodCombo.setColour(juce::ComboBox::backgroundColourId, juce::Colours::white);
    autoSelectionMethodCombo.setColour(juce::ComboBox::textColourId, text);
    autoSelectionMethodCombo.setColour(juce::ComboBox::outlineColourId, accent.darker(0.2f));
    autoSingerCountSlider.setColour(juce::Slider::rotarySliderFillColourId, accent.darker(0.08f));
    autoSingerCountSlider.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour::fromRGB(177, 204, 183));
    autoSingerCountSlider.setColour(juce::Slider::thumbColourId, accent.darker(0.2f));
    autoSingerCountSlider.setColour(juce::Slider::textBoxTextColourId, text);
    autoSingerCountSlider.setColour(juce::Slider::textBoxOutlineColourId, accent.darker(0.2f));
    autoPanWidthSlider.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour::fromRGB(79, 156, 188));
    autoPanWidthSlider.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour::fromRGB(177, 204, 183));
    autoPanWidthSlider.setColour(juce::Slider::thumbColourId, juce::Colour::fromRGB(67, 139, 170));
    autoPanWidthSlider.setColour(juce::Slider::textBoxTextColourId, text);
    autoPanWidthSlider.setColour(juce::Slider::textBoxOutlineColourId, accent.darker(0.2f));

    selectAllSingersButton.setColour(juce::TextButton::buttonColourId, accent);
    selectAllSingersButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    clearSingerSelectionButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(199, 219, 205));
    clearSingerSelectionButton.setColour(juce::TextButton::textColourOffId, text.darker(0.05f));

    showAllStylesToggle.setColour(juce::ToggleButton::textColourId, text);
    showAllStylesToggle.setColour(juce::ToggleButton::tickColourId, accent.darker(0.15f));
    showAllStylesToggle.setColour(juce::ToggleButton::tickDisabledColourId, juce::Colour::fromRGB(151, 171, 157));

    playButton.setColour(juce::TextButton::buttonColourId, accent.darker(0.02f));
    playButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    stopButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(198, 217, 203));
    stopButton.setColour(juce::TextButton::textColourOffId, text);

    exportAudioButton.setColour(juce::TextButton::buttonColourId, accent);
    exportAudioButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);

    previewPositionSlider.setColour(juce::Slider::trackColourId, accent.darker(0.15f));
    previewPositionSlider.setColour(juce::Slider::thumbColourId, accent.darker(0.25f));
    previewPositionSlider.setColour(juce::Slider::backgroundColourId, juce::Colours::white);

    selectedVvprojLabel.setColour(juce::Label::backgroundColourId, juce::Colours::white);
    selectedVvprojLabel.setColour(juce::Label::textColourId, text);
    selectedVvprojLabel.setColour(juce::Label::outlineColourId, accent.darker(0.2f));

    statusLabel.setColour(juce::Label::backgroundColourId, panel.withAlpha(0.98f));
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
    syncSelectionModeFromTab();

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
    const auto top = juce::Colour::fromRGB(176, 207, 185);
    const auto bottom = juce::Colour::fromRGB(158, 193, 171);
    juce::ColourGradient bgGradient(top, 0.0f, 0.0f, bottom, 0.0f, static_cast<float>(getHeight()), false);
    g.setGradientFill(bgGradient);
    g.fillAll();

    g.setColour(juce::Colour::fromRGB(244, 250, 246));
    const auto titleHeight = juce::jlimit(24, 40, getHeight() / 12);
    const auto titleFontSize = juce::jlimit(18.0f, 34.0f, static_cast<float>(titleHeight) * 0.74f);
    g.setFont(juce::Font(juce::FontOptions(titleFontSize)).boldened());
    g.drawFittedText("VVChorusPlayer", getLocalBounds().removeFromTop(titleHeight), juce::Justification::centred, 1);

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
    auto area = getLocalBounds().reduced(juce::jlimit(18, 40, getWidth() / 22));
    const auto titleHeight = juce::jlimit(24, 40, getHeight() / 12);
    area.removeFromTop(titleHeight + juce::jlimit(8, 14, getHeight() / 70));

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

    const auto controlHeight = juce::jlimit(24, 42, getHeight() / 15);
    const auto spacing = juce::jlimit(8, 22, getHeight() / 44);
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
        auto tabArea = row.removeFromRight(juce::jmin(320, row.getWidth()));
        const auto tabHeight = juce::jlimit(26, 32, controlHeight - 2);
        selectionModeTabs.setBounds(tabArea.withSizeKeepingCentre(tabArea.getWidth(), tabHeight));
    }
    area.removeFromTop(spacing);

    if (singerSelectionMode == SingerSelectionMode::manual)
    {
        {
            auto row = area.removeFromTop(controlHeight);
            showAllStylesToggle.setBounds(row.removeFromRight(buttonWidth));
            autoSelectionMethodLabel.setBounds({});
            autoSelectionMethodCombo.setBounds({});
            autoSingerCountLabel.setBounds({});
            autoSingerCountSlider.setBounds({});
            autoPanWidthLabel.setBounds({});
            autoPanWidthSlider.setBounds({});
        }
        area.removeFromTop(spacing);

        {
            auto row = area.removeFromTop(controlHeight);
            singerSelectionLabel.setBounds(row.removeFromLeft(juce::jmax(160, getWidth() / 4)));
            row.removeFromLeft(spacing / 2);
            clearSingerSelectionButton.setBounds(row.removeFromRight(juce::jmin(140, buttonWidth)));
            row.removeFromRight(spacing / 2);
            selectAllSingersButton.setBounds(row.removeFromRight(juce::jmin(120, buttonWidth / 2 + 40)));
        }
        area.removeFromTop(spacing);

        const auto singerListHeight = juce::jlimit(96, 180, getHeight() / 6);
        singerListBox.setBounds(area.removeFromTop(singerListHeight));
        area.removeFromTop(spacing);
    }
    else
    {
        showAllStylesToggle.setBounds({});
        singerSelectionLabel.setBounds({});
        selectAllSingersButton.setBounds({});
        clearSingerSelectionButton.setBounds({});
        singerListBox.setBounds({});

        {
            auto row = area.removeFromTop(controlHeight);
            autoSelectionMethodLabel.setBounds(row.removeFromLeft(juce::jmax(120, labelWidth - 20)));
            row.removeFromLeft(spacing / 2);
            autoSelectionMethodCombo.setBounds(row.removeFromLeft(juce::jmin(220, row.getWidth())));
        }
        area.removeFromTop(spacing);

        const auto knobAreaHeight = juce::jlimit(118, 170, getHeight() / 5);
        auto knobsArea = area.removeFromTop(knobAreaHeight);
        auto left = knobsArea.removeFromLeft(knobsArea.getWidth() / 2).reduced(6, 0);
        auto right = knobsArea.reduced(6, 0);
        const auto knobLabelHeight = juce::jmin(26, controlHeight);

        autoSingerCountLabel.setBounds(left.removeFromTop(knobLabelHeight));
        autoSingerCountSlider.setBounds(left);
        autoPanWidthLabel.setBounds(right.removeFromTop(knobLabelHeight));
        autoPanWidthSlider.setBounds(right);

        area.removeFromTop(spacing);
    }

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
    const auto statusHeight = juce::jmax(48, (baseStatusHeight * 6) / 7);

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
