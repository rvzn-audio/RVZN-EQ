#include "PluginProcessor.h"
#include "PluginEditor.h"

static juce::String bandParamID (int b, const char* name)
{
    return juce::String ("band") + juce::String (b) + "_" + name;
}

static const char* typeNames[]  = { "Bell","LoS","HiS","LP","HP","Ntch","BP" };
static const char* slopeNames[] = { "6","12","18","24" };

static void styleLabel (juce::Label& l, const juce::String& text)
{
    l.setText (text, juce::dontSendNotification);
    l.setFont (juce::FontOptions (9.f));
    l.setColour (juce::Label::textColourId, RvznColours::textMuted);
    l.setJustificationType (juce::Justification::centred);
}

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

    startTimerHz (60);
}

EQCurveComponent::~EQCurveComponent()
{
    stopTimer();
}

void EQCurveComponent::timerCallback()
{
    bool needRepaint = isDragging;

    // Rebuild curve only when parameters changed
    int paramCount = processor.paramChangeCounter.load (std::memory_order_relaxed);
    if (paramCount != lastParamCount)
    {
        lastParamCount = paramCount;
        curveDirty = true;
    }

    if (curveDirty)
    {
        cachedCurvePath = buildCurvePath();
        curveDirty = false;
        needRepaint = true;
    }

    {
        // Try-lock so we never block the audio thread. If contended, skip — we
        // still have the previous spectrum in displaySpectrum.
        const juce::SpinLock::ScopedTryLockType lock (processor.spectrumLock);
        if (lock.isLocked() && processor.newSpectrumAvailable.exchange (false))
        {
            for (int i = 0; i < FFT_SIZE / 2; ++i)
            {
                float newVal = processor.spectrumData[(size_t)i];
                float oldVal = displaySpectrum[(size_t)i];
                float sm = (newVal > oldVal) ? 0.4f : 0.88f;
                displaySpectrum[(size_t)i] = oldVal * sm + newVal * (1.f - sm);
            }
            needRepaint = true;
        }
    }

    // Spring physics for + button
    auto spring = [](float& pos, float& vel, float target) {
        const float stiff = 0.18f, damp = 0.72f;
        vel += (target - pos) * stiff;
        vel *= damp;
        pos += vel;
    };

    spring (plusPos.x,  plusVel.x, plusTarget.x);
    spring (plusPos.y,  plusVel.y, plusTarget.y);
    spring (plusScale,  plusScaleVel, plusScaleTarget);

    if (plusScale < 0.01f && plusScaleTarget < 0.01f)
        plusVisible = false;

    // Panel morph animation
    if (popup->isVisible() && panelIsOpening && panelMorphProgress < 1.0f)
    {
        panelMorphProgress = juce::jmin (1.0f, panelMorphProgress + 1.0f / 13.0f);
        float ease = elasticEaseOut (panelMorphProgress);
        int w = (int)juce::jmap (ease, 0.f, 1.f, 28.f, (float)BandPopupPanel::kW);
        int h = (int)juce::jmap (ease, 0.f, 1.f, 28.f, (float)panelTargetH);
        auto* par = getParentComponent();
        int px = juce::jlimit (4, (par ? par->getWidth()  : 800) - BandPopupPanel::kW - 4,
                               panelOriginPt.x - BandPopupPanel::kW / 2);
        int py = juce::jlimit (4, (par ? par->getHeight() : 500) - h - 4,
                               panelOriginPt.y - h - 12);
        popup->setBounds (px, py, w, h);
        popup->setAlpha (ease);
        if (panelMorphProgress >= 1.0f)
            panelIsOpening = false;
    }

    bool springActive = std::abs (plusScaleVel) > 0.001f
                     || std::abs (plusVel.x)    > 0.1f
                     || std::abs (plusVel.y)    > 0.1f;
    bool morphActive  = popup->isVisible() && panelIsOpening;

    if (needRepaint || springActive || morphActive)
        repaint();
}

// ---- elastic ease out ----
float EQCurveComponent::elasticEaseOut (float t) noexcept
{
    if (t <= 0.f) return 0.f;
    if (t >= 1.f) return 1.f;
    return std::pow (2.f, -10.f * t)
         * std::sin ((t * 10.f - 0.75f) * (juce::MathConstants<float>::twoPi / 3.f))
         + 1.f;
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

// ---- getCurveYAtX ----
float EQCurveComponent::getCurveYAtX (float x) const
{
    if (cachedCurveXs.empty()) return (float) getHeight() * 0.5f;

    // cachedCurveXs is monotonically increasing — binary search for the upper bound
    auto it = std::lower_bound (cachedCurveXs.begin(), cachedCurveXs.end(), x);
    if (it == cachedCurveXs.begin()) return cachedCurveYs.front();
    if (it == cachedCurveXs.end())   return cachedCurveYs.back();

    auto i = (size_t) (it - cachedCurveXs.begin());
    float x0 = cachedCurveXs[i - 1], x1 = cachedCurveXs[i];
    float y0 = cachedCurveYs[i - 1], y1 = cachedCurveYs[i];
    float t = (x1 > x0) ? (x - x0) / (x1 - x0) : 0.f;
    return y0 + t * (y1 - y0);
}

// ---- drawing ----
void EQCurveComponent::drawGrid (juce::Graphics& g)
{
    int iw = getWidth(), ih = getHeight();

    if (gridDirty || gridImage.getWidth() != iw || gridImage.getHeight() != ih)
    {
        gridImage = juce::Image (juce::Image::ARGB, iw, ih, true);
        juce::Graphics ig (gridImage);

        float w = (float)iw, h = (float)ih;

        // Background
        ig.setColour (RvznColours::background);
        ig.fillAll();

        // Frequency grid lines
        static const float fLines[] = {
            20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };

        ig.setColour (RvznColours::gridLine);
        for (float f : fLines)
            ig.drawVerticalLine (juce::roundToInt (getXForFreq (f)), 0.f, h);

        // dB grid lines
        static const float dbLines[] = { -12, -6, -3, 0, 3, 6, 12 };
        for (float db : dbLines)
        {
            float y = getYForGain (db);
            ig.setColour (db == 0.f ? RvznColours::borderMid : RvznColours::gridLine);
            ig.drawHorizontalLine (juce::roundToInt (y), 0.f, w);
        }

        // Frequency labels
        static const float lblF[] = { 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };
        ig.setFont (juce::FontOptions (9.f));
        ig.setColour (RvznColours::textDim);
        for (float f : lblF)
        {
            float x = getXForFreq (f);
            juce::String lbl = f >= 1000.f
                ? juce::String (f / 1000.f, 0) + "k"
                : juce::String ((int)f);
            ig.drawText (lbl, juce::roundToInt (x) - 14, (int)h - 14, 28, 12,
                         juce::Justification::centred);
        }

        // dB labels
        for (float db : dbLines)
        {
            float y = getYForGain (db);
            juce::String lbl = (db > 0 ? "+" : "") + juce::String ((int)db);
            ig.drawText (lbl, 2, juce::roundToInt (y) - 6, 26, 12, juce::Justification::left);
        }

        gridDirty = false;
    }

    g.drawImageAt (gridImage, 0, 0);
}

void EQCurveComponent::drawSpectrum (juce::Graphics& g)
{
    float w = (float)getWidth(), h = (float)getHeight();
    double sr = processor.currentSampleRate;
    if (sr <= 0.0) return;

    const int numPoints = 512;
    juce::Path path;
    bool started = false;

    for (int i = 0; i < numPoints; ++i)
    {
        float t    = (float)i / (float)(numPoints - 1);
        float freq = minFreq * std::pow (maxFreq / minFreq, t);
        float x    = w * t;

        float binF = (float)(freq / sr) * (float)FFT_SIZE;
        int   bin0 = juce::jlimit (0, FFT_SIZE / 2 - 1, (int)binF);
        int   bin1 = juce::jlimit (0, FFT_SIZE / 2 - 1, bin0 + 1);
        float frac = binF - (float)bin0;

        float dB = displaySpectrum[(size_t)bin0] * (1.f - frac)
                 + displaySpectrum[(size_t)bin1] * frac;
        float y  = (1.f - juce::jlimit (0.f, 1.f, (dB + 80.f) / 80.f)) * h;

        if (!started) { path.startNewSubPath (x, y); started = true; }
        else          { path.lineTo (x, y); }
    }

    if (started)
    {
        path.lineTo (w, h); path.lineTo (0.f, h); path.closeSubPath();
        g.setColour (RvznColours::surface.withAlpha (0.7f));
        g.fillPath (path);
        g.setColour (RvznColours::accentBlue.withAlpha (0.12f));
        g.strokePath (path, juce::PathStrokeType (0.6f));
    }
}

double EQCurveComponent::computeBandMagnitude (int band, double freq) const
{
    auto p = processor.getBandParams (band);
    if (!p.enabled) return 1.0;

    double sr = processor.currentSampleRate;
    auto arr = RVZNEQAudioProcessor::buildBandCoefficients (p, sr);

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

juce::Path EQCurveComponent::buildCurvePath()
{
    double sr = processor.currentSampleRate;

    using Coeffs = juce::dsp::IIR::Coefficients<float>;
    struct BandCoeffs { juce::ReferenceCountedArray<Coeffs> arr; bool enabled = false; };
    std::array<BandCoeffs, NUM_BANDS> bc;

    for (int b = 0; b < NUM_BANDS; ++b)
    {
        auto p = processor.getBandParams (b);
        bc[b].enabled = p.enabled;
        if (!p.enabled) continue;
        bc[b].arr = RVZNEQAudioProcessor::buildBandCoefficients (p, sr);
    }

    const int numPoints = 256;
    juce::Path path;
    cachedCurveXs.clear();
    cachedCurveYs.clear();
    cachedCurveXs.reserve ((size_t)numPoints);
    cachedCurveYs.reserve ((size_t)numPoints);

    for (int i = 0; i < numPoints; ++i)
    {
        float t    = (float)i / (float)(numPoints - 1);
        float freq = minFreq * std::pow (maxFreq / minFreq, t);
        float x    = getXForFreq (freq);

        double mag = 1.0;
        for (int b = 0; b < NUM_BANDS; ++b)
        {
            if (!bc[b].enabled) continue;
            for (auto* c : bc[b].arr)
                mag *= c->getMagnitudeForFrequency ((double)freq, sr);
        }

        float y = getYForGain ((float)juce::Decibels::gainToDecibels (mag, -60.0));

        cachedCurveXs.push_back (x);
        cachedCurveYs.push_back (y);

        if (i == 0) path.startNewSubPath (x, y);
        else        path.lineTo (x, y);
    }

    return path;
}

void EQCurveComponent::drawPerBandCurves (juce::Graphics& g)
{
    double sr = processor.currentSampleRate;

    for (int b = 0; b < NUM_BANDS; ++b)
    {
        auto p = processor.getBandParams (b);
        if (!p.enabled) continue;

        auto arr = RVZNEQAudioProcessor::buildBandCoefficients (p, sr);

        const int numPoints = 256;
        juce::Path bandPath;
        bool started = false;

        for (int i = 0; i < numPoints; ++i)
        {
            float t    = (float)i / (float)(numPoints - 1);
            float freq = minFreq * std::pow (maxFreq / minFreq, t);
            float x    = getXForFreq (freq);

            double mag = 1.0;
            for (auto* c : arr)
                mag *= c->getMagnitudeForFrequency ((double)freq, sr);

            float y = getYForGain ((float)juce::Decibels::gainToDecibels (mag, -60.0));

            if (!started) { bandPath.startNewSubPath (x, y); started = true; }
            else          { bandPath.lineTo (x, y); }
        }

        g.setColour (RvznColours::bandColour (b).withAlpha (0.25f));
        g.strokePath (bandPath, juce::PathStrokeType (0.8f));
    }
}

void EQCurveComponent::drawCurve (juce::Graphics& g)
{
    juce::Path fill = cachedCurvePath;
    fill.lineTo ((float)getWidth(), getYForGain (0.f));
    fill.lineTo (0.f, getYForGain (0.f));
    fill.closeSubPath();
    g.setColour (RvznColours::curveFill);
    g.fillPath (fill);
    g.setColour (RvznColours::curveBlue);
    g.strokePath (cachedCurvePath, juce::PathStrokeType (1.5f));
}

void EQCurveComponent::drawNodes (juce::Graphics& g)
{
    for (int b = 0; b < NUM_BANDS; ++b)
    {
        auto p = processor.getBandParams (b);
        if (!p.enabled) continue;

        bool sel     = (b == selectedBand);
        bool hovered = (b == hoveredBand);
        bool pinnedY = (p.type == LowPass || p.type == HighPass
                     || p.type == Notch   || p.type == BandPass);

        float x   = getXForFreq (p.freq);
        float y   = pinnedY ? getYForGain (0.f) : getYForGain (p.gainDb);
        float r   = sel ? 8.f : 6.f;
        juce::Colour col = RvznColours::bandColour (b);

        // Q indicator ring for selected band
        if (sel)
        {
            float qR = juce::jmap (p.q, 0.1f, 18.f, 18.f, 55.f);
            g.setColour (col.withAlpha (0.12f));
            g.drawEllipse (x - qR, y - qR * 0.4f, qR * 2.f, qR * 0.8f, 1.f);
        }

        // Hover glow
        if (hovered)
        {
            const float gr = 10.f;
            g.setColour (col.withAlpha (0.20f));
            g.drawEllipse (x - gr, y - gr, gr * 2.f, gr * 2.f, 1.5f);
        }

        // Main circle fill
        g.setColour (col);
        g.fillEllipse (x - r, y - r, r * 2.f, r * 2.f);

        // Border ring
        g.setColour (RvznColours::nodeBorder);
        g.drawEllipse (x - r, y - r, r * 2.f, r * 2.f, 1.f);

        // Number label
        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (8.f));
        g.drawText (juce::String (b + 1),
                    juce::roundToInt (x - 5), juce::roundToInt (y - 5),
                    10, 10, juce::Justification::centred);
    }
}

void EQCurveComponent::paint (juce::Graphics& g)
{
    g.fillAll (RvznColours::background);
    drawGrid (g);
    if (analyzerEnabled) drawSpectrum (g);
    drawPerBandCurves (g);
    drawCurve (g);
    drawNodes (g);

    // + button
    if (plusVisible && plusScale > 0.01f)
    {
        const float r = 14.f * plusScale;
        g.setColour (juce::Colour (0xFF1A1E28));
        g.fillEllipse (plusPos.x - r, plusPos.y - r, r * 2.f, r * 2.f);
        g.setColour (RvznColours::accentBlue.withAlpha (plusScale));
        g.drawEllipse (plusPos.x - r, plusPos.y - r, r * 2.f, r * 2.f, 1.5f);
        const float arm = 5.f * plusScale;
        g.setColour (RvznColours::accentBlue.withAlpha (plusScale));
        g.drawLine (plusPos.x - arm, plusPos.y, plusPos.x + arm, plusPos.y, 1.5f * plusScale);
        g.drawLine (plusPos.x, plusPos.y - arm, plusPos.x, plusPos.y + arm, 1.5f * plusScale);
    }
}

void EQCurveComponent::resized()
{
    gridDirty  = true;
    curveDirty = true;
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
    // Reset ALL params to known defaults — a recycled slot can carry stale
    // slope / mode / type from a previously-deleted band.
    setP ("freq",  freq);
    setP ("gain",  gainDb);
    setP ("q",     1.f);
    setP ("type",  (float) Bell);
    setP ("slope", 1.f);   // 12 dB/oct
    setP ("mode",  (float) Stereo);

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

    panelOriginPt    = { (int)nx, (int)ny };
    panelMorphProgress = 0.0f;
    panelIsOpening   = true;

    popup->setAlpha (0.0f);
    popup->showForBand (band, panelOriginPt);
    panelTargetH = popup->currentHeight();
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

void EQCurveComponent::mouseMove (const juce::MouseEvent& e)
{
    float mx = (float)e.x, my = (float)e.y;

    // Update hovered band
    int newHovered = -1;
    for (int b = 0; b < NUM_BANDS; ++b)
    {
        auto p = processor.getBandParams (b);
        if (!p.enabled) continue;
        bool pinnedY = (p.type == LowPass || p.type == HighPass
                     || p.type == Notch   || p.type == BandPass);
        float nx = getXForFreq (p.freq);
        float ny = pinnedY ? getYForGain (0.f) : getYForGain (p.gainDb);
        if (std::hypot (mx - nx, my - ny) < 14.f) { newHovered = b; break; }
    }
    if (newHovered != hoveredBand)
    {
        hoveredBand = newHovered;
        repaint();
    }

    // Spring + button
    bool nearNode = (findHitNode (mx, my) >= 0);
    if (!nearNode && std::abs (my - getCurveYAtX (mx)) < 16.f)
    {
        plusTarget   = { mx, getCurveYAtX (mx) };
        plusScaleTarget = 1.0f;
        plusVisible  = true;
    }
    else
    {
        plusScaleTarget = 0.0f;
    }
}

void EQCurveComponent::mouseExit (const juce::MouseEvent&)
{
    hoveredBand     = -1;
    plusScaleTarget = 0.f;
}

void EQCurveComponent::mouseDown (const juce::MouseEvent& e)
{
    float mx = (float)e.x, my = (float)e.y;

    // + button hit test
    if (plusVisible && plusPos.getDistanceFrom (e.position.toFloat()) < 14.f * plusScale)
    {
        float freq = juce::jlimit (minFreq, maxFreq, getFreqForX (plusPos.x));
        float gain = juce::jlimit (minGain, maxGain, getGainForY (plusPos.y));
        addBandAt (freq, gain);
        plusScaleTarget = 0.f;
        return;
    }

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
    else if (popup->isVisible())
    {
        dismissPopup();
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
}

void EQCurveComponent::mouseDoubleClick (const juce::MouseEvent& e)
{
    int hit = findHitNode ((float)e.x, (float)e.y);
    if (hit >= 0)
    {
        auto* gp = processor.apvts.getParameter (bandParamID (hit, "gain"));
        if (gp) gp->setValueNotifyingHost (gp->convertTo0to1 (0.f));
    }
}

//==============================================================================
// BandPopupPanel
//==============================================================================
BandPopupPanel::BandPopupPanel (RVZNEQAudioProcessor& p) : processor (p)
{
    setLookAndFeel (&laf);

    // Band label
    bandLabel.setFont (juce::FontOptions (10.f));
    bandLabel.setColour (juce::Label::textColourId, RvznColours::textPrimary);
    addAndMakeVisible (bandLabel);

    // Close button
    closeBtn.setButtonText (juce::CharPointer_UTF8 ("\xc3\x97")); // ×
    closeBtn.getProperties().set ("style", "header");
    closeBtn.onClick = [this] { if (onRemoveBand) onRemoveBand (currentBand); };
    addAndMakeVisible (closeBtn);

    // Bypass toggle
    bypassToggle.setButtonText ("ON");
    bypassToggle.accentColour = RvznColours::accentBlue;
    addAndMakeVisible (bypassToggle);

    // Sliders — Rotary style
    auto setupSlider = [] (juce::Slider& s)
    {
        s.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 14);
    };
    setupSlider (freqSlider);
    setupSlider (gainSlider);
    setupSlider (qSlider);
    freqSlider.setTooltip ("Center / corner frequency (Hz)");
    gainSlider.setTooltip ("Gain (dB) — only used for Bell, Shelf");
    qSlider.setTooltip    ("Q / resonance / steepness");
    bypassToggle.setTooltip ("Enable / bypass this band");
    closeBtn.setTooltip     ("Remove this band");

    freqSlider.getProperties().set ("accentColour", (juce::int64)RvznColours::accentBlue.getARGB());
    gainSlider.getProperties().set ("accentColour", (juce::int64)RvznColours::accentBlue.getARGB());
    qSlider.getProperties().set    ("accentColour", (juce::int64)RvznColours::accentBlue.getARGB());

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
        typeBtn[i].getProperties().set ("style", "filterType");
        typeBtn[i].getProperties().set ("accentColour", (juce::int64)RvznColours::accentBlue.getARGB());
        typeBtn[i].setClickingTogglesState (true);
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
        slopeBtn[i].getProperties().set ("style", "filterType");
        slopeBtn[i].getProperties().set ("accentColour", (juce::int64)RvznColours::accentBlue.getARGB());
        slopeBtn[i].setClickingTogglesState (true);
        slopeBtn[i].onClick = [this, i] {
            auto* param = processor.apvts.getParameter (bandParamID (currentBand, "slope"));
            if (param) param->setValueNotifyingHost (param->convertTo0to1 ((float)i));
        };
        addAndMakeVisible (slopeBtn[i]);
    }

    styleLabel (slopeLabel, "SLOPE");
    addAndMakeVisible (slopeLabel);

    // M/S mode buttons
    static const char* modeNames[] = { "L/R", "MID", "SID" };
    for (int i = 0; i < 3; ++i)
    {
        modeBtn[i].setButtonText (modeNames[i]);
        modeBtn[i].getProperties().set ("style", "filterType");
        modeBtn[i].setClickingTogglesState (true);
        modeBtn[i].onClick = [this, i] {
            auto* param = processor.apvts.getParameter (bandParamID (currentBand, "mode"));
            if (param) param->setValueNotifyingHost (param->convertTo0to1 ((float)i));
        };
        addAndMakeVisible (modeBtn[i]);
    }
    styleLabel (modeLabel, "MODE");
    addAndMakeVisible (modeLabel);

    setSize (kW, currentHeight());
    startTimerHz (20);
}

BandPopupPanel::~BandPopupPanel()
{
    setLookAndFeel (nullptr);
    stopTimer();
}

int BandPopupPanel::currentHeight() const
{
    return showSlopeRow ? 220 : 188;
}

void BandPopupPanel::showForBand (int band, juce::Point<int> nodePos)
{
    currentBand = band;
    rebuildAttachments();
    updateButtonStates();

    auto p = processor.getBandParams (band);
    bool needSlope = (p.type == LowPass || p.type == HighPass);
    if (needSlope != showSlopeRow) { showSlopeRow = needSlope; resized(); }

    int h = currentHeight();
    setSize (kW, h);

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

    juce::Colour col = RvznColours::bandColour (currentBand);
    bandLabel.setText ("BAND " + juce::String (currentBand + 1), juce::dontSendNotification);
    bandLabel.setColour (juce::Label::textColourId, col);

    bypassToggle.accentColour = col;

    auto colARGB = (juce::int64)col.getARGB();
    for (auto& tb : typeBtn)
        tb.getProperties().set ("accentColour", colARGB);
    for (auto& sb : slopeBtn)
        sb.getProperties().set ("accentColour", colARGB);
    for (auto& mb : modeBtn)
        mb.getProperties().set ("accentColour", colARGB);

    freqSlider.getProperties().set ("accentColour", colARGB);
    gainSlider.getProperties().set ("accentColour", colARGB);
    qSlider.getProperties().set    ("accentColour", colARGB);

    auto p = processor.getBandParams (currentBand);
    int  t = p.type;
    int  s = p.slope - 1;

    for (int i = 0; i < 7; ++i) typeBtn[i].setToggleState  (i == t, juce::dontSendNotification);
    for (int i = 0; i < 4; ++i) slopeBtn[i].setToggleState (i == s, juce::dontSendNotification);

    int m = p.mode;
    for (int i = 0; i < 3; ++i) modeBtn[i].setToggleState (i == m, juce::dontSendNotification);

    bool needSlope = (t == LowPass || t == HighPass);
    if (needSlope != showSlopeRow)
    {
        showSlopeRow = needSlope;
        setSize (kW, currentHeight());
        resized();
    }
    for (auto& sb : slopeBtn) sb.setVisible (showSlopeRow);
    slopeLabel.setVisible (showSlopeRow);

    repaint();
}

void BandPopupPanel::timerCallback()
{
    if (!isVisible() || currentBand < 0) return;

    int paramCount = processor.paramChangeCounter.load (std::memory_order_relaxed);
    if (paramCount != lastParamCount)
    {
        lastParamCount = paramCount;
        updateButtonStates();
    }
}

void BandPopupPanel::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();

    // Background
    g.setColour (RvznColours::surface);
    g.fillRoundedRectangle (b, 12.f);

    // Border
    g.setColour (RvznColours::borderMid);
    g.drawRoundedRectangle (b.reduced (0.5f), 12.f, 1.f);

    // Header separator
    g.setColour (RvznColours::border);
    g.drawHorizontalLine (28, 6.f, b.getWidth() - 6.f);

    // Accent dot
    if (currentBand >= 0)
    {
        juce::Colour col = RvznColours::bandColour (currentBand);
        g.setColour (col);
        g.fillEllipse (10.f, (28.f - 8.f) * 0.5f, 8.f, 8.f);
    }
}

void BandPopupPanel::resized()
{
    auto area = getLocalBounds().reduced (6, 0);

    // Header row (28px)
    auto header = area.removeFromTop (28);
    header.removeFromLeft (22); // dot space
    bandLabel.setBounds (header.removeFromLeft (80));
    closeBtn.setBounds  (header.removeFromRight (24).withSizeKeepingCentre (20, 18));
    bypassToggle.setBounds (header.removeFromRight (50).withSizeKeepingCentre (46, 18));

    area.removeFromTop (6);

    // Slider labels row (12px)
    auto lblRow = area.removeFromTop (12);
    int sw = (area.getWidth() - 4) / 3;
    freqLabel.setBounds (lblRow.removeFromLeft (sw));
    lblRow.removeFromLeft (2);
    gainLabel.setBounds (lblRow.removeFromLeft (sw));
    lblRow.removeFromLeft (2);
    qLabel.setBounds    (lblRow);

    // Rotary sliders row (70px — includes rotary + text box below)
    auto sliderRow = area.removeFromTop (70);
    freqSlider.setBounds (sliderRow.removeFromLeft (sw));
    sliderRow.removeFromLeft (2);
    gainSlider.setBounds (sliderRow.removeFromLeft (sw));
    sliderRow.removeFromLeft (2);
    qSlider.setBounds    (sliderRow);

    area.removeFromTop (6);

    // Type buttons row (24px)
    auto typeRow = area.removeFromTop (24);
    int tbW = typeRow.getWidth() / 7;
    for (auto& tb : typeBtn)
        tb.setBounds (typeRow.removeFromLeft (tbW));

    area.removeFromTop (4);
    auto modeRow = area.removeFromTop (24);
    modeLabel.setBounds (modeRow.removeFromLeft (38));
    int mbW = modeRow.getWidth() / 3;
    for (auto& mb : modeBtn)
        mb.setBounds (modeRow.removeFromLeft (mbW));

    if (showSlopeRow)
    {
        area.removeFromTop (4);
        auto slopeRow = area.removeFromTop (24);
        slopeLabel.setBounds (slopeRow.removeFromLeft (38));
        int sbW = slopeRow.getWidth() / 4;
        for (auto& sb : slopeBtn)
            sb.setBounds (slopeRow.removeFromLeft (sbW));
    }
}

//==============================================================================
// PresetModal
//==============================================================================
class PresetModal : public juce::Component,
                    public juce::ListBoxModel
{
public:
    std::function<void()> onClosed;

    explicit PresetModal (RVZNEQAudioProcessor& p) : processor (p)
    {
        setLookAndFeel (&laf);

        listBox.setModel (this);
        listBox.setRowHeight (26);
        listBox.setColour (juce::ListBox::backgroundColourId, juce::Colour (0xFF0E1018));
        listBox.setColour (juce::ListBox::outlineColourId,    RvznColours::borderMid);
        listBox.setOutlineThickness (1);
        addAndMakeVisible (listBox);

        nameEditor.setFont (juce::FontOptions (12.f));
        nameEditor.setColour (juce::TextEditor::backgroundColourId,      juce::Colour (0xFF1A1D27));
        nameEditor.setColour (juce::TextEditor::textColourId,            RvznColours::textPrimary);
        nameEditor.setColour (juce::TextEditor::outlineColourId,         RvznColours::borderMid);
        nameEditor.setColour (juce::TextEditor::focusedOutlineColourId,  RvznColours::accentBlue);
        nameEditor.setTextToShowWhenEmpty ("preset name...", RvznColours::textMuted);
        addAndMakeVisible (nameEditor);

        for (auto* b : { &saveBtn, &deleteBtn, &defaultBtn, &closeBtn })
        {
            b->getProperties().set ("style", "header");
            addAndMakeVisible (*b);
        }

        saveBtn.onClick = [this]
        {
            auto name = nameEditor.getText().trim();
            if (name.isEmpty()) return;
            processor.presets.savePreset (name);
            refresh();
        };

        deleteBtn.onClick = [this]
        {
            if (processor.presets.currentName.isEmpty()) return;
            processor.presets.deletePreset (processor.presets.currentName);
            nameEditor.clear();
            refresh();
        };

        defaultBtn.onClick = [this]
        {
            if (processor.presets.currentName.isEmpty()) return;
            processor.presets.setAsDefault (processor.presets.currentName);
            refresh();
        };

        closeBtn.onClick = [this]
        {
            setVisible (false);
            if (onClosed) onClosed();
        };

        setInterceptsMouseClicks (true, true);
        setWantsKeyboardFocus (true);
    }

    ~PresetModal() override { setLookAndFeel (nullptr); }

    void show()
    {
        refresh();
        setVisible (true);
        toFront (true);
        grabKeyboardFocus();
    }

    // ---- ListBoxModel ----
    int getNumRows() override { return names.size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
    {
        if (selected)
        {
            g.setColour (RvznColours::accentBlue.withAlpha (0.18f));
            g.fillRoundedRectangle (2.f, 1.f, (float)(w - 4), (float)(h - 2), 3.f);
        }

        bool isDefault = (names[row] == processor.presets.defaultPresetName());
        bool isCurrent = (names[row] == processor.presets.currentName);

        g.setFont (juce::FontOptions (11.f));
        g.setColour (isCurrent ? RvznColours::textPrimary : RvznColours::textMuted);
        g.drawText (names[row], 10, 0, w - 30, h, juce::Justification::centredLeft);

        if (isDefault)
        {
            g.setColour (RvznColours::accentOrange);
            g.drawText (juce::CharPointer_UTF8 ("\xe2\x98\x85"), w - 22, 0, 18, h, juce::Justification::centred);
        }
    }

    void listBoxItemClicked (int row, const juce::MouseEvent&) override
    {
        if (row < 0 || row >= names.size()) return;
        processor.presets.loadPreset (names[row]);
        nameEditor.setText (names[row], juce::dontSendNotification);
        updateButtonStates();
        listBox.repaintRow (row);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xBB000000));

        auto card = getCardBounds();
        g.setColour (RvznColours::background);
        g.fillRoundedRectangle (card.toFloat(), 12.f);
        g.setColour (RvznColours::border);
        g.drawRoundedRectangle (card.toFloat().reduced (0.5f), 12.f, 1.f);

        g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 11.f, juce::Font::plain));
        g.setColour (RvznColours::textMuted);
        g.drawText ("PRESETS", card.getX() + 16, card.getY(), 80, 36, juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto card  = getCardBounds();
        auto inner = card.reduced (14);
        inner.removeFromTop (22);   // title space

        closeBtn.setBounds (card.getRight() - 34, card.getY() + 5, 26, 26);

        auto btnRow  = inner.removeFromBottom (28);
        inner.removeFromBottom (6);
        auto nameRow = inner.removeFromBottom (26);
        inner.removeFromBottom (8);

        listBox.setBounds (inner);
        nameEditor.setBounds (nameRow);

        saveBtn.setBounds    (btnRow.removeFromLeft (60));
        btnRow.removeFromLeft (4);
        deleteBtn.setBounds  (btnRow.removeFromLeft (60));
        btnRow.removeFromLeft (4);
        defaultBtn.setBounds (btnRow.removeFromLeft (96));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (!getCardBounds().contains (e.getPosition()))
        {
            setVisible (false);
            if (onClosed) onClosed();
        }
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::escapeKey)
        {
            setVisible (false);
            if (onClosed) onClosed();
            return true;
        }
        if (k == juce::KeyPress::returnKey)
        {
            saveBtn.triggerClick();
            return true;
        }
        return false;
    }

private:
    RVZNEQAudioProcessor& processor;
    RvznLookAndFeel       laf;
    juce::StringArray     names;

    juce::ListBox    listBox { {}, this };
    juce::TextEditor nameEditor;
    juce::TextButton saveBtn    { "SAVE"    };
    juce::TextButton deleteBtn  { "DELETE"  };
    juce::TextButton defaultBtn { juce::CharPointer_UTF8 ("\xe2\x98\x85 DEFAULT") };
    juce::TextButton closeBtn   { "X" };

    juce::Rectangle<int> getCardBounds() const
    {
        const int w = 370, h = 360;
        return { (getWidth() - w) / 2, (getHeight() - h) / 2, w, h };
    }

    void refresh()
    {
        names = processor.presets.allPresetNames();
        listBox.updateContent();
        int idx = names.indexOf (processor.presets.currentName);
        if (idx >= 0)
        {
            listBox.selectRow (idx, true, false);
            nameEditor.setText (processor.presets.currentName, juce::dontSendNotification);
        }
        updateButtonStates();
        repaint();
    }

    void updateButtonStates()
    {
        bool has = processor.presets.currentName.isNotEmpty();
        deleteBtn.setEnabled (has);
        defaultBtn.setEnabled (has);
        bool isStar = has && processor.presets.currentName == processor.presets.defaultPresetName();
        defaultBtn.setToggleState (isStar, juce::dontSendNotification);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetModal)
};

//==============================================================================
// SettingsModal
//==============================================================================
class SettingsModal : public juce::Component,
                      public juce::ListBoxModel
{
public:
    std::function<void()> onClosed;

    SettingsModal()
    {
        setLookAndFeel (&laf);

        listBox.setModel (this);
        listBox.setRowHeight (28);
        listBox.setColour (juce::ListBox::backgroundColourId, juce::Colour (0xFF0E1018));
        listBox.setColour (juce::ListBox::outlineColourId,    RvznColours::borderMid);
        listBox.setOutlineThickness (1);
        addAndMakeVisible (listBox);

        closeBtn.setButtonText ("X");
        closeBtn.getProperties().set ("style", "header");
        closeBtn.onClick = [this]
        {
            setVisible (false);
            if (onClosed) onClosed();
        };
        addAndMakeVisible (closeBtn);

        setInterceptsMouseClicks (true, true);
        setWantsKeyboardFocus (true);
    }

    ~SettingsModal() override { setLookAndFeel (nullptr); }

    void show()
    {
        listBox.updateContent();
        listBox.selectRow (RvznSettings::getInstance().getThemeIndex(), true, true);
        setVisible (true);
        toFront (true);
        grabKeyboardFocus();
    }

    int getNumRows() override { return RvznSettings::getNumThemes(); }

    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
    {
        const auto& t = RvznSettings::getTheme (row);
        bool isCurrent = (row == RvznSettings::getInstance().getThemeIndex());

        if (selected)
        {
            g.setColour (RvznColours::accentBlue.withAlpha (0.18f));
            g.fillRoundedRectangle (2.f, 1.f, (float)(w - 4), (float)(h - 2), 3.f);
        }

        // Colour swatch — show 4 of the band colours stacked
        const float swatchX = 10.f, swatchW = 56.f;
        const float swatchH = (float) (h - 8);
        for (int i = 0; i < 4; ++i)
        {
            float sx = swatchX + i * (swatchW / 4.f);
            g.setColour (t.bandColours[i]);
            g.fillRect (sx, 4.f, swatchW / 4.f, swatchH);
        }

        // Background sample square
        g.setColour (t.background);
        g.fillRect (swatchX + swatchW + 6.f, 4.f, 14.f, swatchH);
        g.setColour (t.borderMid);
        g.drawRect (swatchX + swatchW + 6.f, 4.f, 14.f, swatchH, 1.f);

        // Theme name
        g.setFont (juce::FontOptions (12.f));
        g.setColour (isCurrent ? RvznColours::textPrimary : RvznColours::textMuted);
        g.drawText (t.name, (int) (swatchX + swatchW + 30.f), 0, w - (int)(swatchX + swatchW + 36.f), h,
                    juce::Justification::centredLeft);

        if (isCurrent)
        {
            g.setColour (RvznColours::accentOrange);
            g.drawText (juce::CharPointer_UTF8 ("\xe2\x97\x8f"), w - 22, 0, 18, h,
                        juce::Justification::centred);
        }
    }

    void listBoxItemClicked (int row, const juce::MouseEvent&) override
    {
        if (row >= 0 && row < RvznSettings::getNumThemes())
            RvznSettings::getInstance().setThemeIndex (row);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xBB000000));

        auto card = getCardBounds();
        g.setColour (RvznColours::background);
        g.fillRoundedRectangle (card.toFloat(), 12.f);
        g.setColour (RvznColours::border);
        g.drawRoundedRectangle (card.toFloat().reduced (0.5f), 12.f, 1.f);

        g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 11.f, juce::Font::plain));
        g.setColour (RvznColours::textMuted);
        g.drawText ("SETTINGS / THEME", card.getX() + 16, card.getY(), 200, 36,
                    juce::Justification::centredLeft);

        g.setFont (juce::FontOptions (9.f));
        g.setColour (RvznColours::textDim);
        g.drawText ("Theme is saved globally and applied to all new RVZN V1 EQ instances.",
                    card.getX() + 16, card.getBottom() - 28, card.getWidth() - 32, 18,
                    juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto card = getCardBounds();
        closeBtn.setBounds (card.getRight() - 34, card.getY() + 5, 26, 26);

        auto inner = card.reduced (14);
        inner.removeFromTop (22);
        inner.removeFromBottom (24);  // footer hint

        listBox.setBounds (inner);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (!getCardBounds().contains (e.getPosition()))
        {
            setVisible (false);
            if (onClosed) onClosed();
        }
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::escapeKey)
        {
            setVisible (false);
            if (onClosed) onClosed();
            return true;
        }
        return false;
    }

private:
    RvznLookAndFeel  laf;
    juce::ListBox    listBox { {}, nullptr };
    juce::TextButton closeBtn;

    juce::Rectangle<int> getCardBounds() const
    {
        const int w = 380, h = 380;
        return { (getWidth() - w) / 2, (getHeight() - h) / 2, w, h };
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SettingsModal)
};

//==============================================================================
// RVZNEQAudioProcessorEditor
//==============================================================================
RVZNEQAudioProcessorEditor::RVZNEQAudioProcessorEditor (RVZNEQAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p), curveComp (p)
{
    setLookAndFeel (&laf);

    // ---- Preset modal ----
    presetModal = std::make_unique<PresetModal> (p);
    presetModal->onClosed = [this] { presetsBtn.setToggleState (false, juce::dontSendNotification); };
    addChildComponent (*presetModal);

    presetsBtn.getProperties().set ("style", "header");
    presetsBtn.setClickingTogglesState (true);
    presetsBtn.setTooltip ("Manage presets — save, load, delete, set default");
    presetsBtn.onClick = [this]
    {
        if (presetsBtn.getToggleState())
        {
            presetModal->setBounds (getLocalBounds());
            presetModal->show();
        }
        else
        {
            presetModal->setVisible (false);
        }
    };
    addAndMakeVisible (presetsBtn);

    // ---- Settings (gear) button + modal ----
    settingsModal = std::make_unique<SettingsModal>();
    settingsModal->onClosed = [this] { settingsBtn.setToggleState (false, juce::dontSendNotification); };
    addChildComponent (*settingsModal);

    settingsBtn.setButtonText (juce::CharPointer_UTF8 ("\xe2\x9a\x99"));  // ⚙
    settingsBtn.getProperties().set ("style", "header");
    settingsBtn.setClickingTogglesState (true);
    settingsBtn.setTooltip ("Settings — change theme");
    settingsBtn.onClick = [this]
    {
        if (settingsBtn.getToggleState())
        {
            settingsModal->setBounds (getLocalBounds());
            settingsModal->show();
        }
        else
        {
            settingsModal->setVisible (false);
        }
    };
    addAndMakeVisible (settingsBtn);

    RvznSettings::getInstance().addChangeListener (this);

    // ---- Header buttons ----
    analyzerBtn.getProperties().set ("style", "header");
    analyzerBtn.setClickingTogglesState (true);
    analyzerBtn.setToggleState (true, juce::dontSendNotification);
    analyzerBtn.setTooltip ("Toggle real-time spectrum analyzer");
    analyzerBtn.onClick = [this] { curveComp.analyzerEnabled = analyzerBtn.getToggleState(); };
    addAndMakeVisible (analyzerBtn);

    bypassBtn.getProperties().set ("style", "bypass");
    bypassBtn.setClickingTogglesState (true);
    bypassBtn.setTooltip ("Bypass all EQ processing");
    bypassBtn.onClick = [this] {
        audioProcessor.globalBypassed.store (bypassBtn.getToggleState(), std::memory_order_relaxed);
    };
    addAndMakeVisible (bypassBtn);

    // ---- Footer meters ----
    addAndMakeVisible (inMeter);
    addAndMakeVisible (outMeter);

    addAndMakeVisible (curveComp);
    setSize (900, 480);
    setResizable (true, true);
    setResizeLimits (600, 320, 1600, 900);

    startTimerHz (60);
}

RVZNEQAudioProcessorEditor::~RVZNEQAudioProcessorEditor()
{
    RvznSettings::getInstance().removeChangeListener (this);
    stopTimer();
    setLookAndFeel (nullptr);
}

void RVZNEQAudioProcessorEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // Theme changed — repaint everything
    repaint();
    curveComp.repaint();
    if (presetModal != nullptr)  presetModal->repaint();
    if (settingsModal != nullptr) settingsModal->repaint();
}

void RVZNEQAudioProcessorEditor::timerCallback()
{
    inMeter.setLevelDb  (audioProcessor.inputLevelDb.load  (std::memory_order_relaxed));
    outMeter.setLevelDb (audioProcessor.outputLevelDb.load (std::memory_order_relaxed));
    inMeter.repaint();
    outMeter.repaint();
}


void RVZNEQAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (RvznColours::background);

    // Header
    g.setColour (RvznColours::header);
    g.fillRect (0, 0, getWidth(), 36);
    g.setColour (RvznColours::border);
    g.drawHorizontalLine (36, 0.f, (float)getWidth());

    // Footer
    g.setColour (RvznColours::header);
    g.fillRect (0, getHeight() - 30, getWidth(), 30);
    g.setColour (RvznColours::border);
    g.drawHorizontalLine (getHeight() - 30, 0.f, (float)getWidth());

    // Outer rounded border
    g.setColour (RvznColours::border);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 14.f, 1.f);

    // Logo text
    g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.f, juce::Font::plain));
    g.setColour (RvznColours::textPrimary);
    g.drawText ("RVZN", 14, 0, 38, 36, juce::Justification::centredLeft);
    g.setColour (RvznColours::accentBlue);
    g.drawText ("V1 EQ", 50, 0, 44, 36, juce::Justification::centredLeft);

    // Footer text  (inMeter: x=30..130  outMeter: x=W-220..W-120)
    g.setFont (juce::FontOptions (9.f));
    int fy = getHeight() - 30;
    g.setColour (RvznColours::textMuted);
    g.drawText ("IN",  8, fy, 22, 30, juce::Justification::centredLeft);
    g.drawText ("OUT", getWidth() - 258, fy, 36, 30, juce::Justification::centredLeft);
    int latencyCenter = (130 + getWidth() - 258) / 2;
    g.setColour (RvznColours::textDim);
    g.drawText ("LATENCY  0 ms", latencyCenter - 55, fy, 110, 30, juce::Justification::centred);
    g.setColour (RvznColours::textMuted);
    g.drawText ("PHASE", getWidth() - 116, fy, 44, 30, juce::Justification::centredLeft);
    g.setColour (RvznColours::accentBlue);
    g.drawText ("MINIMUM", getWidth() - 72, fy, 66, 30, juce::Justification::centredLeft);
}

void RVZNEQAudioProcessorEditor::resized()
{
    auto area   = getLocalBounds();
    auto header = area.removeFromTop (36).reduced (0, 7);
    area.removeFromBottom (30); // footer (painted only, meters positioned separately)

    // Logo area on left (matches the "RVZN V1 EQ" text painted at x=14)
    header.removeFromLeft (100);

    // Right: gear (settings) | PRESETS — matched 14px right margin to logo's left margin
    header.removeFromRight (14);
    settingsBtn.setBounds (header.removeFromRight (26));
    header.removeFromRight (4);
    presetsBtn.setBounds (header.removeFromRight (68));
    header.removeFromRight (6);

    // Left side of header: analyzer + bypass buttons
    analyzerBtn.setBounds (header.removeFromLeft (62));
    header.removeFromLeft (4);
    bypassBtn.setBounds   (header.removeFromLeft (50));

    // Keep modals covering full editor
    if (presetModal   != nullptr) presetModal->setBounds (getLocalBounds());
    if (settingsModal != nullptr) settingsModal->setBounds (getLocalBounds());

    // Footer meters: IN on left, OUT on right, labels painted separately
    auto footerBounds = getLocalBounds().removeFromBottom (30).reduced (0, 5);
    footerBounds.removeFromLeft  (30);
    inMeter.setBounds  (footerBounds.removeFromLeft  (100));
    footerBounds.removeFromRight (120);   // PHASE LINEAR area
    outMeter.setBounds (footerBounds.removeFromRight (100));

    curveComp.setBounds (area);
}
