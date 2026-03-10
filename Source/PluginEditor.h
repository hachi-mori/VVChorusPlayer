/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "VoiceVoxClient.h"

//==============================================================================
/**
 */
class VVChorusPlayerAudioProcessorEditor : public juce::AudioProcessorEditor,
                                           private juce::Timer,
                                           private juce::ListBoxModel
{
public:
  VVChorusPlayerAudioProcessorEditor(VVChorusPlayerAudioProcessor &);
  ~VVChorusPlayerAudioProcessorEditor() override;

  //==============================================================================
  void paint(juce::Graphics &) override;
  void resized() override;

private:
  class StyleSwitchLookAndFeel;
  class ControlButtonLookAndFeel;
  enum class SingerSelectionMode
  {
    manual = 0,
    autoArrange = 1
  };

  int getNumRows() override;
  void paintListBoxItem(int rowNumber, juce::Graphics &g, int width, int height, bool rowIsSelected) override;
  void listBoxItemClicked(int rowNumber, const juce::MouseEvent &event) override;
  juce::Component *refreshComponentForRow(int rowNumber, bool isRowSelected, juce::Component *existingComponentToUpdate) override;
  void timerCallback() override;
  void startFetchSingers();
  void startVoicevoxGeneration(const juce::Array<voicevox::SingerStyle> &selectedSingers,
                               const juce::Array<float> &panPositions = {});
  void rebuildSingerListItems();
  juce::Array<voicevox::SingerStyle> getSelectedSingers() const;
  juce::Array<voicevox::SingerStyle> getAutoSelectedSingers() const;
  juce::Array<float> buildPanPositions(int singerCount) const;
  juce::String getSingerDisplayText(const voicevox::SingerStyle &singer) const;
  void updateSingerSelectionLabel();
  void syncSelectionModeFromTab();
  void updateSelectionModeUi();
  void refreshPreviewUi();
  juce::String formatSeconds(double seconds) const;
  void updateActionState();
  void applyTheme();

  // This reference is provided as a quick way for your editor to
  // access the processor object that created it.
  VVChorusPlayerAudioProcessor &audioProcessor;

  juce::Label singerStepLabel;
  juce::TabbedButtonBar selectionModeTabs{juce::TabbedButtonBar::TabsAtTop};
  juce::ListBox singerListBox;
  juce::Label singerSelectionLabel;
  juce::TextButton selectAllSingersButton;
  juce::TextButton clearSingerSelectionButton;
  juce::Label autoSelectionMethodLabel;
  juce::ComboBox autoSelectionMethodCombo;
  juce::Label autoSingerCountLabel;
  juce::Slider autoSingerCountSlider;
  juce::Label autoPanWidthLabel;
  juce::Slider autoPanWidthSlider;
  juce::ToggleButton showAllStylesToggle;
  std::unique_ptr<StyleSwitchLookAndFeel> showAllStylesToggleLookAndFeel;
  std::unique_ptr<ControlButtonLookAndFeel> controlButtonLookAndFeel;
  juce::Label fileStepLabel;
  juce::TextButton selectVvprojButton;
  juce::Label selectedVvprojLabel;
  juce::Label generateStepLabel;
  juce::TextButton generateVoicevoxButton;
  juce::Label voicevoxWarningLabel;
  juce::Label statusLabel;
  juce::Label previewLabel;
  juce::TextButton playButton;
  juce::TextButton stopButton;
  juce::TextButton exportAudioButton;
  juce::Slider previewPositionSlider;
  juce::Label previewTimeLabel;
  std::unique_ptr<juce::FileChooser> vvprojChooser;
  std::unique_ptr<juce::FileChooser> exportWavChooser;
  juce::File selectedVvprojFile;
  bool isGeneratingVoicevox{false};
  bool isLoadingSingers{false};
  bool isShowingAllStyles{false};
  bool isVoicevoxUnavailable{false};
  SingerSelectionMode singerSelectionMode{SingerSelectionMode::manual};
  juce::Array<voicevox::SingerStyle> availableSingers;
  juce::Array<int> selectedSpeakerIds;
  juce::String voicevoxBaseUrl{"http://127.0.0.1:50021"};
  juce::uint32 nextVoicevoxRetryTick{0};

  double displayedProgress{0.0};
  float lastRawProgress{0.0f};
  double lastTimerSeconds{0.0};
  double lastRawProgressSeconds{0.0};
  double secondsPerPercentEstimate{0.12};
  juce::Array<float> waveformPeaks;
  juce::String waveformSourceName;
  bool isScrubbingPreview{false};
  int previewControlHeight{34};
  juce::Rectangle<int> fileSectionArea;
  juce::Rectangle<int> singerSectionArea;
  juce::Rectangle<int> generateSectionArea;
  juce::Rectangle<int> previewSectionArea;
  juce::Rectangle<int> waveformArea;
  double displayPositionSeconds{0.0};
  double displayDurationSeconds{0.0};
  bool displayFollowingHost{false};

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VVChorusPlayerAudioProcessorEditor)
};
