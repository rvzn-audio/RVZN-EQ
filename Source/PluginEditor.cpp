#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace RVZNColours;

static juce::String bandParamID (int b, const char* name)
{
    return juce::String ("band") + juce::String (b) + "_" + name;
}

static const char* typeNames[] = { "Bell","LoS","HiS","LP","HP","Ntch","BP" };
static const char* slopeNames[] = { "6","12","18","24" };

//==============================================================================
// EQCurveComponent
//==============================================================================
EQCurveComponent::EQCurveComponent (RVZNEQAudioProcessor& p)
    : processor (p)
{
    setOpaque (true);
    displaySpectrum.fill (-100.f);

    popup = std::make_unique<BandPopupPanel> (p);
    popup->onRemoveBand = [this] (int b) { removeBand (b); };
    addChildComponent (*popup);

    startTimerHz (30);
}

EQCurveComponent::~EQCurveComponent()
{
    stopTimer();
}

void EQCurveComponent::timerCallback()
{
    if (processor.newSpectrumAvailable.load())
    {
        processor.newSpectrumAvailable.store (false);
        const float sm = 0.75f;
        for (int i = 0; i < FFT_SIZE / 2; ++i)
            displaySpectrum[(size_t)i] = displaySpectrum[(size_t)i] * sm
                                       + processor.spectrumData[(size_t)i] * (1.f - sm);
        repaint();
    }
}

// ---- coordinate helpers ----
float EQCurveComponent::getXForFreq (float freq) const
{
    return (float)getWidth() * std::log (freq / minFreq) / std::log (maxFreq / minFreq);
}
float EQCurveComponent::getFreqForX (float x) const
{
    return minFreq * std::pow (maxFreq / minFreq, x / (float)getWidth());
}
float EQCurveComponent::getYForGain (float dB) const
{
    return (float)getHeight() * (dB - maxGain) / (minGain - maxGain);
}
float EQCurveComponent::getGainForY (float y) const
{
    return maxGain + (minGain - maxGain) * y / (float)getHeight();
}

// ---- drawing ----
void EQCurveComponent::drawGrid (juce::Graphics& g)
{
    float w = (float)getWidth(), h = (float)getHeight();

    static const float fLines[] = {
        20,30,40,50,60,70,80,100,150,200,300,400,500,600,700,
        800,1000,1500,2000,3000,4000,5000,6000,7000,8000,10000,15000,20000 };

    g.setColour (grid);
    for (float f : fLines)
        g.drawVerticalLine (juce::roundToInt (getXForFreq (f)), 0.f, h);

    static const float dbLines[] = { -24,-18,-12,-6, 0, 6,12,18,24 };
    for (float db : dbLines)
    {
        float y = getYForGain (db);
        g.setColour (db == 0.f ? grid.brighter (0.6f) : grid);
        g.drawHorizontalLine (juce::roundToInt (y), 0.f, w);
    }

    // Labels
    static const float lblF[] = { 50,100,200,500,1000,2000,5000,10000,20000 };
    g.setFont (juce::FontOptions (9.f));
    g.setColour (gridText);
    for (float f : lblF)
    {
        float x = getXForFreq (f);
        juce::String lbl = f >= 1000.f ? juce::String (f / 1000.f, 0) + "k" : juce::String ((int)f);
        g.drawText (lbl, juce::roundToInt (x) - 14, (int)h - 14, 28, 12, juce::Justification::centred);
    }
    for (float db : dbLines)
    {
        float y = getYForGain (db);
        juce::String lbl = (db > 0 ? "+" : "") + juce::String ((int)db);
        g.drawText (lbl, 2, juce::roundToInt (y) - 6, 26, 12, juce::Justification::left);
    }
}

void EQCurveComponent::drawSpectrum (juce::Graphics& g)
{
    float w = (float)getWidth(), h = (float)getHeight();
    double sr = processor.currentSampleRate;
    juce::Path path;
    bool started = false;

    for (int x = 0; x < (int)w; ++x)
    {
        float freq = getFreqForX ((float)x);
        int bin = juce::roundToInt ((float)(freq / sr) * (float)(FFT_SIZE));
        bin = juce::jlimit (0, FFT_SIZE / 2 - 1, bin);

        float dB   = displaySpectrum[(size_t)bin];
        float yN   = 1.f - juce::jlimit (0.f, 1.f, (dB + 80.f) / 80.f);
        float y    = yN * h;

        if (!started) { path.startNewSubPath ((float)x, y); started = true; }
        else          { path.lineTo ((float)x, y); }
    }

    if (started)
    {
        path.lineTo (w, h); path.lineTo (0.f, h); path.closeSubPath();
        g.setColour (spectrum);
        g.fillPath (path);
        g.setColour (spectrumLine);
        g.strokePath (path, juce::PathStrokeType (0.8f));
    }
}

double EQCurveComponent::computeBandMagnitude (int band, double freq) const
{
    auto p = processor.getBandParams (band);
    if (!p.enabled) return 1.0;

    double sr = processor.currentSampleRate;
    float q = juce::jlimit (0.1f, 18.f, p.q);
    float f = juce::jlimit (20.f, 20000.f, p.freq);
    float g = p.gainDb;

    using Coeffs = juce::dsp::IIR::Coefficients<float>;
    juce::ReferenceCountedArray<Coeffs> arr;

    switch (p.type)
    {
        case Bell:      arr.add (Coeffs::makePeakFilter (sr, f, q, juce::Decibels::decibelsToGain (g))); break;
        case LowShelf:  arr.add (Coeffs::makeLowShelf   (sr, f, q, juce::Decibels::decibelsToGain (g))); break;
        case HighShelf: arr.add (Coeffs::makeHighShelf  (sr, f, q, juce::Decibels::decibelsToGain (g))); break;
        case LowPass:   for (int s=0;s<p.slope;++s) arr.add(Coeffs::makeLowPass  (sr,f,q)); break;
        case HighPass:  for (int s=0;s<p.slope;++s) arr.add(Coeffs::makeHighPass (sr,f,q)); break;
        case Notch:     arr.add (Coeffs::makeNotch    (sr, f, q)); break;
        case BandPass:  arr.add (Coeffs::makeBandPass (sr, f, q)); break;
        default: return 1.0;
    }

    double mag = 1.0;
    for (auto* c : arr) mag *= c->getMagnitudeForFrequency (freq, sr);
    return mag;
}

double EQCurveComponent::computeTotalMagnitudeDb (double freq) const
{
    double m = 1.0;
    for (int b = 0; b < NUM_BANDS; ++b) m *= computeBandMagnitude (b, freq);
    return juce::Decibels::gainToDecibels (m, -60.0);
}

juce::Path EQCurveComponent::buildCurvePath() const
{
    juce::Path path;
    for (int x = 0; x < getWidth(); ++x)
    {
        float y = getYForGain ((float)computeTotalMagnitudeDb (getFreqForX ((float)x)));
        if (x == 0) path.startNewSubPath ((float)x, y);
        else        path.lineTo ((float)x, y);
    }
    return path;
}

void EQCurveComponent::drawCurve (juce::Graphics& g)
{
    auto path = buildCurvePath();
    juce::Path fill = path;
    fill.lineTo ((float)getWidth(), getYForGain (0.f));
    fill.lineTo (0.f, getYForGain (0.f));
    fill.closeSubPath();
    g.setColour (curveFill);
    g.fillPath (fill);
    g.setColour (curve);
    g.strokePath (path, juce::PathStrokeType (1.5f));
}

void EQCurveComponent::drawNodes (juce::Graphics& g)
{
    for (int b = 0; b < NUM_BANDS; ++b)
    {
        auto p = processor.getBandParams (b);
        if (!p.enabled) continue;

        bool sel = (b == selectedBand);
        bool pinnedY = (p.type == LowPass || p.type == HighPass
                     || p.type == Notch   || p.type == BandPass);

        float x = getXForFreq (p.freq);
        float y = pinnedY ? getYForGain (0.f) : getYForGain (p.gainDb);
        float r = sel ? 8.f : 6.f;
        juce::Colour col = bandColour (b);

        if (sel)
        {
            float qR = juce::jmap (p.q, 0.1f, 18.f, 18.f, 55.f);
            g.setColour (col.withAlpha (0.12f));
            g.drawEllipse (x - qR, y - qR * 0.4f, qR * 2.f, qR * 0.8f, 1.f);
        }

        g.setColour (col.withAlpha (sel ? 1.f : 0.7f));
        g.fillEllipse (x - r, y - r, r * 2.f, r * 2.f);
        g.setColour (sel ? juce::Colours::white.withAlpha (0.9f) : col.darker (0.4f));
        g.drawEllipse (x - r, y - r, r * 2.f, r * 2.f, sel ? 1.5f : 1.f);

        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.setFont (juce::FontOptions (8.f));
        g.drawText (juce::String (b + 1), juce::roundToInt (x - 5),
                    juce::roundToInt (y - 5), 10, 10, juce::Justification::centred);
    }
}

void EQCurveComponent::paint (juce::Graphics& g)
{
    g.fillAll (background);
    drawGrid (g);
    drawSpectrum (g);
    drawCurve (g);
    drawNodes (g);
}

void EQCurveComponent::resized()
{
    if (popup->isVisible()) updatePopupPosition();
}

// ---- mouse ----
int EQCurveComponent::findHitNode (float mx, float my) const
{
    float best = 16.f; int hit = -1;
    for (int b = 0; b < NUM_BANDS; ++b)
    {
        auto p = processor.getBandParams (b);
        if (!p.enabled) continue;
        bool pinnedY = (p.type == LowPass || p.type == HighPass
                     || p.type == Notch   || p.type == BandPass);
        float x = getXForFreq (p.freq);
        float y = pinnedY ? getYForGain (0.f) : getYForGain (p.gainDb);
        float d = std::hypot (mx - x, my - y);
        if (d < best) { best = d; hit = b; }
    }
    return hit;
}

int EQCurveComponent::findFirstFreeBand() const
{
    for (int b = 0; b < NUM_BANDS; ++b)
        if (!processor.getBandParams (b).enabled) return b;
    return -1;
}

void EQCurveComponent::addBandAt (float freq, float gainDb)
{
    int b = findFirstFreeBand();
    if (b < 0) return;

    auto setP = [&] (const char* name, float v) {
        auto* param = processor.apvts.getParameter (bandParamID (b, name));
        if (param) param->setValueNotifyingHost (param->convertTo0to1 (v));
    };
    setP ("freq", freq);
    setP ("gain", gainDb);
    // Reset Q and type to defaults
    auto* typeParam = processor.apvts.getParameter (bandParamID (b, "type"));
    if (typeParam) typeParam->setValueNotifyingHost (typeParam->convertTo0to1 (0.f)); // Bell
    auto* qParam = processor.apvts.getParameter (bandParamID (b, "q"));
    if (qParam) qParam->setValueNotifyingHost (qParam->convertTo0to1 (1.f));

    auto* en = processor.apvts.getParameter (bandParamID (b, "enabled"));
    if (en) en->setValueNotifyingHost (1.f);

    selectedBand = b;
    showPopupForBand (b);
}

void EQCurveComponent::removeBand (int b)
{
    auto* en = processor.apvts.getParameter (bandParamID (b, "enabled"));
    if (en) en->setValueNotifyingHost (0.f);
    auto* gainParam = processor.apvts.getParameter (bandParamID (b, "gain"));
    if (gainParam) gainParam->setValueNotifyingHost (gainParam->convertTo0to1 (0.f));

    if (selectedBand == b) selectedBand = -1;
    dismissPopup();
    repaint();
}

void EQCurveComponent::showPopupForBand (int band)
{
    auto p = processor.getBandParams (band);
    bool pinnedY = (p.type == LowPass || p.type == HighPass
                 || p.type == Notch   || p.type == BandPass);
    float nx = getXForFreq (p.freq);
    float ny = pinnedY ? getYForGain (0.f) : getYForGain (p.gainDb);
    popup->showForBand (band, { (int)nx, (int)ny });
}

void EQCurveComponent::dismissPopup()
{
    popup->hidePanel();
    selectedBand = -1;
    repaint();
}

void EQCurveComponent::updatePopupPosition()
{
    if (!popup->isVisible() || popup->currentBand < 0) return;
    int b = popup->currentBand;
    auto p = processor.getBandParams (b);
    bool pinnedY = (p.type == LowPass || p.type == HighPass
                 || p.type == Notch   || p.type == BandPass);
    float nx = getXForFreq (p.freq);
    float ny = pinnedY ? getYForGain (0.f) : getYForGain (p.gainDb);
    popup->showForBand (b, { (int)nx, (int)ny });
}

void EQCurveComponent::mouseDown (const juce::MouseEvent& e)
{
    float mx = (float)e.x, my = (float)e.y;
    int hit = findHitNode (mx, my);

    if (hit >= 0)
    {
        selectedBand = hit;
        draggingBand = hit;
        isDragging   = true;
        dragStartMouse = { mx, my };
        auto p = processor.getBandParams (hit);
        dragStartFreq = p.freq;
        dragStartGain = p.gainDb;
        showPopupForBand (hit);
    }
    else if (!e.mods.isRightButtonDown())
    {
        float freq = juce::jlimit (minFreq, maxFreq, getFreqForX (mx));
        float gain = juce::jlimit (minGain, maxGain, getGainForY (my));
        addBandAt (freq, gain);
        draggingBand = selectedBand;
        isDragging   = true;
        dragStartMouse = { mx, my };
        auto p2 = processor.getBandParams (selectedBand);
        dragStartFreq = p2.freq;
        dragStartGain = p2.gainDb;
    }
    repaint();
}

void EQCurveComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (!isDragging || draggingBand < 0) return;

    float dx = (float)(e.x - dragStartMouse.x);
    float dy = (float)(e.y - dragStartMouse.y);
    float w  = (float)getWidth();
    float h  = (float)getHeight();

    float normX = juce::jlimit (0.f, 1.f,
        std::log (dragStartFreq / minFreq) / std::log (maxFreq / minFreq) + dx / w);
    float newFreq = minFreq * std::pow (maxFreq / minFreq, normX);
    float newGain = juce::jlimit (minGain, maxGain,
        dragStartGain - dy * (maxGain - minGain) / h);

    auto setParam = [&] (const char* name, float v) {
        auto* param = processor.apvts.getParameter (bandParamID (draggingBand, name));
        if (param) param->setValueNotifyingHost (param->convertTo0to1 (v));
    };
    setParam ("freq", newFreq);
    setParam ("gain", newGain);

    updatePopupPosition();
    repaint();
}

void EQCurveComponent::mouseUp (const juce::MouseEvent&)
{
    isDragging = false; draggingBand = -1;
}

void EQCurveComponent::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    if (selectedBand < 0) return;
    auto* p = processor.apvts.getParameter (bandParamID (selectedBand, "q"));
    if (p) p->setValueNotifyingHost (juce::jlimit (0.f, 1.f, p->getValue() + w.deltaY * 0.06f));
    repaint();
}

void EQCurveComponent::mouseDoubleClick (const juce::MouseEvent& e)
{
    int hit = findHitNode ((float)e.x, (float)e.y);
    if (hit >= 0)
    {
        auto* gp = processor.apvts.getParameter (bandParamID (hit, "gain"));
        if (gp) gp->setValueNotifyingHost (gp->convertTo0to1 (0.f));
        repaint();
    }
}

//==============================================================================
// BandPopupPanel
//==============================================================================
static void styleSlider (juce::Slider& s, juce::Colour col)
{
    s.setSliderStyle (juce::Slider::LinearBar);
    s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    s.setColour (juce::Slider::trackColourId,             col.withAlpha (0.55f));
    s.setColour (juce::Slider::backgroundColourId,        RVZNColours::controlBg);
    s.setColour (juce::Slider::thumbColourId,             col);
    s.setColour (juce::Slider::textBoxTextColourId,       RVZNColours::valueText);
    s.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    s.setColour (juce::Slider::textBoxOutlineColourId,    juce::Colours::transparentBlack);
}

static void styleLabel (juce::Label& l, const juce::String& text)
{
    l.setText (text, juce::dontSendNotification);
    l.setFont (juce::FontOptions (9.f));
    l.setColour (juce::Label::textColourId, RVZNColours::labelText);
    l.setJustificationType (juce::Justification::centred);
}

BandPopupPanel::BandPopupPanel (RVZNEQAudioProcessor& p) : processor (p)
{
    // Band label
    bandLabel.setFont (juce::FontOptions (10.f));
    bandLabel.setColour (juce::Label::textColourId, RVZNColours::valueText);
    addAndMakeVisible (bandLabel);

    // Close button
    closeBtn.setButtonText (juce::CharPointer_UTF8 ("\xc3\x97")); // ×
    closeBtn.setColour (juce::TextButton::buttonColourId,     RVZNColours::controlBg);
    closeBtn.setColour (juce::TextButton::textColourOffId,    RVZNColours::labelText);
    closeBtn.onClick = [this] { if (onRemoveBand) onRemoveBand (currentBand); };
    addAndMakeVisible (closeBtn);

    // Bypass toggle
    bypassToggle.setButtonText ("ON");
    bypassToggle.setColour (juce::ToggleButton::textColourId,         RVZNColours::valueText);
    bypassToggle.setColour (juce::ToggleButton::tickColourId,         RVZNColours::accent);
    bypassToggle.setColour (juce::ToggleButton::tickDisabledColourId, RVZNColours::labelText);
    addAndMakeVisible (bypassToggle);

    // Sliders
    styleSlider (freqSlider, RVZNColours::accent);
    styleSlider (gainSlider, RVZNColours::accent);
    styleSlider (qSlider,    RVZNColours::accent);
    addAndMakeVisible (freqSlider);
    addAndMakeVisible (gainSlider);
    addAndMakeVisible (qSlider);

    styleLabel (freqLabel, "FREQ");
    styleLabel (gainLabel, "GAIN");
    styleLabel (qLabel,    "Q");
    addAndMakeVisible (freqLabel);
    addAndMakeVisible (gainLabel);
    addAndMakeVisible (qLabel);

    // Type buttons
    for (int i = 0; i < 7; ++i)
    {
        typeBtn[i].setButtonText (typeNames[i]);
        typeBtn[i].setColour (juce::TextButton::buttonColourId,   RVZNColours::controlBg);
        typeBtn[i].setColour (juce::TextButton::buttonOnColourId, RVZNColours::accent);
        typeBtn[i].setColour (juce::TextButton::textColourOffId,  RVZNColours::labelText);
        typeBtn[i].setColour (juce::TextButton::textColourOnId,   juce::Colours::white);
        typeBtn[i].onClick = [this, i] {
            auto* param = processor.apvts.getParameter (bandParamID (currentBand, "type"));
            if (param) param->setValueNotifyingHost (param->convertTo0to1 ((float)i));
        };
        addAndMakeVisible (typeBtn[i]);
    }

    // Slope buttons
    for (int i = 0; i < 4; ++i)
    {
        slopeBtn[i].setButtonText (juce::String (slopeNames[i]));
        slopeBtn[i].setColour (juce::TextButton::buttonColourId,   RVZNColours::controlBg);
        slopeBtn[i].setColour (juce::TextButton::buttonOnColourId, RVZNColours::accent);
        slopeBtn[i].setColour (juce::TextButton::textColourOffId,  RVZNColours::labelText);
        slopeBtn[i].setColour (juce::TextButton::textColourOnId,   juce::Colours::white);
        slopeBtn[i].onClick = [this, i] {
            auto* param = processor.apvts.getParameter (bandParamID (currentBand, "slope"));
            if (param) param->setValueNotifyingHost (param->convertTo0to1 ((float)i));
        };
        addAndMakeVisible (slopeBtn[i]);
    }

    styleLabel (slopeLabel, "SLOPE");
    addAndMakeVisible (slopeLabel);

    setSize (kW, 120);
    startTimerHz (20);
}

BandPopupPanel::~BandPopupPanel()
{
    stopTimer();
}

int BandPopupPanel::currentHeight() const
{
    return showSlopeRow ? 156 : 128;
}

void BandPopupPanel::showForBand (int band, juce::Point<int> nodePos)
{
    currentBand = band;
    rebuildAttachments();
    updateButtonStates();

    // Determine slope row visibility
    auto p = processor.getBandParams (band);
    bool needSlope = (p.type == LowPass || p.type == HighPass);
    if (needSlope != showSlopeRow) { showSlopeRow = needSlope; resized(); }

    int h = currentHeight();
    setSize (kW, h);

    // Position: above node; clamp into parent
    auto* par = getParentComponent();
    int pw = par ? par->getWidth()  : 800;
    int ph = par ? par->getHeight() : 500;

    int x = nodePos.x - kW / 2;
    int y = nodePos.y - h - 12;
    if (y < 4)      y = nodePos.y + 16;
    if (y + h > ph) y = ph - h - 4;
    x = juce::jlimit (4, pw - kW - 4, x);

    setBounds (x, y, kW, h);
    setVisible (true);
    toFront (false);
    resized();
}

void BandPopupPanel::hidePanel()
{
    freqAttach.reset(); gainAttach.reset(); qAttach.reset(); bypassAttach.reset();
    currentBand = -1;
    setVisible (false);
}

void BandPopupPanel::rebuildAttachments()
{
    freqAttach.reset(); gainAttach.reset(); qAttach.reset(); bypassAttach.reset();
    if (currentBand < 0) return;

    freqAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>
        (processor.apvts, bandParamID (currentBand, "freq"),    freqSlider);
    gainAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>
        (processor.apvts, bandParamID (currentBand, "gain"),    gainSlider);
    qAttach    = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>
        (processor.apvts, bandParamID (currentBand, "q"),       qSlider);
    bypassAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>
        (processor.apvts, bandParamID (currentBand, "enabled"), bypassToggle);
}

void BandPopupPanel::updateButtonStates()
{
    if (currentBand < 0) return;

    juce::Colour col = bandColour (currentBand);
    bandLabel.setText ("BAND " + juce::String (currentBand + 1), juce::dontSendNotification);
    bandLabel.setColour (juce::Label::textColourId, col);

    styleSlider (freqSlider, col);
    styleSlider (gainSlider, col);
    styleSlider (qSlider,    col);

    auto p = processor.getBandParams (currentBand);
    int  t = p.type;
    int  s = p.slope - 1; // slope is 1-4, buttons are 0-3

    for (int i = 0; i < 7; ++i) typeBtn[i].setToggleState (i == t, juce::dontSendNotification);
    for (int i = 0; i < 4; ++i) slopeBtn[i].setToggleState (i == s, juce::dontSendNotification);

    bool needSlope = (t == LowPass || t == HighPass);
    if (needSlope != showSlopeRow)
    {
        showSlopeRow = needSlope;
        setSize (kW, currentHeight());
        resized();
    }
    for (auto& sb : slopeBtn) sb.setVisible (showSlopeRow);
    slopeLabel.setVisible (showSlopeRow);
}

void BandPopupPanel::timerCallback()
{
    if (isVisible() && currentBand >= 0) updateButtonStates();
}

void BandPopupPanel::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour (RVZNColours::panelBg.withAlpha (0.97f));
    g.fillRoundedRectangle (bounds, 6.f);
    g.setColour (RVZNColours::grid.brighter (0.3f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 6.f, 1.f);

    // Header separator
    g.setColour (RVZNColours::grid);
    g.drawHorizontalLine (28, 6.f, (float)getWidth() - 6.f);

    if (currentBand >= 0)
    {
        juce::Colour col = bandColour (currentBand);
        float r = 4.f;
        g.setColour (col);
        g.fillEllipse (10.f, (28.f - r * 2.f) * 0.5f, r * 2.f, r * 2.f);
    }
}

void BandPopupPanel::resized()
{
    auto area = getLocalBounds().reduced (6, 0);

    // Header row
    auto header = area.removeFromTop (28);
    header.removeFromLeft (18); // space for dot
    bandLabel.setBounds (header.removeFromLeft (80));
    closeBtn.setBounds (header.removeFromRight (24).withSizeKeepingCentre (20, 18));
    bypassToggle.setBounds (header.removeFromRight (44).withSizeKeepingCentre (40, 18));

    area.removeFromTop (6);

    // Slider labels row
    auto lblRow = area.removeFromTop (12);
    int sw = (area.getWidth() - 4) / 3;
    freqLabel.setBounds (lblRow.removeFromLeft (sw));
    lblRow.removeFromLeft (2);
    gainLabel.setBounds (lblRow.removeFromLeft (sw));
    lblRow.removeFromLeft (2);
    qLabel.setBounds    (lblRow);

    // Sliders row
    auto sliderRow = area.removeFromTop (22);
    freqSlider.setBounds (sliderRow.removeFromLeft (sw));
    sliderRow.removeFromLeft (2);
    gainSlider.setBounds (sliderRow.removeFromLeft (sw));
    sliderRow.removeFromLeft (2);
    qSlider.setBounds    (sliderRow);

    area.removeFromTop (8);

    // Type buttons row
    auto typeRow = area.removeFromTop (22);
    int tbW = typeRow.getWidth() / 7;
    for (auto& tb : typeBtn)
        tb.setBounds (typeRow.removeFromLeft (tbW));

    if (showSlopeRow)
    {
        area.removeFromTop (6);
        auto slopeRow = area.removeFromTop (22);
        slopeLabel.setBounds (slopeRow.removeFromLeft (38));
        int sbW = slopeRow.getWidth() / 4;
        for (auto& sb : slopeBtn)
            sb.setBounds (slopeRow.removeFromLeft (sbW));
    }
}

//==============================================================================
// RVZNEQAudioProcessorEditor
//==============================================================================
RVZNEQAudioProcessorEditor::RVZNEQAudioProcessorEditor (RVZNEQAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p), curveComp (p)
{
    addAndMakeVisible (curveComp);
    setSize (900, 480);
    setResizable (true, true);
    setResizeLimits (600, 320, 1600, 900);

    // GPU-accelerate all rendering in this window
    openGLContext.attachTo (*this);
}

RVZNEQAudioProcessorEditor::~RVZNEQAudioProcessorEditor()
{
    openGLContext.detach();
}

void RVZNEQAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (panelBg);

    // Title bar
    g.setColour (panelBg);
    g.fillRect (0, 0, getWidth(), 34);
    g.setColour (grid);
    g.drawHorizontalLine (34, 0.f, (float)getWidth());

    g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 15.f, juce::Font::plain));
    g.setColour (juce::Colours::white);
    g.drawText ("RVZN", 14, 0, 52, 34, juce::Justification::centredLeft);
    g.setColour (accent);
    g.drawText ("EQ", 48, 0, 26, 34, juce::Justification::centredLeft);
    g.setFont (juce::FontOptions (9.f));
    g.setColour (labelText);
    g.drawText ("PARAMETRIC EQUALIZER", 80, 0, 180, 34, juce::Justification::centredLeft);
}

void RVZNEQAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop (34);
    curveComp.setBounds (area);
}
