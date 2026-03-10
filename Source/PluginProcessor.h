/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <atomic>
#include "VoiceVoxClient.h"

//==============================================================================
/**
 */
class VVChorusPlayerAudioProcessor : public juce::AudioProcessor
{
public:
  //==============================================================================
  VVChorusPlayerAudioProcessor();
  ~VVChorusPlayerAudioProcessor() override;

  //==============================================================================
  void prepareToPlay(double sampleRate, int samplesPerBlock) override;
  void releaseResources() override;

#ifndef JucePlugin_PreferredChannelConfigurations
  bool isBusesLayoutSupported(const BusesLayout &layouts) const override;
#endif

  void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;

  //==============================================================================
  juce::AudioProcessorEditor *createEditor() override;
  bool hasEditor() const override;

  //==============================================================================
  const juce::String getName() const override;

  bool acceptsMidi() const override;
  bool producesMidi() const override;
  bool isMidiEffect() const override;
  double getTailLengthSeconds() const override;

  //==============================================================================
  int getNumPrograms() override;
  int getCurrentProgram() override;
  void setCurrentProgram(int index) override;
  const juce::String getProgramName(int index) override;
  void changeProgramName(int index, const juce::String &newName) override;

  //==============================================================================
  void getStateInformation(juce::MemoryBlock &destData) override;
  void setStateInformation(const void *data, int sizeInBytes) override;

  juce::Result loadFile(const juce::File &file);
  juce::Result generateFromVvproj(const juce::File &vvprojFile,
                                  int trackIndex,
                                  int speakerId,
                                  const juce::String &singerName,
                                  const juce::String &styleName,
                                  const juce::String &baseUrl = "http://127.0.0.1:50021");
  juce::Result generateChorusFromVvproj(const juce::File &vvprojFile,
                                        int trackIndex,
                                        const juce::Array<voicevox::SingerStyle> &singers,
                                        const juce::String &baseUrl = "http://127.0.0.1:50021");
  juce::Result generateChorusFromVvproj(const juce::File &vvprojFile,
                                        int trackIndex,
                                        const juce::Array<voicevox::SingerStyle> &singers,
                                        const juce::Array<float> &panPositions,
                                        const juce::String &baseUrl = "http://127.0.0.1:50021");
  float getVoicevoxProgress() const noexcept;
  juce::String getVoicevoxStatus() const;
  bool hasLoadedFile() const noexcept;
  juce::String getLoadedFileName() const;
  juce::Result exportLoadedAudioToWav(const juce::File &outputFile) const;
  double getLoadedDurationSeconds() const noexcept;
  void setPreviewPlaying(bool shouldPlay) noexcept;
  bool isPreviewPlaying() const noexcept;
  void setPreviewPositionSeconds(double seconds) noexcept;
  double getPreviewPositionSeconds() const noexcept;
  double getHostPositionSeconds() const noexcept;
  bool isHostPlayingNow() const noexcept;
  void getWaveformPeaks(int numPoints, juce::Array<float> &outPeaks) const;

private:
  //==============================================================================
  juce::AudioFormatManager formatManager;
  juce::AudioBuffer<float> loadedBuffer;
  mutable juce::SpinLock loadedBufferLock;
  juce::String loadedFileName;
  std::atomic<bool> fileLoaded{false};
  std::atomic<double> currentHostSampleRate{44100.0};
  std::atomic<double> loadedBufferSampleRate{0.0};
  std::atomic<float> voicevoxProgress{0.0f};
  mutable juce::SpinLock voicevoxStatusLock;
  juce::String voicevoxStatus{"Idle"};
  std::atomic<bool> previewPlaying{false};
  std::atomic<int64_t> previewPositionSamples{0};
  std::atomic<bool> hostPlayingNow{false};
  std::atomic<int64_t> hostPositionSamples{0};

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VVChorusPlayerAudioProcessor)
};
