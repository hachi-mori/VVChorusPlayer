#pragma once

#include <JuceHeader.h>
#include <functional>

namespace voicevox
{
struct SingerStyle
{
    juce::String singerName;
    juce::String styleName;
    int speakerId { 6000 };
    int keyShiftOffset { 0 };
};

struct SynthesisOptions
{
    juce::String baseUrl { "http://127.0.0.1:50021" };
    int querySpeakerId { 6000 };
    int speakerId { 6000 };
    juce::String singerName;
    juce::String styleName;
    int keyShiftOffset { 0 };
    int trackIndex { 0 };
    int maxFramesPerSegment { 2500 };
    double outputSampleRate { 44100.0 };
    int timeoutMs { 60000 };
};

juce::Result fetchSingers (const juce::String& baseUrl,
                           juce::Array<SingerStyle>& outSingers,
                           bool includeAllStyles = false);

int getKeyAdjustment (const juce::String& singerName,
                      const juce::String& styleName);

juce::Result convertVvprojTrackToScoreJson (const juce::File& vvprojFile,
                                            const juce::File& outScoreJsonFile,
                                            int trackIndex);

juce::Result synthesizeTrackFromVvproj (const juce::File& vvprojFile,
                                        const juce::File& outputWavFile,
                                        const SynthesisOptions& options,
                                        std::function<void(float, const juce::String&)> progressCallback = {});
}
