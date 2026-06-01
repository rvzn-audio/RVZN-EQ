#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

namespace RVZNColours
{
    static const juce::Colour background  { 0xFF0D0D12 };
    static const juce::Colour grid        { 0xFF1C1C26 };
    static const juce::Colour gridText    { 0xFF3A3A52 };
    static const juce::Colour curve       { 0xFFE8E8FF };
    static const juce::Colour curveFill   { 0x14E8E8FF };
    static const juce::Colour spectrum    { 0xFF111820 };
    static const juce::Colour spectrumLine{ 0xFF1E2E46 };
    static const juce::Colour panelBg     { 0xFF12121A };
    static const juce::Colour controlBg   { 0xFF1A1A26 };
    static const juce::Colour labelText   { 0xFF484860 };
    static const juce::Colour valueText   { 0xFFB8B8D8 };
    static const juce::Colour accent      { 0xFF3A7EFF };

    static juce::Colour bandColour (int b)
    {
        static const juce::Colour cols[] = {
            juce::Colour (0xFF00D4FF), juce::Colour (0xFFFF8C00),
            juce::Colour (0xFFC87CFF), juce::Colour (0xFF00E68A),
            juce::Colour (0xFFFF4455), juce::Colour (0xFFFFD700),
            juce::Colour (0xFFFF69B4), juce::Colour (0xFF4488FF),
        };
        return cols[b % 8];
    }
}

//==============================================================================
class BandPopupPanel;

class EQCurveComponent : public juce::Component, public juce::Timer
{
public:
    EQCurveComponent (RVZNEQAudioProcessor& p);
    ~EQCurveComponent() override;

    void paint        (juce::Graphics& g) override;
    void resized      () override;
    void mouseDown    (const juce::MouseEvent& e) override;
    void mouseDrag    (const juce::MouseEvent& e) override;
    void mouseUp      (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void timerCallback () override;

    int  getSelectedBand() const { return selectedBand; }

    void updatePopupPosition();

private:
    RVZNEQAudioProcessor& processor;

    float getXForFreq (float f) const;
    float getFreqForX (float x) const;
    float getYForGain (float dB) const;
    float getGainForY (float y) const;

    void  drawGrid    (juce::Graphics& g);
    void  drawSpectrum (juce::Graphics& g);
    void  drawCurve   (juce::Graphics& g);
    void  drawNodes   (juce::Graphics& g);
    juce::Path buildCurvePath() const;

    double computeBandMagnitude  (int band, double freq) const;
    double computeTotalMagnitudeDb (double freq) const;

    int  findHitNode       (float x, float y) const;
    int  findFirstFreeBand () const;
    void addBandAt         (float freq, float gainDb);
    void removeBand        (int band);
    void showPopupForBand  (int band);
    void dismissPopup      ();

    int selectedBand  = -1;
    int draggingBand  = -1;
    bool isDragging   = false;
    juce::Point<float> dragStartMouse;
    float dragStartFreq = 0.f;
    float dragStartGain = 0.f;

    std::array<float, FFT_SIZE / 2> displaySpectrum {};
    std::unique_ptr<BandPopupPanel> popup;

    static constexpr float minFreq = 20.f;
    static constexpr float maxFreq = 20000.f;
    static constexpr float minGain = -30.f;
    static constexpr float maxGain = 30.f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EQCurveComponent)
};

//==============================================================================
class BandPopupPanel : public juce::Component, public juce::Timer
{
public:
    BandPopupPanel (RVZNEQAudioProcessor& p);
    ~BandPopupPanel() override;

    void showForBand (int band, juce::Point<int> nodePosInParent);
    void hidePanel   ();

    void paint   (juce::Graphics& g) override;
    void resized () override;
    void timerCallback () override;

    int currentBand = -1;
    std::function<void(int)> onRemoveBand;

    static constexpr int kW = 238;

private:
    RVZNEQAudioProcessor& processor;

    juce::Label   bandLabel;
    juce::TextButton closeBtn;

    // Value sliders (LinearBar)
    juce::Slider freqSlider, gainSlider, qSlider;
    juce::Label  freqLabel,  gainLabel,  qLabel;

    // Filter type mini-buttons (Bell LS HS LP HP Notch BP)
    std::array<juce::TextButton, 7> typeBtn;

    // Slope mini-buttons (6 12 18 24)
    std::array<juce::TextButton, 4> slopeBtn;
    juce::Label slopeLabel;

    // Bypass toggle
    juce::ToggleButton bypassToggle;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> freqAttach, gainAttach, qAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttach;

    bool showSlopeRow = false;
    void rebuildAttachments ();
    void updateButtonStates ();
    int  currentHeight () const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandPopupPanel)
};

//==============================================================================
class RVZNEQAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    RVZNEQAudioProcessorEditor (RVZNEQAudioProcessor&);
    ~RVZNEQAudioProcessorEditor() override;

    void paint   (juce::Graphics&) override;
    void resized () override;

private:
    RVZNEQAudioProcessor& audioProcessor;

    juce::OpenGLContext openGLContext;
    EQCurveComponent    curveComp;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RVZNEQAudioProcessorEditor)
};
