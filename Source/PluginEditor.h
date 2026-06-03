#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "LookAndFeel/RvznColours.h"
#include "LookAndFeel/RvznLookAndFeel.h"
#include "LookAndFeel/RvznSettings.h"
#include "UI/MeterComponent.h"
#include "License/LicenseConfig.h"
#include "License/LicenseManager.h"
#include "License/LicenseGate.h"

//==============================================================================
class BandPopupPanel;

class EQCurveComponent : public juce::Component, public juce::Timer
{
public:
    EQCurveComponent (RVZNEQAudioProcessor& p);
    ~EQCurveComponent() override;

    void paint             (juce::Graphics& g) override;
    void resized           () override;
    void mouseDown         (const juce::MouseEvent& e) override;
    void mouseDrag         (const juce::MouseEvent& e) override;
    void mouseUp           (const juce::MouseEvent& e) override;
    void mouseMove         (const juce::MouseEvent& e) override;
    void mouseExit         (const juce::MouseEvent& e) override;
    void mouseWheelMove    (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override;
    void mouseDoubleClick  (const juce::MouseEvent& e) override;
    void timerCallback     () override;

    int  getSelectedBand() const { return selectedBand; }
    void updatePopupPosition();

    bool analyzerEnabled = true;

private:
    RVZNEQAudioProcessor& processor;

    float getXForFreq (float f) const;
    float getFreqForX (float x) const;
    float getYForGain (float dB) const;
    float getGainForY (float y) const;

    void        drawGrid          (juce::Graphics& g);
    void        drawSpectrum      (juce::Graphics& g);
    void        drawCurve         (juce::Graphics& g);
    void        drawNodes         (juce::Graphics& g);
    void        drawPerBandCurves (juce::Graphics& g);
    void        drawMSLegend      (juce::Graphics& g);
    void        drawLasso         (juce::Graphics& g);
    void        buildCurvePaths   ();            // fills cached paths + arrays

    float       getCurveYAtX  (float x) const;

    int  findHitNode       (float x, float y) const;
    int  findFirstFreeBand () const;
    void addBandAt         (float freq, float gainDb, FilterType type = Bell);
    void removeBand        (int band);
    void showPopupForBand  (int band);
    void dismissPopup      ();

    bool isBandSelected    (int b) const;
    void selectOnly        (int b);
    void clearSelection    ();

    int   selectedBand  = -1;          // primary band — popup target / Q ring
    int   draggingBand  = -1;
    bool  isDragging    = false;
    int   hoveredBand   = -1;
    juce::Point<float> dragStartMouse;
    float dragStartFreq = 0.f;
    float dragStartGain = 0.f;

    // Multi-selection (filled by lasso)
    std::vector<int> selectedBands;

    // Multi-drag — captured at mouseDown so the whole group moves with one delta
    struct DragRef { int band; float startFreq; float startGain; };
    std::vector<DragRef> multiDragRefs;

    // Lasso selection rectangle (drag from empty area)
    bool isLassoing = false;
    juce::Point<float> lassoStart, lassoCurrent;

    // Rendering caches — Mid and Side are tracked separately so the curve can
    // visually show which channels each band is processing.
    juce::Path  cachedMidPath, cachedSidePath;
    bool        cachedHasMS    = false;   // true when any band uses Mid or Side mode
    bool        curveDirty     = true;
    int         lastParamCount = -1;
    juce::Image gridImage;
    bool        gridDirty      = true;

    // Cached curve sample positions (for getCurveYAtX — uses Mid as reference)
    std::vector<float> cachedCurveXs, cachedCurveYs, cachedCurveSideYs;

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

    void paint        (juce::Graphics& g) override;
    void resized      () override;
    void timerCallback () override;

    void mouseDown    (const juce::MouseEvent& e) override;
    void mouseDrag    (const juce::MouseEvent& e) override;
    void mouseUp      (const juce::MouseEvent& e) override;

    int currentBand    = -1;
    int lastParamCount = -1;
    std::function<void(int)> onRemoveBand;   // delete button — removes the band
    std::function<void()>    onClose;        // × button — just hides the panel

    static constexpr int kW = 238;
    static constexpr int kHeaderH = 28;

    int currentHeight () const;    // public so EQCurveComponent can query it

    bool userMoved = false;   // true once the user drags the panel — disables auto-tracking

private:
    RVZNEQAudioProcessor& processor;

    RvznLookAndFeel laf;

    juce::Label       bandLabel;
    juce::TextButton  closeBtn;
    juce::TextButton  deleteBtn;

    // Value sliders (Rotary)
    juce::Slider freqSlider, gainSlider, qSlider;
    juce::Label  freqLabel,  gainLabel,  qLabel;

    // Filter type mini-buttons
    std::array<juce::TextButton, 7> typeBtn;

    // Slope mini-buttons (6 12 18 24)
    std::array<juce::TextButton, 4> slopeBtn;
    juce::Label slopeLabel;

    // M/S mode buttons
    std::array<juce::TextButton, 3> modeBtn;
    juce::Label modeLabel;

    // Bypass toggle
    RvznToggleButton bypassToggle;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> freqAttach, gainAttach, qAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttach;

    bool showSlopeRow = false;
    void rebuildAttachments ();
    void updateButtonStates ();

    // Drag state (header grab)
    bool isDraggingPanel = false;
    juce::Point<int> dragStartPanelPos;
    juce::Point<int> dragStartMousePos;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandPopupPanel)
};

//==============================================================================
class PresetModal;
class SettingsModal;

class RVZNEQAudioProcessorEditor : public juce::AudioProcessorEditor,
                                    private juce::Timer,
                                    private juce::ChangeListener
{
public:
    RVZNEQAudioProcessorEditor (RVZNEQAudioProcessor&);
    ~RVZNEQAudioProcessorEditor() override;

    void paint   (juce::Graphics&) override;
    void resized () override;

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    RVZNEQAudioProcessor& audioProcessor;
    EQCurveComponent      curveComp;
    RvznLookAndFeel       laf;

    // ---- Preset button + modal ----
    juce::TextButton presetsBtn { "PRESETS" };
    std::unique_ptr<PresetModal> presetModal;

    // ---- Settings button + modal ----
    juce::TextButton settingsBtn;
    std::unique_ptr<SettingsModal> settingsModal;

    // ---- Header buttons ----
    juce::TextButton analyzerBtn { "ANALYZER" };
    juce::TextButton bypassBtn   { "BYPASS"   };

    // ---- Footer meters ----
    MeterComponent inMeter, outMeter;
    juce::Label    inLabel, outLabel, latencyLabel, phaseLabel;

    // ---- Tooltip ----
    juce::TooltipWindow tooltipWindow { this, 700 };

    // License gate overlays the entire editor until the plugin is verified.
    std::unique_ptr<rvzn::license::LicenseManager> licenseManager;
    std::unique_ptr<rvzn::license::LicenseGate>    licenseGate;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RVZNEQAudioProcessorEditor)
};
