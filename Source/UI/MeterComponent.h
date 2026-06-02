#pragma once
#include <JuceHeader.h>
#include "../LookAndFeel/RvznColours.h"

class MeterComponent : public juce::Component
{
public:
    void setLevelDb (float db)
    {
        targetDb = juce::jlimit (-60.f, 6.f, db);
    }

    void paint (juce::Graphics& g) override
    {
        // Ballistic: fast attack, slow release
        if (targetDb > smoothDb)
            smoothDb += (targetDb - smoothDb) * 0.5f;
        else
            smoothDb += (targetDb - smoothDb) * 0.08f;

        auto bounds = getLocalBounds().toFloat();

        // Track
        g.setColour (juce::Colour (0x12FFFFFF));
        g.fillRoundedRectangle (bounds, 2.f);

        // Bar fills left → right, -60..0 dBFS
        float t = juce::jlimit (0.f, 1.f, (smoothDb + 60.f) / 60.f);
        float fillW = bounds.getWidth() * t;
        if (fillW > 0.5f)
        {
            juce::Colour col = (smoothDb > -6.f) ? RvznColours::accentOrange : RvznColours::accentBlue;
            g.setColour (col.withAlpha (0.75f));
            g.fillRoundedRectangle (bounds.withWidth (fillW), 2.f);
        }

        // 0 dBFS tick at right edge
        g.setColour (juce::Colour (0x30FFFFFF));
        g.drawVerticalLine ((int)bounds.getRight() - 1, bounds.getY(), bounds.getBottom());
    }

private:
    float targetDb = -100.f, smoothDb = -100.f;
};
