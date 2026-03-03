/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "VoiceVoxClient.h"

namespace
{
    juce::AudioBuffer<float> resampleBufferLinear(const juce::AudioBuffer<float> &sourceBuffer,
                                                  const double sourceSampleRate,
                                                  const double targetSampleRate)
    {
        jassert(sourceSampleRate > 0.0);
        jassert(targetSampleRate > 0.0);

        if (sourceBuffer.getNumSamples() == 0 || sourceBuffer.getNumChannels() == 0)
            return {};

        if (juce::approximatelyEqual(sourceSampleRate, targetSampleRate))
            return sourceBuffer;

        const auto sourceNumSamples = sourceBuffer.getNumSamples();
        const auto targetNumSamples = juce::jmax(1, static_cast<int>(std::llround(sourceNumSamples * targetSampleRate / sourceSampleRate)));
        juce::AudioBuffer<float> result(sourceBuffer.getNumChannels(), targetNumSamples);

        for (int channel = 0; channel < sourceBuffer.getNumChannels(); ++channel)
        {
            const auto *source = sourceBuffer.getReadPointer(channel);
            auto *destination = result.getWritePointer(channel);

            for (int sample = 0; sample < targetNumSamples; ++sample)
            {
                const double sourcePosition = sample * sourceSampleRate / targetSampleRate;
                const auto index0 = juce::jlimit(0, sourceNumSamples - 1, static_cast<int>(sourcePosition));
                const auto index1 = juce::jmin(index0 + 1, sourceNumSamples - 1);
                const auto fraction = static_cast<float>(sourcePosition - index0);
                destination[sample] = source[index0] + (source[index1] - source[index0]) * fraction;
            }
        }

        return result;
    }
}

//==============================================================================
VVChorusPlayerAudioProcessor::VVChorusPlayerAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor(BusesProperties()
#if !JucePlugin_IsMidiEffect
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)
#endif
      )
#endif
{
    formatManager.registerBasicFormats();
}

VVChorusPlayerAudioProcessor::~VVChorusPlayerAudioProcessor()
{
}

//==============================================================================
const juce::String VVChorusPlayerAudioProcessor::getName() const
{
    return "VVProject Synth";
}

bool VVChorusPlayerAudioProcessor::acceptsMidi() const
{
    return true;
}

bool VVChorusPlayerAudioProcessor::producesMidi() const
{
#if JucePlugin_ProducesMidiOutput
    return true;
#else
    return false;
#endif
}

bool VVChorusPlayerAudioProcessor::isMidiEffect() const
{
#if JucePlugin_IsMidiEffect
    return true;
#else
    return false;
#endif
}

double VVChorusPlayerAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int VVChorusPlayerAudioProcessor::getNumPrograms()
{
    return 1; // NB: some hosts don't cope very well if you tell them there are 0 programs,
              // so this should be at least 1, even if you're not really implementing programs.
}

int VVChorusPlayerAudioProcessor::getCurrentProgram()
{
    return 0;
}

void VVChorusPlayerAudioProcessor::setCurrentProgram(int index)
{
}

const juce::String VVChorusPlayerAudioProcessor::getProgramName(int index)
{
    return {};
}

void VVChorusPlayerAudioProcessor::changeProgramName(int index, const juce::String &newName)
{
}

//==============================================================================
void VVChorusPlayerAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(samplesPerBlock);

    currentHostSampleRate.store(sampleRate, std::memory_order_release);

    if (!fileLoaded.load(std::memory_order_acquire))
        return;

    juce::AudioBuffer<float> sourceCopy;
    double sourceRate = 0.0;

    {
        const juce::SpinLock::ScopedLockType lock(loadedBufferLock);
        sourceCopy.makeCopyOf(loadedBuffer);
        sourceRate = loadedBufferSampleRate.load(std::memory_order_acquire);
    }

    if (sourceCopy.getNumSamples() == 0 || sourceRate <= 0.0 || juce::approximatelyEqual(sourceRate, sampleRate))
        return;

    auto converted = resampleBufferLinear(sourceCopy, sourceRate, sampleRate);

    {
        const juce::SpinLock::ScopedLockType lock(loadedBufferLock);
        loadedBuffer = std::move(converted);
        loadedBufferSampleRate.store(sampleRate, std::memory_order_release);
    }
}

void VVChorusPlayerAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool VVChorusPlayerAudioProcessor::isBusesLayoutSupported(const BusesLayout &layouts) const
{
#if JucePlugin_IsMidiEffect
    juce::ignoreUnused(layouts);
    return true;
#else
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    if (!layouts.getMainInputChannelSet().isDisabled())
        return false;

    return true;
#endif
}
#endif

void VVChorusPlayerAudioProcessor::processBlock(juce::AudioBuffer<float> &buffer, juce::MidiBuffer &midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused(midiMessages);

    buffer.clear();

    juce::AudioPlayHead::CurrentPositionInfo positionInfo;
    int64_t hostStartSample = -1;
    if (auto *playHead = getPlayHead(); playHead != nullptr && playHead->getCurrentPosition(positionInfo) && positionInfo.isPlaying && positionInfo.timeInSamples >= 0)
    {
        hostStartSample = positionInfo.timeInSamples;
        hostPositionSamples.store(hostStartSample, std::memory_order_release);
        hostPlayingNow.store(true, std::memory_order_release);
    }
    else
    {
        hostPlayingNow.store(false, std::memory_order_release);
    }

    auto previewMode = previewPlaying.load(std::memory_order_acquire);

    int64_t readStartSample = 0;
    if (hostStartSample >= 0)
    {
        if (previewMode)
        {
            previewPlaying.store(false, std::memory_order_release);
            previewMode = false;
        }

        readStartSample = hostStartSample;
    }
    else if (previewMode)
    {
        readStartSample = previewPositionSamples.load(std::memory_order_acquire);
    }
    else
    {
        return;
    }

    const juce::SpinLock::ScopedTryLockType lock(loadedBufferLock);
    if (!lock.isLocked() || !fileLoaded.load(std::memory_order_acquire) || loadedBuffer.getNumSamples() == 0)
        return;

    const auto sourceNumSamples = loadedBuffer.getNumSamples();
    if (readStartSample >= sourceNumSamples)
    {
        if (previewMode)
            previewPlaying.store(false, std::memory_order_release);
        return;
    }

    const auto blockNumSamples = buffer.getNumSamples();
    const auto numToCopy = juce::jmin(blockNumSamples, sourceNumSamples - static_cast<int>(readStartSample));
    const auto sourceNumChannels = loadedBuffer.getNumChannels();

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        const auto sourceChannel = juce::jmin(channel, sourceNumChannels - 1);
        const auto *src = loadedBuffer.getReadPointer(sourceChannel, static_cast<int>(readStartSample));
        auto *dst = buffer.getWritePointer(channel);
        juce::FloatVectorOperations::copy(dst, src, numToCopy);

        if (numToCopy < blockNumSamples)
            juce::FloatVectorOperations::clear(dst + numToCopy, blockNumSamples - numToCopy);
    }

    if (previewMode)
    {
        const auto next = static_cast<int64_t>(readStartSample) + numToCopy;
        previewPositionSamples.store(next, std::memory_order_release);
        if (next >= sourceNumSamples)
            previewPlaying.store(false, std::memory_order_release);
    }
    else
    {
        const auto next = static_cast<int64_t>(readStartSample) + numToCopy;
        hostPositionSamples.store(next, std::memory_order_release);
    }
}

juce::Result VVChorusPlayerAudioProcessor::loadFile(const juce::File &file)
{
    if (!file.existsAsFile())
        return juce::Result::fail("File not found");

    auto reader = std::unique_ptr<juce::AudioFormatReader>(formatManager.createReaderFor(file));
    if (reader == nullptr)
        return juce::Result::fail("Unsupported or unreadable audio file");

    if (reader->numChannels <= 0 || reader->lengthInSamples <= 0)
        return juce::Result::fail("Audio file is empty");

    juce::AudioBuffer<float> tempBuffer(static_cast<int>(reader->numChannels), static_cast<int>(reader->lengthInSamples));
    if (!reader->read(&tempBuffer, 0, static_cast<int>(reader->lengthInSamples), 0, true, true))
        return juce::Result::fail("Failed to read audio samples");

    const auto sourceSampleRate = reader->sampleRate;
    const auto hostSampleRate = currentHostSampleRate.load(std::memory_order_acquire) > 0.0
                                    ? currentHostSampleRate.load(std::memory_order_acquire)
                                    : getSampleRate();

    if (sourceSampleRate > 0.0 && hostSampleRate > 0.0 && !juce::approximatelyEqual(sourceSampleRate, hostSampleRate))
        tempBuffer = resampleBufferLinear(tempBuffer, sourceSampleRate, hostSampleRate);

    {
        const juce::SpinLock::ScopedLockType lock(loadedBufferLock);
        loadedBuffer = std::move(tempBuffer);
        loadedFileName = file.getFileName();
        fileLoaded.store(true, std::memory_order_release);
        loadedBufferSampleRate.store(hostSampleRate > 0.0 ? hostSampleRate : sourceSampleRate, std::memory_order_release);
    }

    previewPositionSamples.store(0, std::memory_order_release);
    previewPlaying.store(false, std::memory_order_release);

    return juce::Result::ok();
}

juce::Result VVChorusPlayerAudioProcessor::generateFromVvproj(const juce::File &vvprojFile,
                                                              int trackIndex,
                                                              int speakerId,
                                                              const juce::String &singerName,
                                                              const juce::String &styleName,
                                                              const juce::String &baseUrl)
{
    voicevoxProgress.store(0.0f, std::memory_order_release);
    {
        const juce::SpinLock::ScopedLockType statusLock(voicevoxStatusLock);
        voicevoxStatus = "Preparing VOICEVOX request";
    }

    voicevox::SynthesisOptions options;
    options.baseUrl = baseUrl;
    options.trackIndex = juce::jmax(0, trackIndex);
    options.querySpeakerId = 6000;
    options.speakerId = juce::jmax(0, speakerId);
    options.singerName = singerName;
    options.styleName = styleName;
    options.maxFramesPerSegment = 2500;

    const auto hostSampleRate = currentHostSampleRate.load(std::memory_order_acquire);
    options.outputSampleRate = hostSampleRate > 0.0 ? hostSampleRate : 44100.0;

    const auto tempOutput = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                .getChildFile("VVChorusPlayer")
                                .getNonexistentChildFile("voicevox_joined_", ".wav", false);

    const auto synthResult = voicevox::synthesizeTrackFromVvproj(
        vvprojFile,
        tempOutput,
        options,
        [this](float progress, const juce::String &status)
        {
            voicevoxProgress.store(progress, std::memory_order_release);
            const juce::SpinLock::ScopedLockType statusLock(voicevoxStatusLock);
            voicevoxStatus = status;
        });

    if (synthResult.failed())
    {
        const juce::SpinLock::ScopedLockType statusLock(voicevoxStatusLock);
        voicevoxStatus = "Failed";
        return synthResult;
    }

    const auto loadResult = loadFile(tempOutput);
    tempOutput.deleteFile();

    voicevoxProgress.store(1.0f, std::memory_order_release);
    {
        const juce::SpinLock::ScopedLockType statusLock(voicevoxStatusLock);
        voicevoxStatus = loadResult.wasOk() ? "Loaded to playback buffer" : "Load generated WAV failed";
    }

    return loadResult;
}

float VVChorusPlayerAudioProcessor::getVoicevoxProgress() const noexcept
{
    return voicevoxProgress.load(std::memory_order_acquire);
}

juce::String VVChorusPlayerAudioProcessor::getVoicevoxStatus() const
{
    const juce::SpinLock::ScopedLockType statusLock(voicevoxStatusLock);
    return voicevoxStatus;
}

bool VVChorusPlayerAudioProcessor::hasLoadedFile() const noexcept
{
    return fileLoaded.load(std::memory_order_acquire);
}

juce::String VVChorusPlayerAudioProcessor::getLoadedFileName() const
{
    const juce::SpinLock::ScopedLockType lock(loadedBufferLock);
    return loadedFileName;
}

juce::Result VVChorusPlayerAudioProcessor::exportLoadedAudioToWav(const juce::File &outputFile) const
{
    if (outputFile == juce::File())
        return juce::Result::fail("Output file path is empty");

    juce::AudioBuffer<float> copy;
    double sampleRate = currentHostSampleRate.load(std::memory_order_acquire);

    {
        const juce::SpinLock::ScopedLockType lock(loadedBufferLock);
        if (loadedBuffer.getNumSamples() <= 0 || loadedBuffer.getNumChannels() <= 0)
            return juce::Result::fail("No audio loaded");

        copy.makeCopyOf(loadedBuffer);
        const auto loadedRate = loadedBufferSampleRate.load(std::memory_order_acquire);
        if (sampleRate <= 0.0)
            sampleRate = loadedRate;
    }

    if (sampleRate <= 0.0)
        sampleRate = 44100.0;

    outputFile.getParentDirectory().createDirectory();
    std::unique_ptr<juce::FileOutputStream> stream(outputFile.createOutputStream());
    if (stream == nullptr)
        return juce::Result::fail("Failed creating output stream");

    juce::WavAudioFormat wav;
    auto writer = std::unique_ptr<juce::AudioFormatWriter>(
        wav.createWriterFor(stream.get(), sampleRate, static_cast<unsigned int>(copy.getNumChannels()), 24, {}, 0));

    if (writer == nullptr)
        return juce::Result::fail("Failed creating WAV writer");

    stream.release();

    if (!writer->writeFromAudioSampleBuffer(copy, 0, copy.getNumSamples()))
        return juce::Result::fail("Failed writing WAV data");

    return juce::Result::ok();
}

double VVChorusPlayerAudioProcessor::getLoadedDurationSeconds() const noexcept
{
    const auto sampleRate = currentHostSampleRate.load(std::memory_order_acquire);
    if (sampleRate <= 0.0)
        return 0.0;

    const juce::SpinLock::ScopedLockType lock(loadedBufferLock);
    if (loadedBuffer.getNumSamples() <= 0)
        return 0.0;

    return static_cast<double>(loadedBuffer.getNumSamples()) / sampleRate;
}

void VVChorusPlayerAudioProcessor::setPreviewPlaying(bool shouldPlay) noexcept
{
    if (!fileLoaded.load(std::memory_order_acquire))
    {
        previewPlaying.store(false, std::memory_order_release);
        return;
    }

    if (shouldPlay)
    {
        int totalSamples = 0;
        {
            const juce::SpinLock::ScopedLockType lock(loadedBufferLock);
            totalSamples = loadedBuffer.getNumSamples();
        }

        auto position = previewPositionSamples.load(std::memory_order_acquire);
        if (position >= totalSamples)
            previewPositionSamples.store(0, std::memory_order_release);
    }

    previewPlaying.store(shouldPlay, std::memory_order_release);
}

bool VVChorusPlayerAudioProcessor::isPreviewPlaying() const noexcept
{
    return previewPlaying.load(std::memory_order_acquire);
}

void VVChorusPlayerAudioProcessor::setPreviewPositionSeconds(double seconds) noexcept
{
    const auto sampleRate = currentHostSampleRate.load(std::memory_order_acquire);
    if (sampleRate <= 0.0)
        return;

    int totalSamples = 0;
    {
        const juce::SpinLock::ScopedLockType lock(loadedBufferLock);
        totalSamples = loadedBuffer.getNumSamples();
    }

    const auto targetSamples = static_cast<int64_t>(juce::jlimit(0.0, static_cast<double>(totalSamples), seconds * sampleRate));
    previewPositionSamples.store(targetSamples, std::memory_order_release);
}

double VVChorusPlayerAudioProcessor::getPreviewPositionSeconds() const noexcept
{
    const auto sampleRate = currentHostSampleRate.load(std::memory_order_acquire);
    if (sampleRate <= 0.0)
        return 0.0;

    const auto samples = previewPositionSamples.load(std::memory_order_acquire);
    return static_cast<double>(samples) / sampleRate;
}

double VVChorusPlayerAudioProcessor::getHostPositionSeconds() const noexcept
{
    const auto sampleRate = currentHostSampleRate.load(std::memory_order_acquire);
    if (sampleRate <= 0.0)
        return 0.0;

    const auto samples = hostPositionSamples.load(std::memory_order_acquire);
    return static_cast<double>(samples) / sampleRate;
}

bool VVChorusPlayerAudioProcessor::isHostPlayingNow() const noexcept
{
    return hostPlayingNow.load(std::memory_order_acquire);
}

void VVChorusPlayerAudioProcessor::getWaveformPeaks(int numPoints, juce::Array<float> &outPeaks) const
{
    outPeaks.clearQuick();

    const juce::SpinLock::ScopedLockType lock(loadedBufferLock);
    const auto channels = loadedBuffer.getNumChannels();
    const auto totalSamples = loadedBuffer.getNumSamples();
    if (channels <= 0 || totalSamples <= 0 || numPoints <= 0)
        return;

    outPeaks.ensureStorageAllocated(numPoints);
    const auto windowSize = juce::jmax(1, totalSamples / numPoints);

    for (int i = 0; i < numPoints; ++i)
    {
        const auto start = i * windowSize;
        const auto end = juce::jmin(totalSamples, start + windowSize);
        float peak = 0.0f;

        for (int sample = start; sample < end; ++sample)
        {
            float mixed = 0.0f;
            for (int ch = 0; ch < channels; ++ch)
                mixed += std::abs(loadedBuffer.getSample(ch, sample));

            mixed /= static_cast<float>(channels);
            peak = juce::jmax(peak, mixed);
        }

        outPeaks.add(juce::jlimit(0.0f, 1.0f, peak));
    }
}

//==============================================================================
bool VVChorusPlayerAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor *VVChorusPlayerAudioProcessor::createEditor()
{
    return new VVChorusPlayerAudioProcessorEditor(*this);
}

//==============================================================================
void VVChorusPlayerAudioProcessor::getStateInformation(juce::MemoryBlock &destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.
}

void VVChorusPlayerAudioProcessor::setStateInformation(const void *data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter()
{
    return new VVChorusPlayerAudioProcessor();
}
