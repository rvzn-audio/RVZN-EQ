#pragma once
#include <JuceHeader.h>

static constexpr int NUM_BANDS = 8;
static constexpr int FFT_ORDER = 12;
static constexpr int FFT_SIZE  = 1 << FFT_ORDER;

enum FilterType { Bell=0, LowShelf, HighShelf, LowPass, HighPass, Notch, BandPass, NumFilterTypes };
enum ProcessMode { Stereo = 0, Mid, Side, NumProcessModes };

struct EQBandParams
{
    float freq    = 1000.f;
    float gainDb  = 0.f;
    float q       = 1.f;
    int   type    = Bell;
    int   slope   = 2;
    bool  enabled = true;
    int   mode    = 0;
};

//==============================================================================
class PresetManager
{
public:
    explicit PresetManager (juce::AudioProcessorValueTreeState& apvts);

    juce::StringArray allPresetNames () const;
    void savePreset   (const juce::String& name);
    bool loadPreset   (const juce::String& name);
    void deletePreset (const juce::String& name);
    void setAsDefault (const juce::String& name);
    juce::String defaultPresetName () const;

    juce::String currentName;

private:
    juce::AudioProcessorValueTreeState& apvts;
    juce::File presetDir;
};

//==============================================================================
class RVZNEQAudioProcessor : public juce::AudioProcessor,
                              public juce::AudioProcessorValueTreeState::Listener,
                              private juce::AsyncUpdater
{
public:
    RVZNEQAudioProcessor();
    ~RVZNEQAudioProcessor() override;

    void prepareToPlay   (double sampleRate, int samplesPerBlock) override;
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

    // Q taper (Bell near Nyquist) and Butterworth per-stage Q for LP/HP
    static float effectiveBellQ (float q, float freq, double sampleRate) noexcept;
    static float stageQ (int order, int stage, float userQ) noexcept;

    // Single source of truth for biquad coefficient construction.
    // Called from audio path (via async update) and UI (curve drawing).
    static juce::ReferenceCountedArray<juce::dsp::IIR::Coefficients<float>>
        buildBandCoefficients (const EQBandParams& p, double sampleRate);

    // Flat gain applied to a band's output after its filter stages. Used so a
    // low/high cut node can be dragged vertically: the gain shifts the passband
    // up/down. Bell/Shelf bake gain into their coefficients, so they get unity.
    static float bandPostGain (const EQBandParams& p) noexcept;

    juce::UndoManager undoManager;
    juce::AudioProcessorValueTreeState apvts;
    PresetManager presets;
    double currentSampleRate = 44100.0;

    // Spectrum (audio thread → UI thread, guarded by spectrumLock)
    std::array<float, FFT_SIZE / 2> spectrumData {};
    std::atomic<bool>  newSpectrumAvailable { false };
    std::atomic<int>   paramChangeCounter   { 0 };
    juce::SpinLock     spectrumLock;

    // Level meters (peak, dBFS, ballistic decay applied in audio thread)
    std::atomic<float> inputLevelDb  { -100.f };
    std::atomic<float> outputLevelDb { -100.f };

    // Global bypass
    std::atomic<bool>  globalBypassed { false };

    EQBandParams getBandParams (int band) const;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    void handleAsyncUpdate () override;
    void pushSamplesToFFT  (const float* data, int numSamples);
    void calcSpectrum      ();
    void applyPendingCoefficients ();

    using IIRCoeffs = juce::dsp::IIR::Coefficients<float>;
    using IIRFilter = juce::dsp::IIR::Filter<float>;

    struct Band
    {
        std::array<IIRFilter, 4> filtersL;
        std::array<IIRFilter, 4> filtersR;
        std::array<IIRFilter, 4> filtersM;
        std::array<IIRFilter, 4> filtersS;
        int  activeStages = 0;
    };

    std::array<Band, NUM_BANDS> bands;

    // Pending coefficients built on message thread, swapped under SpinLock
    juce::SpinLock coefficientsLock;
    std::array<std::array<IIRCoeffs::Ptr, 4>, NUM_BANDS> pendingCoeffs;
    std::array<int, NUM_BANDS>  pendingActiveStages {};
    std::atomic<bool> coefficientsPending { false };

    // Track topology per band so we can reset filter state on type/slope/mode change
    std::array<int, NUM_BANDS> prevType  {};
    std::array<int, NUM_BANDS> prevSlope {};
    std::array<int, NUM_BANDS> prevMode  {};

    juce::AudioBuffer<float> msWorkBuf;

    // FFT — ring buffer with hop for high-rate spectrum updates
    static constexpr int FFT_HOP = 512;
    juce::dsp::FFT fft { FFT_ORDER };
    juce::dsp::WindowingFunction<float> window {
        (size_t)FFT_SIZE, juce::dsp::WindowingFunction<float>::hann };
    std::array<float, FFT_SIZE>     fftRingBuffer {};
    std::array<float, FFT_SIZE * 2> fftWorkBuf    {};
    int fftWritePos   = 0;
    int fftHopCounter = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RVZNEQAudioProcessor)
};
