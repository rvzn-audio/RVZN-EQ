#include "PluginProcessor.h"
#include "PluginEditor.h"

static juce::String bandParamID (int b, const char* name)
{
    return juce::String ("band") + juce::String (b) + "_" + name;
}

static const char* slopeNames[] = { "6","12","18","24" };

static const char* typeTooltips[] = {
    "Bell - peak / dip at the centre frequency",
    "Low Shelf - boost or cut below the corner frequency",
    "High Shelf - boost or cut above the corner frequency",
    "Low Pass - passes frequencies below the corner",
    "High Pass - passes frequencies above the corner",
    "Notch - sharp cut at the centre frequency",
    "Band Pass - passes only frequencies around the centre"
};

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
    popup->onClose      = [this] { dismissPopup(); };
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
        buildCurvePaths();
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

    if (needRepaint)
        repaint();
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

void EQCurveComponent::buildCurvePaths()
{
    double sr = processor.currentSampleRate;

    using Coeffs = juce::dsp::IIR::Coefficients<float>;
    struct BandCoeffs
    {
        juce::ReferenceCountedArray<Coeffs> arr;
        bool  enabled  = false;
        int   mode     = Stereo;
        float postGain = 1.f;
    };
    std::array<BandCoeffs, NUM_BANDS> bc;

    bool anyMS = false;
    for (int b = 0; b < NUM_BANDS; ++b)
    {
        auto p = processor.getBandParams (b);
        bc[b].enabled  = p.enabled;
        bc[b].mode     = p.mode;
        bc[b].postGain = RVZNEQAudioProcessor::bandPostGain (p);
        if (!p.enabled) continue;
        bc[b].arr = RVZNEQAudioProcessor::buildBandCoefficients (p, sr);
        if (p.mode != Stereo) anyMS = true;
    }
    cachedHasMS = anyMS;

    const int numPoints = 256;
    cachedMidPath.clear();
    cachedSidePath.clear();
    cachedCurveXs.clear();
    cachedCurveYs.clear();
    cachedCurveSideYs.clear();
    cachedCurveXs.reserve     ((size_t)numPoints);
    cachedCurveYs.reserve     ((size_t)numPoints);
    cachedCurveSideYs.reserve ((size_t)numPoints);

    for (int i = 0; i < numPoints; ++i)
    {
        float t    = (float)i / (float)(numPoints - 1);
        float freq = minFreq * std::pow (maxFreq / minFreq, t);
        float x    = getXForFreq (freq);

        double magM = 1.0, magS = 1.0;
        for (int b = 0; b < NUM_BANDS; ++b)
        {
            if (!bc[b].enabled) continue;
            double bandMag = (double) bc[b].postGain;
            for (auto* c : bc[b].arr)
                bandMag *= c->getMagnitudeForFrequency ((double)freq, sr);

            // Stereo affects both M and S; Mid only M; Side only S.
            if (bc[b].mode == Stereo || bc[b].mode == Mid)  magM *= bandMag;
            if (bc[b].mode == Stereo || bc[b].mode == Side) magS *= bandMag;
        }

        float yM = getYForGain ((float)juce::Decibels::gainToDecibels (magM, -60.0));
        float yS = getYForGain ((float)juce::Decibels::gainToDecibels (magS, -60.0));

        cachedCurveXs.push_back (x);
        cachedCurveYs.push_back (yM);
        cachedCurveSideYs.push_back (yS);

        if (i == 0) { cachedMidPath.startNewSubPath  (x, yM); cachedSidePath.startNewSubPath (x, yS); }
        else        { cachedMidPath.lineTo (x, yM);            cachedSidePath.lineTo (x, yS); }
    }
}

void EQCurveComponent::drawPerBandCurves (juce::Graphics& g)
{
    double sr = processor.currentSampleRate;

    for (int b = 0; b < NUM_BANDS; ++b)
    {
        auto p = processor.getBandParams (b);
        if (!p.enabled) continue;

        auto arr = RVZNEQAudioProcessor::buildBandCoefficients (p, sr);
        const double postGain = (double) RVZNEQAudioProcessor::bandPostGain (p);

        const int numPoints = 256;
        juce::Path bandPath;
        bool started = false;

        for (int i = 0; i < numPoints; ++i)
        {
            float t    = (float)i / (float)(numPoints - 1);
            float freq = minFreq * std::pow (maxFreq / minFreq, t);
            float x    = getXForFreq (freq);

            double mag = postGain;
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
    const float w     = (float) getWidth();
    const float yZero = getYForGain (0.f);

    if (! cachedHasMS)
    {
        // No M/S processing — render the single combined curve as before.
        juce::Path fill = cachedMidPath;
        fill.lineTo (w, yZero);
        fill.lineTo (0.f, yZero);
        fill.closeSubPath();
        g.setColour (RvznColours::curveFill);
        g.fillPath (fill);
        g.setColour (RvznColours::curveBlue);
        g.strokePath (cachedMidPath, juce::PathStrokeType (1.5f));
        return;
    }

    // M/S mode active — render both curves so it's visible which channels
    // are being affected.
    const auto midCol  = RvznColours::curveBlue;
    const auto sideCol = RvznColours::accentEmerald;

    // Mid: filled to baseline (subtle), then stroked.
    juce::Path midFill = cachedMidPath;
    midFill.lineTo (w, yZero);
    midFill.lineTo (0.f, yZero);
    midFill.closeSubPath();
    g.setColour (midCol.withAlpha (0.10f));
    g.fillPath (midFill);

    // Side: dashed stroke so it visually reads as a different signal path.
    g.setColour (sideCol.withAlpha (0.85f));
    {
        juce::Path dashed;
        const float dashLengths[] = { 5.f, 3.f };
        juce::PathStrokeType (1.4f).createDashedStroke (dashed, cachedSidePath, dashLengths, 2);
        g.fillPath (dashed);
    }

    // Mid stroke on top so it stays prominent.
    g.setColour (midCol);
    g.strokePath (cachedMidPath, juce::PathStrokeType (1.5f));
}

void EQCurveComponent::drawMSLegend (juce::Graphics& g)
{
    if (! cachedHasMS) return;

    const int   pad  = 8;
    const int   w    = 96, h = 18;
    const int   x    = getWidth() - w - pad;
    const int   y    = pad;

    g.setColour (juce::Colour (0xCC0D0F14));
    g.fillRoundedRectangle ((float)x, (float)y, (float)w, (float)h, 4.f);
    g.setColour (RvznColours::borderMid);
    g.drawRoundedRectangle ((float)x, (float)y, (float)w, (float)h, 4.f, 1.f);

    g.setFont (juce::FontOptions (9.f));

    // Mid swatch — short solid line
    const int sy = y + h / 2;
    g.setColour (RvznColours::curveBlue);
    g.drawLine ((float)(x + 8), (float)sy, (float)(x + 22), (float)sy, 1.6f);
    g.setColour (RvznColours::textPrimary);
    g.drawText ("MID", x + 26, y, 24, h, juce::Justification::centredLeft);

    // Side swatch — dashed
    const float dx0 = (float)(x + 54), dx1 = (float)(x + 68);
    g.setColour (RvznColours::accentEmerald);
    const float dashes[] = { 3.f, 2.f };
    g.drawDashedLine ({ { dx0, (float)sy }, { dx1, (float)sy } }, dashes, 2, 1.4f);
    g.setColour (RvznColours::textPrimary);
    g.drawText ("SIDE", x + 72, y, 24, h, juce::Justification::centredLeft);
}

void EQCurveComponent::drawNodes (juce::Graphics& g)
{
    for (int b = 0; b < NUM_BANDS; ++b)
    {
        auto p = processor.getBandParams (b);
        if (!p.enabled) continue;

        bool sel     = (b == selectedBand);
        bool inGroup = isBandSelected (b);
        bool hovered = (b == hoveredBand);
        bool pinnedY = (p.type == Notch || p.type == BandPass);

        float x   = getXForFreq (p.freq);
        float y   = pinnedY ? getYForGain (0.f) : getYForGain (p.gainDb);
        float r   = (sel || inGroup) ? 8.f : 6.f;
        juce::Colour col = RvznColours::bandColour (b);

        // Q indicator ring — shown when band is selected (singular or in group)
        if (sel || inGroup)
        {
            float qR = juce::jmap (p.q, 0.1f, 18.f, 18.f, 55.f);
            g.setColour (col.withAlpha (0.12f));
            g.drawEllipse (x - qR, y - qR * 0.4f, qR * 2.f, qR * 0.8f, 1.f);
        }

        // Selection ring for bands in the multi-selection group
        if (inGroup && !sel)
        {
            const float gr = 11.f;
            g.setColour (col.withAlpha (0.5f));
            g.drawEllipse (x - gr, y - gr, gr * 2.f, gr * 2.f, 1.2f);
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

        // M / S badge — small pill above the node indicating the band only
        // processes one channel of the M/S split.
        if (p.mode == Mid || p.mode == Side)
        {
            const char* lbl = (p.mode == Mid) ? "M" : "S";
            juce::Colour badgeCol = (p.mode == Mid)
                ? RvznColours::curveBlue
                : RvznColours::accentEmerald;

            const float bw = 12.f, bh = 9.f;
            const float bx = x - bw * 0.5f;
            const float by = y - r - bh - 2.f;

            g.setColour (badgeCol.withAlpha (0.92f));
            g.fillRoundedRectangle (bx, by, bw, bh, 2.f);
            g.setColour (juce::Colours::white);
            g.setFont (juce::FontOptions (7.5f, juce::Font::bold));
            g.drawText (lbl, (int) bx, (int) by, (int) bw, (int) bh,
                        juce::Justification::centred);
        }
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
    drawMSLegend (g);
    drawLasso (g);
}

void EQCurveComponent::drawLasso (juce::Graphics& g)
{
    if (! isLassoing) return;
    float x = std::min (lassoStart.x, lassoCurrent.x);
    float y = std::min (lassoStart.y, lassoCurrent.y);
    float w = std::abs (lassoCurrent.x - lassoStart.x);
    float h = std::abs (lassoCurrent.y - lassoStart.y);
    g.setColour (RvznColours::accentBlue.withAlpha (0.10f));
    g.fillRect (x, y, w, h);
    g.setColour (RvznColours::accentBlue.withAlpha (0.7f));
    g.drawRect (x, y, w, h, 1.f);
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
        bool pinnedY = (p.type == Notch || p.type == BandPass);
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

void EQCurveComponent::addBandAt (float freq, float gainDb, FilterType type)
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
    setP ("gain",  (type == Bell || type == LowShelf || type == HighShelf) ? gainDb : 0.f);
    setP ("q",     1.f);
    setP ("type",  (float) type);
    setP ("slope", 1.f);   // 12 dB/oct
    setP ("mode",  (float) Stereo);

    auto* en = processor.apvts.getParameter (bandParamID (b, "enabled"));
    if (en) en->setValueNotifyingHost (1.f);
}

bool EQCurveComponent::isBandSelected (int b) const
{
    return std::find (selectedBands.begin(), selectedBands.end(), b) != selectedBands.end();
}

void EQCurveComponent::selectOnly (int b)
{
    selectedBands.clear();
    if (b >= 0) selectedBands.push_back (b);
    selectedBand = b;
}

void EQCurveComponent::clearSelection()
{
    selectedBands.clear();
    selectedBand = -1;
}

void EQCurveComponent::removeBand (int b)
{
    auto* en = processor.apvts.getParameter (bandParamID (b, "enabled"));
    if (en) en->setValueNotifyingHost (0.f);
    auto* gainParam = processor.apvts.getParameter (bandParamID (b, "gain"));
    if (gainParam) gainParam->setValueNotifyingHost (gainParam->convertTo0to1 (0.f));

    selectedBands.erase (std::remove (selectedBands.begin(), selectedBands.end(), b),
                        selectedBands.end());
    if (selectedBand == b) selectedBand = -1;
    dismissPopup();
    repaint();
}

void EQCurveComponent::showPopupForBand (int band)
{
    auto p = processor.getBandParams (band);
    bool pinnedY = (p.type == Notch || p.type == BandPass);
    float nx = getXForFreq (p.freq);
    float ny = pinnedY ? getYForGain (0.f) : getYForGain (p.gainDb);

    popup->userMoved = false;  // opening for a band always re-positions automatically
    popup->setAlpha (1.0f);
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
    if (popup->userMoved) return;   // user has placed the panel manually — leave it alone
    int b = popup->currentBand;
    auto p = processor.getBandParams (b);
    bool pinnedY = (p.type == Notch || p.type == BandPass);
    float nx = getXForFreq (p.freq);
    float ny = pinnedY ? getYForGain (0.f) : getYForGain (p.gainDb);
    popup->showForBand (b, { (int)nx, (int)ny });
}

void EQCurveComponent::mouseMove (const juce::MouseEvent& e)
{
    float mx = (float)e.x, my = (float)e.y;

    int newHovered = findHitNode (mx, my);
    if (newHovered != hoveredBand)
    {
        hoveredBand = newHovered;
        repaint();
    }
}

void EQCurveComponent::mouseExit (const juce::MouseEvent&)
{
    if (hoveredBand != -1)
    {
        hoveredBand = -1;
        repaint();
    }
}

void EQCurveComponent::mouseDown (const juce::MouseEvent& e)
{
    float mx = (float)e.x, my = (float)e.y;

    int hit = findHitNode (mx, my);

    if (hit >= 0)
    {
        // If the band is already part of the multi-selection, drag the whole
        // group. Otherwise, replace the selection with just this band.
        if (! isBandSelected (hit))
            selectOnly (hit);
        else
            selectedBand = hit;

        draggingBand   = hit;
        isDragging     = true;
        dragStartMouse = { mx, my };

        multiDragRefs.clear();
        for (int b : selectedBands)
        {
            auto p = processor.getBandParams (b);
            multiDragRefs.push_back ({ b, p.freq, p.gainDb });
            if (b == hit) { dragStartFreq = p.freq; dragStartGain = p.gainDb; }
        }
        repaint();
        return;
    }

    // Empty area: dismiss popup, clear selection, begin lasso
    if (popup->isVisible())
        dismissPopup();

    if (! e.mods.isRightButtonDown())
    {
        clearSelection();
        isLassoing   = true;
        lassoStart   = { mx, my };
        lassoCurrent = { mx, my };
        repaint();
    }
}

void EQCurveComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (isLassoing)
    {
        lassoCurrent = { (float)e.x, (float)e.y };
        repaint();
        return;
    }

    if (!isDragging || draggingBand < 0) return;

    float dx = (float)(e.x - dragStartMouse.x);
    float dy = (float)(e.y - dragStartMouse.y);
    float w  = (float)getWidth();
    float h  = (float)getHeight();

    // Compute the normalized-frequency delta from the primary dragged band,
    // then apply the same delta to every band in the multi-drag group so the
    // group keeps its relative spacing.
    const float logRange = std::log (maxFreq / minFreq);
    const float startNormX = std::log (dragStartFreq / minFreq) / logRange;
    const float desiredNormX = startNormX + dx / w;
    const float gainDelta = -dy * (maxGain - minGain) / h;

    auto setParam = [&] (int band, const char* name, float v) {
        auto* param = processor.apvts.getParameter (bandParamID (band, name));
        if (param) param->setValueNotifyingHost (param->convertTo0to1 (v));
    };

    // If any band would clip the freq range, clamp the whole group's normX
    // shift so spacing is preserved.
    float clampedNormDelta = desiredNormX - startNormX;
    for (const auto& ref : multiDragRefs)
    {
        float refNormX = std::log (ref.startFreq / minFreq) / logRange;
        float newNormX = refNormX + clampedNormDelta;
        if (newNormX < 0.f) clampedNormDelta -= newNormX;
        if (newNormX > 1.f) clampedNormDelta -= (newNormX - 1.f);
    }

    float clampedGainDelta = gainDelta;
    for (const auto& ref : multiDragRefs)
    {
        float newG = ref.startGain + clampedGainDelta;
        if (newG < minGain) clampedGainDelta += (minGain - newG);
        if (newG > maxGain) clampedGainDelta -= (newG - maxGain);
    }

    for (const auto& ref : multiDragRefs)
    {
        float refNormX = std::log (ref.startFreq / minFreq) / logRange;
        float newNormX = juce::jlimit (0.f, 1.f, refNormX + clampedNormDelta);
        float newFreq  = minFreq * std::pow (maxFreq / minFreq, newNormX);
        float newGain  = juce::jlimit (minGain, maxGain, ref.startGain + clampedGainDelta);
        setParam (ref.band, "freq", newFreq);
        setParam (ref.band, "gain", newGain);
    }

    updatePopupPosition();
}

void EQCurveComponent::mouseUp (const juce::MouseEvent&)
{
    if (isLassoing)
    {
        float x0 = std::min (lassoStart.x, lassoCurrent.x);
        float y0 = std::min (lassoStart.y, lassoCurrent.y);
        float x1 = std::max (lassoStart.x, lassoCurrent.x);
        float y1 = std::max (lassoStart.y, lassoCurrent.y);

        // Only treat as a real lasso if the user actually dragged.
        if ((x1 - x0) > 3.f || (y1 - y0) > 3.f)
        {
            selectedBands.clear();
            for (int b = 0; b < NUM_BANDS; ++b)
            {
                auto p = processor.getBandParams (b);
                if (!p.enabled) continue;
                bool pinnedY = (p.type == Notch || p.type == BandPass);
                float nx = getXForFreq (p.freq);
                float ny = pinnedY ? getYForGain (0.f) : getYForGain (p.gainDb);
                if (nx >= x0 && nx <= x1 && ny >= y0 && ny <= y1)
                    selectedBands.push_back (b);
            }
            selectedBand = selectedBands.empty() ? -1 : selectedBands.front();
        }

        isLassoing = false;
        repaint();
    }

    isDragging   = false;
    draggingBand = -1;
    multiDragRefs.clear();
}

void EQCurveComponent::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    // Q is changed by hovering a node and scrolling — resolve hit from the
    // current cursor position rather than the cached hover band so a fast
    // move-then-scroll still hits the right node.
    int hit = findHitNode ((float)e.x, (float)e.y);
    if (hit < 0) return;
    auto* p = processor.apvts.getParameter (bandParamID (hit, "q"));
    if (p) p->setValueNotifyingHost (juce::jlimit (0.f, 1.f, p->getValue() + w.deltaY * 0.06f));
}

void EQCurveComponent::mouseDoubleClick (const juce::MouseEvent& e)
{
    const float mx = (float)e.x, my = (float)e.y;

    int hit = findHitNode (mx, my);
    if (hit >= 0)
    {
        // Double-click on a node → open the band popup.
        selectOnly (hit);
        showPopupForBand (hit);
        repaint();
        return;
    }

    // Double-click on empty area → add a band at the cursor position.
    const float clickFreq = juce::jlimit (minFreq, maxFreq, getFreqForX (mx));
    const float clickGain = juce::jlimit (minGain, maxGain, getGainForY (my));

    const bool inLowCut  = clickFreq < 50.f;
    const bool inHighCut = clickFreq > 18000.f;

    if (inLowCut)       addBandAt (50.f,    0.f, HighPass);
    else if (inHighCut) addBandAt (18000.f, 0.f, LowPass);
    else                addBandAt (clickFreq, clickGain, Bell);
}

//==============================================================================
// BandPopupPanel
//==============================================================================
BandPopupPanel::BandPopupPanel (RVZNEQAudioProcessor& p) : processor (p)
{
    setLookAndFeel (&laf);

    // Band label — non-interactive so the header drag passes through it
    bandLabel.setFont (juce::FontOptions (10.f));
    bandLabel.setColour (juce::Label::textColourId, RvznColours::textPrimary);
    bandLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (bandLabel);

    // Close button (×) — just hides the panel, keeps the band
    closeBtn.setButtonText (juce::CharPointer_UTF8 ("\xc3\x97")); // ×
    closeBtn.getProperties().set ("style", "header");
    closeBtn.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (closeBtn);

    // Delete button — removes the band entirely
    deleteBtn.setButtonText ("DEL");
    deleteBtn.getProperties().set ("style", "header");
    deleteBtn.onClick = [this] { if (onRemoveBand) onRemoveBand (currentBand); };
    addAndMakeVisible (deleteBtn);

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
    gainSlider.setTooltip ("Gain (dB) — Bell, Shelf, and low/high cut passband level");
    qSlider.setTooltip    ("Q / resonance / steepness");
    bypassToggle.setTooltip ("Enable / bypass this band");
    closeBtn.setTooltip     ("Close this panel");
    deleteBtn.setTooltip    ("Delete this band");

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

    // Type buttons — drawn as filter-shape icons (see RvznLookAndFeel::drawFilterIcon)
    for (int i = 0; i < 7; ++i)
    {
        typeBtn[i].setButtonText ({});
        typeBtn[i].getProperties().set ("style", "filterType");
        typeBtn[i].getProperties().set ("iconType", (juce::int64) i);
        typeBtn[i].getProperties().set ("accentColour", (juce::int64)RvznColours::accentBlue.getARGB());
        typeBtn[i].setClickingTogglesState (true);
        typeBtn[i].setTooltip (typeTooltips[i]);
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

    // Drag-handle dots — visual hint that the header is grabbable
    {
        const float cx = b.getWidth() * 0.5f;
        const float cy = 14.f;
        const float r  = 1.3f;
        const float sp = 4.f;
        g.setColour (RvznColours::textDim);
        for (int i = -1; i <= 1; ++i)
            for (int j = 0; j < 2; ++j)
                g.fillEllipse (cx + i * sp - r,
                               cy + (j == 0 ? -2.f : 2.f) - r,
                               r * 2.f, r * 2.f);
    }
}

void BandPopupPanel::resized()
{
    auto area = getLocalBounds().reduced (6, 0);

    // Header row (28px)
    auto header = area.removeFromTop (28);
    header.removeFromLeft (22); // dot space
    bandLabel.setBounds (header.removeFromLeft (50));
    deleteBtn.setBounds (header.removeFromLeft (32).withSizeKeepingCentre (30, 18));
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

void BandPopupPanel::mouseDown (const juce::MouseEvent& e)
{
    // Only initiate drag from the header strip.
    if (e.y < kHeaderH)
    {
        isDraggingPanel   = true;
        dragStartPanelPos = getBounds().getTopLeft();
        dragStartMousePos = e.getEventRelativeTo (getParentComponent()).getPosition();
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    }
}

void BandPopupPanel::mouseDrag (const juce::MouseEvent& e)
{
    if (! isDraggingPanel) return;

    auto cur   = e.getEventRelativeTo (getParentComponent()).getPosition();
    auto delta = cur - dragStartMousePos;

    auto* par = getParentComponent();
    int pw = par ? par->getWidth()  : 800;
    int ph = par ? par->getHeight() : 500;

    int nx = juce::jlimit (4, pw - getWidth()  - 4, dragStartPanelPos.x + delta.x);
    int ny = juce::jlimit (4, ph - getHeight() - 4, dragStartPanelPos.y + delta.y);
    setTopLeftPosition (nx, ny);
    userMoved = true;
}

void BandPopupPanel::mouseUp (const juce::MouseEvent&)
{
    isDraggingPanel = false;
    setMouseCursor (juce::MouseCursor::NormalCursor);
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

    // License gate. Created last so it sits on top of all other children.
    licenseManager = std::make_unique<rvzn::license::LicenseManager> (
        rvzn::license::Config::productCode,
        rvzn::license::Config::productName,
        rvzn::license::Config::verifyUrl,
        rvzn::license::Config::deactivateUrl,
        rvzn::license::Config::offlineGraceLaunches);
    licenseGate = std::make_unique<rvzn::license::LicenseGate> (*licenseManager);
    addAndMakeVisible (*licenseGate);
    licenseGate->setBounds (getLocalBounds());
    licenseGate->toFront (false);
    licenseManager->startupVerify();
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
    g.drawText ("V1 EQUALIZER", 50, 0, 240, 36, juce::Justification::centredLeft);

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
    header.removeFromLeft (150);

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
    if (licenseGate   != nullptr) licenseGate->setBounds  (getLocalBounds());

    // Footer meters: IN on left, OUT on right, labels painted separately
    auto footerBounds = getLocalBounds().removeFromBottom (30).reduced (0, 5);
    footerBounds.removeFromLeft  (30);
    inMeter.setBounds  (footerBounds.removeFromLeft  (100));
    footerBounds.removeFromRight (120);   // PHASE LINEAR area
    outMeter.setBounds (footerBounds.removeFromRight (100));

    curveComp.setBounds (area);
}
