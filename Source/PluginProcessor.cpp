#include "PluginProcessor.h"
#include "PluginEditor.h"

static juce::String bandParamID (int b, const char* name)
{
    return juce::String ("band") + juce::String (b) + "_" + name;
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
    }

    return layout;
}

RVZNEQAudioProcessor::RVZNEQAudioProcessor()
    : AudioProcessor (BusesProperties()
                      .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "RVZNEQ", createParameterLayout())
{
    for (int b = 0; b < NUM_BANDS; ++b)
    {
        apvts.addParameterListener (bandParamID (b, "freq"),    this);
        apvts.addParameterListener (bandParamID (b, "gain"),    this);
        apvts.addParameterListener (bandParamID (b, "q"),       this);
        apvts.addParameterListener (bandParamID (b, "type"),    this);
        apvts.addParameterListener (bandParamID (b, "slope"),   this);
        apvts.addParameterListener (bandParamID (b, "enabled"), this);
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
    return p;
}

void RVZNEQAudioProcessor::parameterChanged (const juce::String&, float)
{
    filtersDirty.store (true);
}

static juce::ReferenceCountedArray<juce::dsp::IIR::Coefficients<float>>
makeCoefficients (const EQBandParams& p, double sr)
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
            result.add (Coeffs::makePeakFilter (sr, f, q, juce::Decibels::decibelsToGain (g)));
            break;
        case LowShelf:
            result.add (Coeffs::makeLowShelf  (sr, f, q, juce::Decibels::decibelsToGain (g)));
            break;
        case HighShelf:
            result.add (Coeffs::makeHighShelf (sr, f, q, juce::Decibels::decibelsToGain (g)));
            break;
        case LowPass:
            for (int i = 0; i < order; ++i)
                result.add (Coeffs::makeLowPass  (sr, f, q));
            break;
        case HighPass:
            for (int i = 0; i < order; ++i)
                result.add (Coeffs::makeHighPass (sr, f, q));
            break;
        case Notch:
            result.add (Coeffs::makeNotch (sr, f, q));
            break;
        case BandPass:
            result.add (Coeffs::makeBandPass (sr, f, q));
            break;
        default: break;
    }
    return result;
}

void RVZNEQAudioProcessor::updateBandFilter (int b)
{
    auto p     = getBandParams (b);
    auto coeffs = makeCoefficients (p, currentSampleRate);
    int  stages = coeffs.size();

    bands[b].activeStages = stages;

    for (int s = 0; s < stages && s < 4; ++s)
    {
        *bands[b].filtersL[s].coefficients = *coeffs[s];
        *bands[b].filtersR[s].coefficients = *coeffs[s];
    }
}

void RVZNEQAudioProcessor::updateFilters()
{
    for (int b = 0; b < NUM_BANDS; ++b)
        updateBandFilter (b);
    filtersDirty.store (false);
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
    }

    updateFilters();
    fftFillIndex = 0;
    fftBuffer.fill (0.f);
}

void RVZNEQAudioProcessor::releaseResources() {}

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
        fftBuffer[(size_t)fftFillIndex] = data[i];
        fftFillIndex++;

        if (fftFillIndex >= FFT_SIZE)
        {
            fftFillIndex = 0;
            calcSpectrum();
        }
    }
}

void RVZNEQAudioProcessor::calcSpectrum()
{
    std::array<float, FFT_SIZE * 2> tmp {};
    for (int i = 0; i < FFT_SIZE; ++i)
        tmp[(size_t)i] = fftBuffer[(size_t)i];

    window.multiplyWithWindowingTable (tmp.data(), (size_t)FFT_SIZE);
    fft.performFrequencyOnlyForwardTransform (tmp.data());

    float maxVal = 1e-6f;
    for (int i = 0; i < FFT_SIZE / 2; ++i)
        maxVal = std::max (maxVal, tmp[(size_t)i]);

    for (int i = 0; i < FFT_SIZE / 2; ++i)
        spectrumData[(size_t)i] = juce::Decibels::gainToDecibels (tmp[(size_t)i] / maxVal, -100.f);

    newSpectrumAvailable.store (true);
}

void RVZNEQAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                          juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    if (filtersDirty.load())
        updateFilters();

    auto totalIn  = getTotalNumInputChannels();
    auto totalOut = getTotalNumOutputChannels();

    for (int ch = totalIn; ch < totalOut; ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    float* L = buffer.getWritePointer (0);
    float* R = (totalIn > 1) ? buffer.getWritePointer (1) : nullptr;
    int    N = buffer.getNumSamples();

    for (int b = 0; b < NUM_BANDS; ++b)
    {
        auto  p      = getBandParams (b);
        auto& band   = bands[b];
        int   stages = band.activeStages;

        if (!p.enabled || stages == 0) continue;

        for (int s = 0; s < stages && s < 4; ++s)
        {
            for (int i = 0; i < N; ++i) L[i] = band.filtersL[s].processSample (L[i]);
            if (R)
                for (int i = 0; i < N; ++i) R[i] = band.filtersR[s].processSample (R[i]);
        }
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

void RVZNEQAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void RVZNEQAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new RVZNEQAudioProcessor();
}
