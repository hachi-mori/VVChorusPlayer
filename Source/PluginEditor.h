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
                                           private juce::Timer
{
public:
  VVChorusPlayerAudioProcessorEditor(VVChorusPlayerAudioProcessor &);
  ~VVChorusPlayerAudioProcessorEditor() override;

  //==============================================================================
  void paint(juce::Graphics &) override;
  void resized() override;

private:
  class StyleSwitchLookAndFeel;

  void timerCallback() override;
  void startFetchSingers();
  void startVoicevoxGeneration(const voicevox::SingerStyle &selectedSinger);
  void rebuildSingerComboItems();
  void refreshPreviewUi();
  juce::String formatSeconds(double seconds) const;
  void updateActionState();
  void applyTheme();

  // This reference is provided as a quick way for your editor to
  // access the processor object that created it.
  VVChorusPlayerAudioProcessor &audioProcessor;

  juce::Label singerStepLabel;
  juce::ComboBox singerComboBox;
  juce::ToggleButton showAllStylesToggle;
  std::unique_ptr<StyleSwitchLookAndFeel> showAllStylesToggleLookAndFeel;
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
  juce::Array<voicevox::SingerStyle> availableSingers;
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
  juce::Rectangle<int> waveformArea;
  double displayPositionSeconds{0.0};
  double displayDurationSeconds{0.0};
  bool displayFollowingHost{false};

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VVChorusPlayerAudioProcessorEditor)
};
