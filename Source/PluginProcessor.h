#pragma once
#include <JuceHeader.h>

static constexpr int NUM_BANDS = 8;
static constexpr int FFT_ORDER = 12;
static constexpr int FFT_SIZE  = 1 << FFT_ORDER;

enum FilterType { Bell=0, LowShelf, HighShelf, LowPass, HighPass, Notch, BandPass, NumFilterTypes };

struct EQBandParams
{
    float freq    = 1000.f;
    float gainDb  = 0.f;
    float q       = 1.f;
    int   type    = Bell;
    int   slope   = 2;   // order (1=6dB, 2=12dB, 3=18dB, 4=24dB)
    bool  enabled = true;
};

class RVZNEQAudioProcessor : public juce::AudioProcessor,
                              public juce::AudioProcessorValueTreeState::Listener
{
public:
    RVZNEQAudioProcessor();
    ~RVZNEQAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override;
    bool acceptsMidi()  const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override { return 0.0; }

    int  getNumPrograms() override { return 1; }
    int  getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    void parameterChanged (const juce::String& paramID, float newValue) override;

    juce::AudioProcessorValueTreeState apvts;
    double currentSampleRate = 44100.0;

    // Spectrum data (written by audio thread, read by UI thread – lock-free)
    std::array<float, FFT_SIZE / 2> spectrumData {};
    std::atomic<bool> newSpectrumAvailable { false };

    EQBandParams getBandParams (int band) const;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    void updateFilters();
    void updateBandFilter (int band);
    void pushSamplesToFFT (const float* data, int numSamples);
    void calcSpectrum();

    // Per-band: up to 4 cascaded biquad stages (for high slopes)
    using IIRCoeffs = juce::dsp::IIR::Coefficients<float>;
    using IIRFilter = juce::dsp::IIR::Filter<float>;

    struct Band
    {
        // Up to 4 stages for 24 dB/oct
        std::array<IIRFilter, 4> filtersL;
        std::array<IIRFilter, 4> filtersR;
        int activeStages = 1;
        bool dirty = true;
    };

    std::array<Band, NUM_BANDS> bands;
    std::atomic<bool> filtersDirty { true };

    // FFT
    juce::dsp::FFT fft { FFT_ORDER };
    juce::dsp::WindowingFunction<float> window {
        (size_t)FFT_SIZE, juce::dsp::WindowingFunction<float>::hann };
    std::array<float, FFT_SIZE * 2> fftBuffer {};
    int fftFillIndex = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RVZNEQAudioProcessor)
};
