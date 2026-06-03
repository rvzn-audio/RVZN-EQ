#include "PluginProcessor.h"
#include "PluginEditor.h"

static juce::String bandParamID (int b, const char* name)
{
    return juce::String ("band") + juce::String (b) + "_" + name;
}

//==============================================================================
// PresetManager
//==============================================================================
static juce::String sanitizePresetName (const juce::String& raw)
{
    auto name = raw.trim();
    // Block path traversal and absolute paths
    name = name.replace ("..", "").replace ("/", "").replace ("\\", "");
    // Strip any other illegal filesystem characters
    name = juce::File::createLegalFileName (name);
    return name;
}

PresetManager::PresetManager (juce::AudioProcessorValueTreeState& a) : apvts (a)
{
    presetDir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                    .getChildFile ("RVZNEQ").getChildFile ("Presets");
    presetDir.createDirectory();
}

juce::StringArray PresetManager::allPresetNames() const
{
    juce::StringArray names;
    for (auto& f : presetDir.findChildFiles (juce::File::findFiles, false, "*.xml"))
        names.add (f.getFileNameWithoutExtension());
    names.sort (false);
    return names;
}

void PresetManager::savePreset (const juce::String& rawName)
{
    auto name = sanitizePresetName (rawName);
    if (name.isEmpty()) return;
    if (auto xml = apvts.copyState().createXml())
        xml->writeTo (presetDir.getChildFile (name + ".xml"));
    currentName = name;
}

bool PresetManager::loadPreset (const juce::String& rawName)
{
    auto name = sanitizePresetName (rawName);
    if (name.isEmpty()) return false;
    auto f = presetDir.getChildFile (name + ".xml");
    if (!f.existsAsFile()) return false;
    if (auto xml = juce::XmlDocument::parse (f))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
    currentName = name;
    return true;
}

void PresetManager::deletePreset (const juce::String& rawName)
{
    auto name = sanitizePresetName (rawName);
    if (name.isEmpty()) return;
    presetDir.getChildFile (name + ".xml").deleteFile();
    if (currentName == name) currentName = {};
}

void PresetManager::setAsDefault (const juce::String& rawName)
{
    auto name = sanitizePresetName (rawName);
    if (name.isEmpty()) return;
    presetDir.getParentDirectory().getChildFile ("default.txt").replaceWithText (name);
}

juce::String PresetManager::defaultPresetName() const
{
    auto f = presetDir.getParentDirectory().getChildFile ("default.txt");
    return f.existsAsFile() ? f.loadFileAsString().trim() : juce::String{};
}

juce::AudioProcessorValueTreeState::ParameterLayout RVZNEQAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (int b = 0; b < NUM_BANDS; ++b)
    {
        auto freqDefault = [b]() -> float {
            float defaults[] = { 80.f, 200.f, 500.f, 1000.f, 2500.f, 5000.f, 10000.f, 16000.f };
            return defaults[b];
        }();

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            bandParamID (b, "freq"), "Band " + juce::String (b + 1) + " Freq",
            juce::NormalisableRange<float> (20.f, 20000.f, 0.01f, 0.3f), freqDefault));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            bandParamID (b, "gain"), "Band " + juce::String (b + 1) + " Gain",
            juce::NormalisableRange<float> (-30.f, 30.f, 0.01f), 0.f));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            bandParamID (b, "q"), "Band " + juce::String (b + 1) + " Q",
            juce::NormalisableRange<float> (0.1f, 18.f, 0.01f, 0.5f), 1.f));

        layout.add (std::make_unique<juce::AudioParameterInt> (
            bandParamID (b, "type"), "Band " + juce::String (b + 1) + " Type",
            0, (int)NumFilterTypes - 1, (int)Bell));

        layout.add (std::make_unique<juce::AudioParameterInt> (
            bandParamID (b, "slope"), "Band " + juce::String (b + 1) + " Slope",
            0, 3, 1));  // 0=6dB, 1=12dB, 2=18dB, 3=24dB (index maps directly to combo)

        layout.add (std::make_unique<juce::AudioParameterBool> (
            bandParamID (b, "enabled"), "Band " + juce::String (b + 1) + " Enabled", false));

        layout.add (std::make_unique<juce::AudioParameterInt> (
            bandParamID (b, "mode"), "Band " + juce::String (b + 1) + " Mode",
            0, (int)NumProcessModes - 1, (int)Stereo));
    }

    return layout;
}

float RVZNEQAudioProcessor::stageQ (int /*order*/, int /*stage*/, float userQ) noexcept
{
    // Use the same Q for every cascaded stage.
    // Butterworth pole-Q distribution causes a high-Q stage (e.g. 1.85 for 4th-order)
    // that produces a stopband resonance hump near Nyquist.
    // Identical Q per stage stays monotonic in the stopband.
    return juce::jlimit (0.1f, 18.f, userQ);
}

float RVZNEQAudioProcessor::effectiveBellQ (float q, float freq, double sampleRate) noexcept
{
    float nyquist = (float)(sampleRate * 0.5);
    // Smoothly taper Q above 70% of Nyquist to prevent mirror-peak aliasing artifacts
    float t = juce::jlimit (0.f, 1.f, (freq / nyquist - 0.7f) / 0.3f);
    return q * (1.f - t) + 0.707f * t;
}

RVZNEQAudioProcessor::RVZNEQAudioProcessor()
    : AudioProcessor (BusesProperties()
                      .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts   (*this, &undoManager, "RVZNEQ", createParameterLayout()),
      presets (apvts)
{
    prevType.fill  (-1);
    prevSlope.fill (-1);
    prevMode.fill  (-1);
    for (int b = 0; b < NUM_BANDS; ++b)
    {
        apvts.addParameterListener (bandParamID (b, "freq"),    this);
        apvts.addParameterListener (bandParamID (b, "gain"),    this);
        apvts.addParameterListener (bandParamID (b, "q"),       this);
        apvts.addParameterListener (bandParamID (b, "type"),    this);
        apvts.addParameterListener (bandParamID (b, "slope"),   this);
        apvts.addParameterListener (bandParamID (b, "enabled"), this);
        apvts.addParameterListener (bandParamID (b, "mode"),    this);
    }
}

RVZNEQAudioProcessor::~RVZNEQAudioProcessor()
{
    for (int b = 0; b < NUM_BANDS; ++b)
    {
        apvts.removeParameterListener (bandParamID (b, "freq"),    this);
        apvts.removeParameterListener (bandParamID (b, "gain"),    this);
        apvts.removeParameterListener (bandParamID (b, "q"),       this);
        apvts.removeParameterListener (bandParamID (b, "type"),    this);
        apvts.removeParameterListener (bandParamID (b, "slope"),   this);
        apvts.removeParameterListener (bandParamID (b, "enabled"), this);
        apvts.removeParameterListener (bandParamID (b, "mode"),    this);
    }
}

EQBandParams RVZNEQAudioProcessor::getBandParams (int b) const
{
    EQBandParams p;
    p.freq    = apvts.getRawParameterValue (bandParamID (b, "freq"))->load();
    p.gainDb  = apvts.getRawParameterValue (bandParamID (b, "gain"))->load();
    p.q       = apvts.getRawParameterValue (bandParamID (b, "q"))->load();
    p.type    = (int) apvts.getRawParameterValue (bandParamID (b, "type"))->load();
    p.slope   = (int) apvts.getRawParameterValue (bandParamID (b, "slope"))->load() + 1;
    p.enabled = apvts.getRawParameterValue (bandParamID (b, "enabled"))->load() > 0.5f;
    p.mode = (int) apvts.getRawParameterValue (bandParamID (b, "mode"))->load();
    return p;
}

void RVZNEQAudioProcessor::parameterChanged (const juce::String&, float)
{
    paramChangeCounter.fetch_add (1, std::memory_order_relaxed);
    triggerAsyncUpdate();   // build coefficients off the audio thread
}

void RVZNEQAudioProcessor::handleAsyncUpdate()
{
    // Runs on the message thread — allocations are OK here.
    // We build new coefficients into the pending slots under SpinLock so the
    // audio thread can swap them in atomically with try_lock.
    std::array<std::array<IIRCoeffs::Ptr, 4>, NUM_BANDS> newCoeffs;
    std::array<int, NUM_BANDS> newStages {};

    for (int b = 0; b < NUM_BANDS; ++b)
    {
        auto coeffs = buildBandCoefficients (getBandParams (b), currentSampleRate);
        newStages[b] = coeffs.size();
        for (int s = 0; s < 4; ++s)
            newCoeffs[b][s] = (s < coeffs.size()) ? coeffs[s] : nullptr;
    }

    {
        const juce::SpinLock::ScopedLockType lock (coefficientsLock);
        pendingCoeffs       = std::move (newCoeffs);
        pendingActiveStages = newStages;
        coefficientsPending.store (true, std::memory_order_release);
    }
}

void RVZNEQAudioProcessor::applyPendingCoefficients()
{
    // Called from audio thread. Uses try-lock so we never block.
    const juce::SpinLock::ScopedTryLockType lock (coefficientsLock);
    if (! lock.isLocked()) return;
    if (! coefficientsPending.exchange (false, std::memory_order_acquire)) return;

    for (int b = 0; b < NUM_BANDS; ++b)
    {
        auto p = getBandParams (b);

        // Detect topology / mode change to reset filter state and avoid pops
        bool topologyChanged = (p.type  != prevType[b]
                             || p.slope != prevSlope[b]);
        bool modeChanged     = (p.mode  != prevMode[b]);

        bands[b].activeStages = pendingActiveStages[b];

        for (int s = 0; s < 4; ++s)
        {
            if (pendingCoeffs[b][s] != nullptr)
            {
                bands[b].filtersL[s].coefficients = pendingCoeffs[b][s];
                bands[b].filtersR[s].coefficients = pendingCoeffs[b][s];
                bands[b].filtersM[s].coefficients = pendingCoeffs[b][s];
                bands[b].filtersS[s].coefficients = pendingCoeffs[b][s];
            }
        }

        if (topologyChanged)
        {
            for (auto& f : bands[b].filtersL) f.reset();
            for (auto& f : bands[b].filtersR) f.reset();
            for (auto& f : bands[b].filtersM) f.reset();
            for (auto& f : bands[b].filtersS) f.reset();
        }
        else if (modeChanged)
        {
            // M/S chains may have been idle in the previous mode → reset to
            // avoid a click from stale state when they become active again.
            for (auto& f : bands[b].filtersM) f.reset();
            for (auto& f : bands[b].filtersS) f.reset();
        }

        prevType[b]  = p.type;
        prevSlope[b] = p.slope;
        prevMode[b]  = p.mode;
    }
}

juce::ReferenceCountedArray<juce::dsp::IIR::Coefficients<float>>
RVZNEQAudioProcessor::buildBandCoefficients (const EQBandParams& p, double sr)
{
    using Coeffs = juce::dsp::IIR::Coefficients<float>;
    juce::ReferenceCountedArray<Coeffs> result;

    if (!p.enabled)
        return result;

    float f = juce::jlimit (20.f, 20000.f, p.freq);
    float q = juce::jlimit (0.1f, 18.f, p.q);
    float g = p.gainDb;
    int   order = p.slope;  // 1-4

    switch (p.type)
    {
        case Bell:
            result.add (Coeffs::makePeakFilter (sr, f, effectiveBellQ (q, f, sr),
                                                juce::Decibels::decibelsToGain (g)));
            break;
        case LowShelf:
            result.add (Coeffs::makeLowShelf  (sr, f, q, juce::Decibels::decibelsToGain (g)));
            break;
        case HighShelf:
            result.add (Coeffs::makeHighShelf (sr, f, q, juce::Decibels::decibelsToGain (g)));
            break;
        case LowPass:
            for (int i = 0; i < order; ++i)
                result.add (Coeffs::makeLowPass  (sr, f, effectiveBellQ (stageQ (order, i, q), f, sr)));
            break;
        case HighPass:
            for (int i = 0; i < order; ++i)
                result.add (Coeffs::makeHighPass (sr, f, effectiveBellQ (stageQ (order, i, q), f, sr)));
            break;
        case Notch:    result.add (Coeffs::makeNotch    (sr, f, q)); break;
        case BandPass: result.add (Coeffs::makeBandPass (sr, f, q)); break;
        default: break;
    }
    return result;
}

float RVZNEQAudioProcessor::bandPostGain (const EQBandParams& p) noexcept
{
    // Only low/high cut carry a passband gain (their coefficients are unity-gain).
    if (p.type == LowPass || p.type == HighPass)
        return juce::Decibels::decibelsToGain (p.gainDb);
    return 1.0f;
}

void RVZNEQAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate       = sampleRate;
    spec.maximumBlockSize = (juce::uint32) samplesPerBlock;
    spec.numChannels      = 1;

    for (auto& band : bands)
    {
        for (auto& f : band.filtersL) f.prepare (spec);
        for (auto& f : band.filtersR) f.prepare (spec);
        for (auto& f : band.filtersM) f.prepare (spec);
        for (auto& f : band.filtersS) f.prepare (spec);
    }

    // Build initial coefficients synchronously so the first block has them
    handleAsyncUpdate();
    applyPendingCoefficients();

    msWorkBuf.setSize (2, samplesPerBlock);
    msWorkBuf.clear();
    fftRingBuffer.fill (0.f);
    fftWritePos   = 0;
    fftHopCounter = 0;
    fftWorkBuf.fill (0.f);
}

void RVZNEQAudioProcessor::releaseResources()
{
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool RVZNEQAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
    return true;
}
#endif

void RVZNEQAudioProcessor::pushSamplesToFFT (const float* data, int numSamples)
{
    for (int i = 0; i < numSamples; ++i)
    {
        fftRingBuffer[(size_t)fftWritePos] = data[i];
        fftWritePos = (fftWritePos + 1) % FFT_SIZE;
        if (++fftHopCounter >= FFT_HOP)
        {
            fftHopCounter = 0;
            calcSpectrum();
        }
    }
}

void RVZNEQAudioProcessor::calcSpectrum()
{
    // Copy ring buffer in chronological order into work buffer
    for (int i = 0; i < FFT_SIZE; ++i)
        fftWorkBuf[(size_t)i] = fftRingBuffer[(size_t)((fftWritePos + i) % FFT_SIZE)];

    window.multiplyWithWindowingTable (fftWorkBuf.data(), (size_t)FFT_SIZE);
    fft.performFrequencyOnlyForwardTransform (fftWorkBuf.data());

    float maxVal = 1e-6f;
    for (int i = 0; i < FFT_SIZE / 2; ++i)
        maxVal = std::max (maxVal, fftWorkBuf[(size_t)i]);

    // dBFS-ish scaling: divide by FFT_SIZE/2 then compensate Hann coherent gain (~0.5 → x2)
    const float scale = 4.f / (float) FFT_SIZE;
    (void) maxVal;  // not used in absolute-dBFS mode

    {
        const juce::SpinLock::ScopedLockType lock (spectrumLock);
        for (int i = 0; i < FFT_SIZE / 2; ++i)
            spectrumData[(size_t)i] = juce::Decibels::gainToDecibels (fftWorkBuf[(size_t)i] * scale, -100.f);
        newSpectrumAvailable.store (true);
    }
}

void RVZNEQAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                          juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    applyPendingCoefficients();

    auto totalIn  = getTotalNumInputChannels();
    auto totalOut = getTotalNumOutputChannels();

    for (int ch = totalIn; ch < totalOut; ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    float* L = buffer.getWritePointer (0);
    float* R = (totalIn > 1) ? buffer.getWritePointer (1) : nullptr;
    int    N = buffer.getNumSamples();

    // Measure input peak (ballistic: fast attack, slow release)
    {
        float pk = 0.f;
        for (int i = 0; i < N; ++i) pk = std::max (pk, std::abs (L[i]));
        if (R) for (int i = 0; i < N; ++i) pk = std::max (pk, std::abs (R[i]));
        float db  = juce::Decibels::gainToDecibels (pk + 1e-9f, -100.f);
        float cur = inputLevelDb.load (std::memory_order_relaxed);
        inputLevelDb.store (db > cur ? db : cur + (db - cur) * 0.05f, std::memory_order_relaxed);
    }

    if (!globalBypassed.load (std::memory_order_relaxed))
    {
        if (R != nullptr)
        {
            // Stereo: encode L/R → M/S, process per-band mode, decode
            float* M = msWorkBuf.getWritePointer (0);
            float* S = msWorkBuf.getWritePointer (1);

            for (int i = 0; i < N; ++i)
            {
                M[i] = (L[i] + R[i]) * 0.5f;
                S[i] = (L[i] - R[i]) * 0.5f;
            }

            for (int b = 0; b < NUM_BANDS; ++b)
            {
                auto  p      = getBandParams (b);
                auto& band   = bands[b];
                int   stages = band.activeStages;
                if (!p.enabled || stages == 0) continue;

                const float bg = bandPostGain (p);

                switch (p.mode)
                {
                    case Stereo:
                        for (int s = 0; s < stages && s < 4; ++s)
                        {
                            for (int i = 0; i < N; ++i) M[i] = band.filtersM[s].processSample (M[i]);
                            for (int i = 0; i < N; ++i) S[i] = band.filtersS[s].processSample (S[i]);
                        }
                        if (bg != 1.0f)
                            for (int i = 0; i < N; ++i) { M[i] *= bg; S[i] *= bg; }
                        break;
                    case Mid:
                        for (int s = 0; s < stages && s < 4; ++s)
                            for (int i = 0; i < N; ++i) M[i] = band.filtersM[s].processSample (M[i]);
                        if (bg != 1.0f)
                            for (int i = 0; i < N; ++i) M[i] *= bg;
                        break;
                    case Side:
                        for (int s = 0; s < stages && s < 4; ++s)
                            for (int i = 0; i < N; ++i) S[i] = band.filtersS[s].processSample (S[i]);
                        if (bg != 1.0f)
                            for (int i = 0; i < N; ++i) S[i] *= bg;
                        break;
                    default: break;
                }
            }

            for (int i = 0; i < N; ++i)
            {
                L[i] = M[i] + S[i];
                R[i] = M[i] - S[i];
            }
        }
        else
        {
            // Mono: ignore mode, apply all bands to L
            for (int b = 0; b < NUM_BANDS; ++b)
            {
                auto  p      = getBandParams (b);
                auto& band   = bands[b];
                int   stages = band.activeStages;
                if (!p.enabled || stages == 0) continue;

                for (int s = 0; s < stages && s < 4; ++s)
                    for (int i = 0; i < N; ++i) L[i] = band.filtersL[s].processSample (L[i]);

                const float bg = bandPostGain (p);
                if (bg != 1.0f)
                    for (int i = 0; i < N; ++i) L[i] *= bg;
            }
        }
    }

    // Measure output peak
    {
        float pk = 0.f;
        for (int i = 0; i < N; ++i) pk = std::max (pk, std::abs (L[i]));
        if (R) for (int i = 0; i < N; ++i) pk = std::max (pk, std::abs (R[i]));
        float db  = juce::Decibels::gainToDecibels (pk + 1e-9f, -100.f);
        float cur = outputLevelDb.load (std::memory_order_relaxed);
        outputLevelDb.store (db > cur ? db : cur + (db - cur) * 0.05f, std::memory_order_relaxed);
    }

    pushSamplesToFFT (L, N);
}

juce::AudioProcessorEditor* RVZNEQAudioProcessor::createEditor()
{
    return new RVZNEQAudioProcessorEditor (*this);
}

const juce::String RVZNEQAudioProcessor::getName() const { return JucePlugin_Name; }
bool RVZNEQAudioProcessor::acceptsMidi()  const { return false; }
bool RVZNEQAudioProcessor::producesMidi() const { return false; }
bool RVZNEQAudioProcessor::isMidiEffect() const { return false; }

static constexpr int kPluginStateVersion = 1;

void RVZNEQAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("pluginVersion", kPluginStateVersion, nullptr);
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void RVZNEQAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml == nullptr || !xml->hasTagName (apvts.state.getType())) return;

    auto tree = juce::ValueTree::fromXml (*xml);
    int storedVersion = (int) tree.getProperty ("pluginVersion", 0);
    if (storedVersion > kPluginStateVersion)
    {
        // State was written by a newer plugin version — refuse rather than load broken data
        return;
    }
    apvts.replaceState (tree);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new RVZNEQAudioProcessor();
}
