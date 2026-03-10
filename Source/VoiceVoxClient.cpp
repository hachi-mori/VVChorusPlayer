#include "VoiceVoxClient.h"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <map>

namespace voicevox
{
namespace
{
constexpr double kFrameRate = 93.75;

void reportProgress (const std::function<void(float, const juce::String&)>& callback,
                     float progress,
                     const juce::String& message)
{
    if (callback)
        callback (juce::jlimit (0.0f, 1.0f, progress), message);
}

juce::String midiToName (int midi)
{
    static const juce::String names[12] =
    {
        "C", "C#", "D", "D#", "E", "F",
        "F#", "G", "G#", "A", "A#", "B"
    };

    const auto octave = midi / 12 - 1;
    return names[midi % 12] + juce::String (octave);
}

juce::var makeNote (int frameLength, const juce::var& key, const juce::String& lyric, const juce::String& noteLen)
{
    auto* note = new juce::DynamicObject();
    note->setProperty ("frame_length", frameLength);

    if (key.isVoid())
    {
        static const auto jsonNull = juce::JSON::parse ("null");
        note->setProperty ("key", jsonNull);
    }
    else
        note->setProperty ("key", key);

    note->setProperty ("lyric", lyric);
    note->setProperty ("notelen", noteLen);
    return juce::var (note);
}

int calcFrameLen (int64_t ticks, double bpm, double tpqn, double& carry)
{
    const auto beats = static_cast<double> (ticks) / tpqn;
    const auto seconds = beats * (60.0 / bpm);
    const auto frames = seconds * kFrameRate + carry;

    const auto frameLength = juce::jmax (1, static_cast<int> (std::floor (frames + 0.5)));
    carry = frames - static_cast<double> (frameLength);
    return frameLength;
}

double getFirstBpm (const juce::var& song)
{
    if (const auto* songObj = song.getDynamicObject())
    {
        const auto tempos = songObj->getProperty ("tempos");
        if (const auto* tempoArray = tempos.getArray(); tempoArray != nullptr && ! tempoArray->isEmpty())
        {
            if (const auto* tempoObj = tempoArray->getReference (0).getDynamicObject())
                return static_cast<double> (tempoObj->getProperty ("bpm"));
        }
    }

    return 120.0;
}

juce::Result postJson (const juce::URL& url, const juce::String& body, int timeoutMs, juce::String& response)
{
    int statusCode = 0;
    const auto requestUrl = url.withPOSTData (body);
    auto stream = requestUrl.createInputStream (
        juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
            .withHttpRequestCmd ("POST")
            .withExtraHeaders ("Content-Type: application/json\r\n")
            .withConnectionTimeoutMs (timeoutMs)
            .withStatusCode (&statusCode));

    if (stream == nullptr)
        return juce::Result::fail ("Failed HTTP connection: " + url.toString (true));

    response = stream->readEntireStreamAsString();

    if (statusCode < 200 || statusCode >= 300)
    {
        const auto responseDetail = response.isNotEmpty() ? (" response: " + response.substring (0, 2048)) : juce::String();
        const auto requestDetail = body.isNotEmpty() ? (" request: " + body.substring (0, 2048)) : juce::String();
        return juce::Result::fail ("HTTP " + juce::String (statusCode) + " at " + url.toString (true) + responseDetail + requestDetail);
    }

    return juce::Result::ok();
}

juce::Result getJson (const juce::URL& url, int timeoutMs, juce::String& response)
{
    int statusCode = 0;
    auto stream = url.createInputStream (
        juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
            .withHttpRequestCmd ("GET")
            .withConnectionTimeoutMs (timeoutMs)
            .withStatusCode (&statusCode));

    if (stream == nullptr)
        return juce::Result::fail ("Failed HTTP connection: " + url.toString (true));

    response = stream->readEntireStreamAsString();
    if (statusCode < 200 || statusCode >= 300)
        return juce::Result::fail ("HTTP " + juce::String (statusCode) + " at " + url.toString (true)
                                   + (response.isNotEmpty() ? (" response: " + response.substring (0, 2048)) : juce::String()));

    return juce::Result::ok();
}

bool isNormalStyleName (const juce::String& styleName)
{
    const auto s = styleName.trim();

    const auto isCodepointSequence = [] (const juce::String& text, std::initializer_list<juce::juce_wchar> codepoints)
    {
        if (text.length() != static_cast<int> (codepoints.size()))
            return false;

        int index = 0;
        for (const auto cp : codepoints)
        {
            if (text[index] != cp)
                return false;
            ++index;
        }

        return true;
    };

    const auto isNormal = isCodepointSequence (s, { 0x30CE, 0x30FC, 0x30DE, 0x30EB }); // ノーマル
    const auto isFutsuu = isCodepointSequence (s, { 0x3075, 0x3064, 0x3046 });         // ふつう

    return isNormal || isFutsuu || s.equalsIgnoreCase ("normal") || s.equalsIgnoreCase ("default");
}

const std::map<juce::String, int>& getKeyAdjustmentMap()
{
    static const std::map<juce::String, int> table = []
    {
        std::map<juce::String, int> loaded;

        auto tryLoad = [&loaded] (const juce::File& file) -> bool
        {
            if (! file.existsAsFile())
                return false;

            const auto parsed = juce::JSON::parse (file);
            const auto* root = parsed.getDynamicObject();
            if (root == nullptr)
                return false;

            for (const auto& singerEntry : root->getProperties())
            {
                const auto singerName = singerEntry.name.toString();
                const auto* styleObj = singerEntry.value.getDynamicObject();
                if (styleObj == nullptr)
                    continue;

                for (const auto& styleEntry : styleObj->getProperties())
                {
                    const auto styleName = styleEntry.name.toString();
                    if (! styleEntry.value.isInt() && ! styleEntry.value.isInt64() && ! styleEntry.value.isDouble())
                        continue;

                    loaded[singerName + "|" + styleName] = static_cast<int> (styleEntry.value);
                }
            }

            return ! loaded.empty();
        };

        const auto executableDir = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
        const auto workingDir = juce::File::getCurrentWorkingDirectory();
        const auto sourceDir = juce::File (__FILE__).getParentDirectory();
        const auto exeParent = executableDir.getParentDirectory();
        const auto exeGrandParent = exeParent.getParentDirectory();

        const juce::Array<juce::File> candidates
        {
            workingDir.getChildFile ("keyshift_table.json"),
            executableDir.getChildFile ("keyshift_table.json"),
            executableDir.getChildFile ("Resources").getChildFile ("keyshift_table.json"),
            workingDir.getChildFile ("Source").getChildFile ("keyshift_table.json"),
            sourceDir.getChildFile ("keyshift_table.json"),
            exeParent.getChildFile ("Source").getChildFile ("keyshift_table.json"),
            exeGrandParent.getChildFile ("Source").getChildFile ("keyshift_table.json")
        };

        for (const auto& file : candidates)
        {
            if (tryLoad (file))
            {
                juce::Logger::writeToLog ("Loaded keyshift table: " + file.getFullPathName());
                return loaded;
            }
        }

        juce::Logger::writeToLog ("keyshift_table.json not found or invalid; keyShift fallback to 0");
        return loaded;
    }();

    return table;
}

void transposeScoreVar (juce::var& score, int semitone)
{
    if (semitone == 0)
        return;

    auto* scoreObj = score.getDynamicObject();
    if (scoreObj == nullptr)
        return;

    auto notes = scoreObj->getProperty ("notes");
    auto* noteArray = notes.getArray();
    if (noteArray == nullptr)
        return;

    for (auto& note : *noteArray)
    {
        auto* noteObj = note.getDynamicObject();
        if (noteObj == nullptr)
            continue;

        const auto keyVar = noteObj->getProperty ("key");
        if (! keyVar.isInt() && ! keyVar.isInt64() && ! keyVar.isDouble())
            continue;

        const auto key = static_cast<int> (keyVar);
        const auto shifted = juce::jlimit (0, 127, key + semitone);
        noteObj->setProperty ("key", shifted);
        noteObj->setProperty ("notelen", midiToName (shifted));
    }
}

void transposeQueryVar (juce::var& query, int semitone)
{
    if (semitone == 0)
        return;

    auto* queryObj = query.getDynamicObject();
    if (queryObj == nullptr)
        return;

    const auto ratio = std::pow (2.0, static_cast<double> (semitone) / 12.0);

    auto f0 = queryObj->getProperty ("f0");
    if (auto* f0Array = f0.getArray(); f0Array != nullptr)
    {
        for (auto& value : *f0Array)
        {
            if (value.isDouble() || value.isInt() || value.isInt64())
                value = static_cast<double> (value) * ratio;
        }
    }

    auto phonemes = queryObj->getProperty ("phonemes");
    if (auto* phonemeArray = phonemes.getArray(); phonemeArray != nullptr)
    {
        for (auto& phoneme : *phonemeArray)
        {
            auto* phObj = phoneme.getDynamicObject();
            if (phObj == nullptr)
                continue;

            const auto noteIdVar = phObj->getProperty ("note_id");
            if (noteIdVar.isInt() || noteIdVar.isInt64() || noteIdVar.isDouble())
                phObj->setProperty ("note_id", static_cast<int> (noteIdVar) + semitone);
        }
    }
}

juce::Result postBinary (const juce::URL& url, const juce::String& body, int timeoutMs, juce::MemoryBlock& data)
{
    int statusCode = 0;
    const auto requestUrl = url.withPOSTData (body);
    auto stream = requestUrl.createInputStream (
        juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
            .withHttpRequestCmd ("POST")
            .withExtraHeaders ("Content-Type: application/json\r\n")
            .withConnectionTimeoutMs (timeoutMs)
            .withStatusCode (&statusCode));

    if (stream == nullptr)
        return juce::Result::fail ("Failed HTTP connection: " + url.toString (true));

    stream->readIntoMemoryBlock (data);

    if (statusCode < 200 || statusCode >= 300)
    {
        const auto responseText = juce::String::fromUTF8 (static_cast<const char*> (data.getData()), static_cast<int> (data.getSize()));
        const auto responseDetail = responseText.isNotEmpty() ? (" response: " + responseText.substring (0, 2048)) : juce::String();
        const auto requestDetail = body.isNotEmpty() ? (" request: " + body.substring (0, 2048)) : juce::String();
        return juce::Result::fail ("HTTP " + juce::String (statusCode) + " at " + url.toString (true) + responseDetail + requestDetail);
    }

    return juce::Result::ok();
}

bool selectTrackByIndex (const juce::var& song, int trackIndex, juce::var& outTrack)
{
    const auto* songObj = song.getDynamicObject();
    if (songObj == nullptr)
        return false;

    const auto tracks = songObj->getProperty ("tracks");
    const auto* tracksObj = tracks.getDynamicObject();
    if (tracksObj == nullptr)
        return false;

    const auto trackOrder = songObj->getProperty ("trackOrder");
    if (const auto* orderArray = trackOrder.getArray(); orderArray != nullptr
        && juce::isPositiveAndBelow (trackIndex, orderArray->size()))
    {
        const auto key = orderArray->getReference (trackIndex).toString();
        const auto track = tracksObj->getProperty (key);
        if (track.isObject())
        {
            outTrack = track;
            return true;
        }
    }

    const auto& props = tracksObj->getProperties();
    if (! juce::isPositiveAndBelow (trackIndex, props.size()))
        return false;

    outTrack = props.getValueAt (trackIndex);
    return outTrack.isObject();
}

juce::Result splitScoreByRests (const juce::var& score,
                                int maxFrames,
                                juce::Array<juce::var>& outSegments)
{
    const auto* scoreObj = score.getDynamicObject();
    if (scoreObj == nullptr)
        return juce::Result::fail ("Invalid score JSON object");

    const auto notes = scoreObj->getProperty ("notes");
    const auto* noteArray = notes.getArray();
    if (noteArray == nullptr || noteArray->isEmpty())
        return juce::Result::fail ("Score notes are empty");

    juce::Array<juce::var> currentSegment;
    int frameSum = 0;
    juce::var carryRest;

    auto flushSegment = [&outSegments] (const juce::Array<juce::var>& segment)
    {
        auto* segmentObj = new juce::DynamicObject();
        segmentObj->setProperty ("notes", juce::var (segment));
        outSegments.add (juce::var (segmentObj));
    };

    for (const auto& note : *noteArray)
    {
        if (carryRest.isObject())
        {
            const auto* carryObj = carryRest.getDynamicObject();
            const auto carryLength = carryObj != nullptr ? static_cast<int> (carryObj->getProperty ("frame_length")) : 0;
            currentSegment.add (carryRest);
            frameSum += carryLength;
            carryRest = juce::var();
        }

        const auto* noteObj = note.getDynamicObject();
        if (noteObj == nullptr)
            continue;

        const auto frameLength = static_cast<int> (noteObj->getProperty ("frame_length"));
        const auto isRest = noteObj->getProperty ("notelen").toString() == "R";

        currentSegment.add (note);
        frameSum += frameLength;

        if (frameSum >= maxFrames && isRest && currentSegment.size() > 1)
        {
            const auto firstHalf = frameLength / 2;
            const auto secondHalf = frameLength - firstHalf;

            if (firstHalf > 0 && secondHalf > 0)
            {
                currentSegment.removeLast();
                frameSum -= frameLength;

                const auto rest1 = makeNote (firstHalf, juce::var(), "", "R");
                currentSegment.add (rest1);
                frameSum += firstHalf;

                flushSegment (currentSegment);
                currentSegment.clearQuick();
                frameSum = 0;

                carryRest = makeNote (secondHalf, juce::var(), "", "R");
            }
            else
            {
                flushSegment (currentSegment);
                currentSegment.clearQuick();
                frameSum = 0;
            }
        }
    }

    if (carryRest.isObject())
        currentSegment.add (carryRest);

    if (! currentSegment.isEmpty())
        flushSegment (currentSegment);

    if (outSegments.isEmpty())
        return juce::Result::fail ("Failed to split score");

    return juce::Result::ok();
}

juce::Result joinWaveFiles (const juce::Array<juce::File>& partFiles,
                            const juce::File& outputWavFile)
{
    if (partFiles.isEmpty())
        return juce::Result::fail ("No synthesized WAV parts");

    juce::AudioFormatManager manager;
    manager.registerBasicFormats();

    int outputChannels = 2;
    int64_t totalSamples = 0;
    double outputSampleRate = 0.0;

    struct ReadPart
    {
        juce::AudioBuffer<float> buffer;
    };

    juce::Array<ReadPart> parts;

    for (const auto& file : partFiles)
    {
        auto reader = std::unique_ptr<juce::AudioFormatReader> (manager.createReaderFor (file));
        if (reader == nullptr)
            return juce::Result::fail ("Failed to open synthesized WAV part: " + file.getFileName());

        if (outputSampleRate <= 0.0)
            outputSampleRate = reader->sampleRate;
        else if (! juce::approximatelyEqual (reader->sampleRate, outputSampleRate))
            return juce::Result::fail ("Mismatched sample rates in synthesized parts");

        outputChannels = juce::jmax (outputChannels, static_cast<int> (reader->numChannels));

        ReadPart part;
        part.buffer.setSize (static_cast<int> (reader->numChannels), static_cast<int> (reader->lengthInSamples));
        if (! reader->read (&part.buffer, 0, static_cast<int> (reader->lengthInSamples), 0, true, true))
            return juce::Result::fail ("Failed reading synthesized WAV part");

        totalSamples += reader->lengthInSamples;
        parts.add (std::move (part));
    }

    juce::AudioBuffer<float> joined (outputChannels, static_cast<int> (totalSamples));
    joined.clear();

    int writePos = 0;
    for (const auto& part : parts)
    {
        for (int channel = 0; channel < outputChannels; ++channel)
        {
            const auto sourceChannel = juce::jmin (channel, part.buffer.getNumChannels() - 1);
            joined.copyFrom (channel, writePos, part.buffer, sourceChannel, 0, part.buffer.getNumSamples());
        }

        writePos += part.buffer.getNumSamples();
    }

    outputWavFile.getParentDirectory().createDirectory();
    std::unique_ptr<juce::FileOutputStream> stream (outputWavFile.createOutputStream());
    if (stream == nullptr)
        return juce::Result::fail ("Failed creating output WAV file");

    juce::WavAudioFormat wavFormat;
    auto writer = std::unique_ptr<juce::AudioFormatWriter> (
        wavFormat.createWriterFor (stream.get(), outputSampleRate, static_cast<unsigned int> (outputChannels), 24, {}, 0));

    if (writer == nullptr)
        return juce::Result::fail ("Failed creating WAV writer");

    stream.release();

    if (! writer->writeFromAudioSampleBuffer (joined, 0, joined.getNumSamples()))
        return juce::Result::fail ("Failed writing merged WAV");

    return juce::Result::ok();
}
}

juce::Result fetchSingers (const juce::String& baseUrl,
                           juce::Array<SingerStyle>& outSingers,
                           bool includeAllStyles)
{
    outSingers.clearQuick();

    juce::String response;
    const auto getResult = getJson (juce::URL (baseUrl + "/singers"), 10000, response);
    if (getResult.failed())
        return getResult;

    const auto singersJson = juce::JSON::parse (response);
    const auto* singersArray = singersJson.getArray();
    if (singersArray == nullptr)
        return juce::Result::fail ("Invalid /singers JSON response");

    for (const auto& singer : *singersArray)
    {
        const auto* singerObj = singer.getDynamicObject();
        if (singerObj == nullptr)
            continue;

        const auto singerName = singerObj->getProperty ("name").toString();
        const auto styles = singerObj->getProperty ("styles");
        const auto* stylesArray = styles.getArray();
        if (stylesArray == nullptr)
            continue;

        if (! includeAllStyles)
        {
            if (stylesArray->isEmpty())
                continue;

            const auto* firstStyle = stylesArray->getFirst().getDynamicObject();
            if (firstStyle == nullptr)
                continue;

            SingerStyle singerStyle;
            singerStyle.singerName = singerName;
            singerStyle.styleName = firstStyle->getProperty ("name").toString();
            singerStyle.speakerId = static_cast<int> (firstStyle->getProperty ("id"));
            outSingers.add (std::move (singerStyle));
            continue;
        }

        for (const auto& style : *stylesArray)
        {
            const auto* styleObj = style.getDynamicObject();
            if (styleObj == nullptr)
                continue;

            SingerStyle singerStyle;
            singerStyle.singerName = singerName;
            singerStyle.styleName = styleObj->getProperty ("name").toString();
            singerStyle.speakerId = static_cast<int> (styleObj->getProperty ("id"));
            outSingers.add (std::move (singerStyle));
        }
    }

    if (outSingers.isEmpty())
        return juce::Result::fail (includeAllStyles
                                       ? "No singers found"
                                       : "No singers found with first-style filter");

    return juce::Result::ok();
}

int getKeyAdjustment (const juce::String& singerName,
                      const juce::String& styleName)
{
    const auto key = singerName + "|" + styleName;
    if (const auto it = getKeyAdjustmentMap().find (key); it != getKeyAdjustmentMap().end())
        return it->second;
    return 0;
}

juce::Result convertVvprojTrackToScoreJson (const juce::File& vvprojFile,
                                            const juce::File& outScoreJsonFile,
                                            int trackIndex)
{
    if (! vvprojFile.existsAsFile())
        return juce::Result::fail ("vvproj file not found");

    juce::var root = juce::JSON::parse (vvprojFile);
    auto* rootObj = root.getDynamicObject();
    if (rootObj == nullptr)
        return juce::Result::fail ("Failed parsing vvproj JSON");

    const auto song = rootObj->getProperty ("song");
    if (! song.isObject())
        return juce::Result::fail ("vvproj has no song object");

    juce::var track;
    if (! selectTrackByIndex (song, trackIndex, track))
        return juce::Result::fail ("Target track not found");

    auto* trackObj = track.getDynamicObject();
    if (trackObj == nullptr)
        return juce::Result::fail ("Invalid track object");

    const auto notesVar = trackObj->getProperty ("notes");
    const auto* notes = notesVar.getArray();
    if (notes == nullptr || notes->isEmpty())
        return juce::Result::fail ("Track notes are empty");

    const auto* songObj = song.getDynamicObject();
    const auto tpqn = songObj != nullptr ? static_cast<double> (songObj->getProperty ("tpqn")) : 480.0;
    const auto bpm = getFirstBpm (song);

    struct NoteData
    {
        int64_t position = 0;
        int64_t duration = 0;
        int midi = 60;
        juce::String lyric;
    };

    juce::Array<NoteData> noteList;
    noteList.ensureStorageAllocated (notes->size());

    for (const auto& note : *notes)
    {
        const auto* noteObj = note.getDynamicObject();
        if (noteObj == nullptr)
            continue;

        NoteData data;
        data.position = static_cast<int64_t> (noteObj->getProperty ("position"));
        data.duration = static_cast<int64_t> (noteObj->getProperty ("duration"));
        data.midi = static_cast<int> (noteObj->getProperty ("noteNumber"));
        data.lyric = noteObj->getProperty ("lyric").toString();

        noteList.add (data);
    }

    if (noteList.isEmpty())
        return juce::Result::fail ("Track has no valid notes");

    std::sort (noteList.begin(), noteList.end(), [] (const NoteData& a, const NoteData& b)
    {
        return a.position < b.position;
    });

    juce::Array<juce::var> outNotes;
    outNotes.add (makeNote (2, juce::var(), "", "R"));

    int64_t previousEnd = 0;
    double carry = 0.0;

    for (const auto& note : noteList)
    {
        const auto gap = note.position - previousEnd;
        if (gap > 0)
            outNotes.add (makeNote (calcFrameLen (gap, bpm, tpqn, carry), juce::var(), "", "R"));

        const auto midi = juce::jlimit (0, 127, note.midi);
        outNotes.add (makeNote (calcFrameLen (note.duration, bpm, tpqn, carry), midi, note.lyric, midiToName (midi)));
        previousEnd = note.position + note.duration;
    }

    outNotes.add (makeNote (2, juce::var(), "", "R"));

    auto* scoreObj = new juce::DynamicObject();
    scoreObj->setProperty ("notes", juce::var (outNotes));

    if (! outScoreJsonFile.replaceWithText (juce::JSON::toString (juce::var (scoreObj), false)))
        return juce::Result::fail ("Failed writing score JSON");

    return juce::Result::ok();
}

juce::Result synthesizeTrackFromVvproj (const juce::File& vvprojFile,
                                        const juce::File& outputWavFile,
                                        const SynthesisOptions& options,
                                        std::function<void(float, const juce::String&)> progressCallback)
{
    reportProgress (progressCallback, 0.0f, "Start VOICEVOX pipeline");

    auto tempRoot = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("VVChorusPlayer");
    tempRoot.createDirectory();

    const auto scoreFile = tempRoot.getNonexistentChildFile ("voicevox_score_", ".json", false);
    const auto scoreResult = convertVvprojTrackToScoreJson (vvprojFile, scoreFile, options.trackIndex);
    if (scoreResult.failed())
        return scoreResult;

    reportProgress (progressCallback, 0.08f, "Converted vvproj track to score JSON");

    juce::Array<juce::File> tempPartFiles;

    juce::var score = juce::JSON::parse (scoreFile);
    juce::Array<juce::var> segments;

    const auto splitResult = splitScoreByRests (score, juce::jmax (64, options.maxFramesPerSegment), segments);
    if (splitResult.failed())
    {
        scoreFile.deleteFile();
        return splitResult;
    }

    reportProgress (progressCallback, 0.12f, "Split score into " + juce::String (segments.size()) + " segments");

    const auto keyShift = getKeyAdjustment (options.singerName, options.styleName) + options.keyShiftOffset;
    if (keyShift != 0)
    {
        for (auto& segment : segments)
        {
            if (keyShift < -12)
                transposeScoreVar (segment, -12);
            if (keyShift < -20)
                transposeScoreVar (segment, -12);
            transposeScoreVar (segment, -keyShift);
        }
    }

    const auto queryBase = options.baseUrl + "/sing_frame_audio_query?speaker=" + juce::String (options.querySpeakerId);
    const auto synthesisBase = options.baseUrl + "/frame_synthesis?speaker=" + juce::String (options.speakerId);

    for (int segmentIndex = 0; segmentIndex < segments.size(); ++segmentIndex)
    {
        const auto& segment = segments.getReference (segmentIndex);

        juce::String queryResponse;
        const auto queryResult = postJson (juce::URL (queryBase), juce::JSON::toString (segment, false), options.timeoutMs, queryResponse);
        if (queryResult.failed())
        {
            scoreFile.deleteFile();
            for (const auto& file : tempPartFiles)
                file.deleteFile();
            return queryResult;
        }

        auto queryJson = juce::JSON::parse (queryResponse);
        if (! queryJson.isObject())
        {
            scoreFile.deleteFile();
            for (const auto& file : tempPartFiles)
                file.deleteFile();
            return juce::Result::fail ("VOICEVOX query JSON parse failed");
        }

        if (auto* queryObj = queryJson.getDynamicObject())
        {
            if (keyShift != 0)
                transposeQueryVar (queryJson, keyShift);

            queryObj->setProperty ("outputSamplingRate", options.outputSampleRate);
            queryObj->setProperty ("outputStereo", true);
        }

        juce::MemoryBlock wavData;
        const auto synthResult = postBinary (juce::URL (synthesisBase), juce::JSON::toString (queryJson, false), options.timeoutMs, wavData);
        if (synthResult.failed())
        {
            scoreFile.deleteFile();
            for (const auto& file : tempPartFiles)
                file.deleteFile();
            return synthResult;
        }

        const auto partFile = tempRoot.getNonexistentChildFile ("voicevox_part_", ".wav", false);
        if (! partFile.replaceWithData (wavData.getData(), wavData.getSize()))
        {
            scoreFile.deleteFile();
            for (const auto& file : tempPartFiles)
                file.deleteFile();
            return juce::Result::fail ("Failed writing synthesized WAV part");
        }

        tempPartFiles.add (partFile);

        const auto segmentProgress = 0.12f + 0.76f * (static_cast<float> (segmentIndex + 1) / static_cast<float> (juce::jmax (1, segments.size())));
        reportProgress (progressCallback,
                        segmentProgress,
                        "Synthesized segment " + juce::String (segmentIndex + 1) + "/" + juce::String (segments.size()));
    }

    reportProgress (progressCallback, 0.92f, "Merging synthesized segments");

    const auto joinResult = joinWaveFiles (tempPartFiles, outputWavFile);

    scoreFile.deleteFile();
    for (const auto& file : tempPartFiles)
        file.deleteFile();

    if (joinResult.wasOk())
        reportProgress (progressCallback, 1.0f, "VOICEVOX synthesis finished");

    return joinResult;
}
}
