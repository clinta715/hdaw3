#include "StretchRenderer.h"
#include <SoundTouch.h>
#include <algorithm>

namespace HDAW {

StretchRenderer::StretchRenderer() = default;

StretchRenderer::~StretchRenderer()
{
    cancel();
    if (renderThread.joinable())
        renderThread.join();
}

bool StretchRenderer::startRender(const juce::String& sourceFile, double timeRatio,
                                  double sampleRate, juce::AudioFormatManager& formatManager)
{
    if (active.load())
        return false;

    cancelFlag = false;
    active = true;

    if (renderThread.joinable())
        renderThread.join();

    renderThread = std::thread(&StretchRenderer::renderThreadFunc, this,
                               sourceFile, timeRatio, sampleRate, &formatManager);
    return true;
}

void StretchRenderer::cancel()
{
    cancelFlag = true;
}

void StretchRenderer::renderThreadFunc(juce::String sourceFile, double timeRatio,
                                       double sampleRate, juce::AudioFormatManager* formatManager)
{
    Result result;
    result.sampleRate = sampleRate;

    // Guard inputs.
    if (timeRatio <= 0.0 || timeRatio > 64.0 || sourceFile.isEmpty() || sampleRate <= 0.0)
    {
        active = false;
        if (onComplete) onComplete(result);
        return;
    }

    // --- 1. Decode the source into a float buffer ---------------------------
    std::unique_ptr<juce::AudioFormatReader> reader(
        formatManager->createReaderFor(juce::File(sourceFile)));
    if (reader == nullptr)
    {
        active = false;
        if (onComplete) onComplete(result);
        return;
    }

    const int srcChannels = juce::jmin(static_cast<int>(reader->numChannels), 2);
    const int64_t srcLen = reader->lengthInSamples;
    if (srcChannels <= 0 || srcLen <= 0)
    {
        active = false;
        if (onComplete) onComplete(result);
        return;
    }

    // SoundTouch wants SAMPLETYPE samples. This fork's STTypes.h forces
    // integer (short) samples; we declare our buffers in terms of
    // soundtouch::SAMPLETYPE so the code is correct regardless of which
    // path the header resolves to.
    using STSample = soundtouch::SAMPLETYPE;
    const int outChannels = 2;
    juce::AudioBuffer<float> srcFloat(outChannels, static_cast<int>(srcLen));
    {
        // JUCE's AudioFormatReader::read(AudioBuffer<float>*, startSample,
        // numSamples, start, useReaderLeft, useReaderRight) duplicates
        // mono sources across both destination channels automatically.
        reader->read(&srcFloat, 0, static_cast<int>(srcLen), 0, true, srcChannels == 1);
    }
    if (cancelFlag.load())
    {
        active = false;
        if (onComplete) onComplete(result);
        return;
    }
    if (onProgress) onProgress(0.2f);

    // --- 2. Configure SoundTouch for pitch-preserving stretch ---------------
    soundtouch::SoundTouch st;
    st.setSampleRate(static_cast<unsigned int>(sampleRate));
    st.setChannels(static_cast<unsigned int>(outChannels));
    // Tempo = 100% / timeRatio: a timeRatio of 2.0 (twice as long) means
    // tempo halved, so the content takes 2x wall-clock to play. Pitch is
    // untouched (the default), giving pitch-preserving time-stretch.
    st.setTempo(1.0 / timeRatio);
    st.clear();

    // --- 3. Push source through SoundTouch and collect output ---------------
    // The output size is approximate (SoundTouch adds some processing
    // latency). Pre-allocate optimistically to the expected size plus a
    // generous headroom, then accumulate into a growing buffer of
    // SAMPLETYPE samples (interleaved).
    const int64_t expectedOut = juce::jmax<int64_t>(
        1, static_cast<int64_t>(static_cast<double>(srcLen) * timeRatio) + 4096);
    std::vector<STSample> stretchedInterleaved;
    stretchedInterleaved.reserve(static_cast<size_t>(expectedOut) * outChannels);

    constexpr int kBlock = 4096;
    std::vector<STSample> procBuf(static_cast<size_t>(kBlock) * outChannels);
    std::vector<STSample> recvBuf(static_cast<size_t>(kBlock) * outChannels * 4);

    for (int64_t pos = 0; pos < srcLen; pos += kBlock)
    {
        if (cancelFlag.load())
        {
            active = false;
            if (onComplete) onComplete(result);
            return;
        }
        const int n = static_cast<int>(juce::jmin<int64_t>(kBlock, srcLen - pos));

        // De-interleave into SoundTouch's expected L,R,L,R... layout,
        // converting float→SAMPLETYPE (short when integer samples).
        for (int i = 0; i < n; ++i)
        {
            for (int ch = 0; ch < outChannels; ++ch)
            {
                float v = srcFloat.getSample(ch, static_cast<int>(pos) + i);
                procBuf[static_cast<size_t>(i) * outChannels + ch] =
                    static_cast<STSample>(juce::jlimit(-32768.0f, 32767.0f, v * 32768.0f));
            }
        }
        st.putSamples(procBuf.data(), static_cast<unsigned int>(n));

        // Drain whatever is ready so far.
        unsigned int got = 0;
        do
        {
            got = st.receiveSamples(recvBuf.data(), static_cast<unsigned int>(kBlock));
            if (got > 0)
            {
                const size_t prev = stretchedInterleaved.size();
                stretchedInterleaved.resize(prev + static_cast<size_t>(got) * outChannels);
                std::copy_n(recvBuf.data(), static_cast<size_t>(got) * outChannels,
                            stretchedInterleaved.data() + prev);
            }
        } while (got > 0);

        if (onProgress)
        {
            float prog = 0.2f + 0.6f * static_cast<float>(pos) / static_cast<float>(srcLen);
            onProgress(juce::jmin(0.8f, prog));
        }
    }

    // --- 4. Flush the processor's tail -------------------------------------
    st.flush();
    unsigned int got = 0;
    do
    {
        if (cancelFlag.load())
        {
            active = false;
            if (onComplete) onComplete(result);
            return;
        }
        got = st.receiveSamples(recvBuf.data(), static_cast<unsigned int>(kBlock));
        if (got > 0)
        {
            const size_t prev = stretchedInterleaved.size();
            stretchedInterleaved.resize(prev + static_cast<size_t>(got) * outChannels);
            std::copy_n(recvBuf.data(), static_cast<size_t>(got) * outChannels,
                        stretchedInterleaved.data() + prev);
        }
    } while (got > 0);

    const int64_t totalOut = stretchedInterleaved.empty()
        ? 0
        : static_cast<int64_t>(stretchedInterleaved.size()) / outChannels;

    if (totalOut <= 0)
    {
        active = false;
        if (onComplete) onComplete(result);
        return;
    }
    if (onProgress) onProgress(0.9f);

    // --- 5. De-interleave SAMPLETYPE → int PCM (ClipSourceProcessor format) -
    // ClipSourceProcessor divides by 32768.0f on read, so emit int values
    // in roughly [-32768, 32767]. SAMPLETYPE is already short when integer
    // samples are active, so the cast is identity; the jlimit is a defensive
    // no-op in that case and correct if the header is switched to float.
    result.data[0].malloc(static_cast<size_t>(totalOut));
    result.data[1].malloc(static_cast<size_t>(totalOut));
    for (int64_t i = 0; i < totalOut; ++i)
    {
        for (int ch = 0; ch < outChannels; ++ch)
        {
            STSample s = stretchedInterleaved[static_cast<size_t>(i) * outChannels + ch];
            result.data[ch][i] = static_cast<int>(
                juce::jlimit(-32768.0f, 32767.0f, static_cast<float>(s)));
        }
    }
    result.length = totalOut;
    result.channels = outChannels;
    result.success = true;

    if (onProgress) onProgress(1.0f);
    active = false;
    if (onComplete) onComplete(result);
}

} // namespace HDAW
