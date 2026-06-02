#pragma once
#include <JuceHeader.h>
#include "RvznColours.h"

//==============================================================================
// Animated toggle button with a pill-style thumb
//==============================================================================
class RvznToggleButton : public juce::ToggleButton, private juce::Timer
{
public:
    float thumbX = 0.0f;
    juce::Colour accentColour = RvznColours::accentBlue;

    RvznToggleButton()
    {
        startTimerHz (60);
    }

    ~RvznToggleButton() override
    {
        stopTimer();
    }

private:
    void timerCallback() override
    {
        float target = getToggleState() ? 1.0f : 0.0f;
        float delta  = (target - thumbX) * 0.18f;
        if (std::abs (delta) > 0.001f)
        {
            thumbX += delta;
            repaint();
        }
    }
};

//==============================================================================
// LookAndFeel
//==============================================================================
class RvznLookAndFeel : public juce::LookAndFeel_V4
{
public:
    RvznLookAndFeel() = default;

    //--------------------------------------------------------------------------
    void drawRotarySlider (juce::Graphics& g,
                           int x, int y, int width, int height,
                           float sliderPosProportional,
                           float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider& slider) override
    {
        // Resolve accent colour from property or fallback
        juce::Colour accent = RvznColours::accentBlue;
        auto& props = slider.getProperties();
        if (props.contains ("accentColour"))
        {
            auto v = props["accentColour"];
            if (v.isInt() || v.isInt64())
                accent = juce::Colour (static_cast<juce::uint32> (static_cast<juce::int64> (v)));
        }

        float cx = (float)x + (float)width  * 0.5f;
        float cy = (float)y + (float)height * 0.5f;
        float radius = juce::jmin ((float)width, (float)height) * 0.5f - 4.f;

        // Track arc
        {
            juce::Path track;
            track.addArc (cx - radius, cy - radius, radius * 2.f, radius * 2.f,
                          rotaryStartAngle, rotaryEndAngle, true);
            g.setColour (accent.withAlpha (0.15f));
            g.strokePath (track, juce::PathStrokeType (2.5f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // Progress arc
        {
            float currentAngle = rotaryStartAngle
                + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);
            juce::Path progress;
            progress.addArc (cx - radius, cy - radius, radius * 2.f, radius * 2.f,
                             rotaryStartAngle, currentAngle, true);
            g.setColour (accent);
            g.strokePath (progress, juce::PathStrokeType (2.5f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // Centre dot
        const float dotR = 5.f;
        g.setColour (juce::Colour (0xFF1A1E2A));
        g.fillEllipse (cx - dotR, cy - dotR, dotR * 2.f, dotR * 2.f);
        g.setColour (juce::Colour (0x1FFFFFFF));
        g.drawEllipse (cx - dotR, cy - dotR, dotR * 2.f, dotR * 2.f, 1.f);
    }

    //--------------------------------------------------------------------------
    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                           bool /*shouldDrawButtonAsHighlighted*/,
                           bool /*shouldDrawButtonAsDown*/) override
    {
        // Try to cast to RvznToggleButton to get animated thumbX
        float tx = button.getToggleState() ? 1.0f : 0.0f;
        juce::Colour accent = RvznColours::accentBlue;

        if (auto* rtb = dynamic_cast<RvznToggleButton*> (&button))
        {
            tx     = rtb->thumbX;
            accent = rtb->accentColour;
        }

        // Pill dims: 28 x 15, centred vertically
        const float pillW = 28.f, pillH = 15.f, pillR = 7.5f;
        float pillX = 2.f;
        float pillY = ((float)button.getHeight() - pillH) * 0.5f;

        // Pill background
        g.setColour (juce::Colour (0xFF0D0F14));
        g.fillRoundedRectangle (pillX, pillY, pillW, pillH, pillR);
        g.setColour (RvznColours::border);
        g.drawRoundedRectangle (pillX, pillY, pillW, pillH, pillR, 1.f);

        // Thumb
        const float thumbDia = 11.f;
        float thumbMinX = pillX + 2.f;
        float thumbMaxX = pillX + pillW - 2.f - thumbDia;
        float thumbPosX = thumbMinX + tx * (thumbMaxX - thumbMinX);
        float thumbPosY = pillY + (pillH - thumbDia) * 0.5f;

        juce::Colour thumbCol = (tx > 0.5f) ? accent : juce::Colour (0xFF2A2E3A);
        g.setColour (thumbCol);
        g.fillEllipse (thumbPosX, thumbPosY, thumbDia, thumbDia);

        // Optional text label to the right of pill
        juce::String txt = button.getButtonText();
        if (txt.isNotEmpty())
        {
            g.setFont (juce::FontOptions (9.f));
            g.setColour (RvznColours::textMuted);
            g.drawText (txt, (int)(pillX + pillW + 4), 0,
                        button.getWidth() - (int)(pillX + pillW + 4), button.getHeight(),
                        juce::Justification::centredLeft);
        }
    }

    //--------------------------------------------------------------------------
    void drawButtonBackground (juce::Graphics& g, juce::Button& button,
                               const juce::Colour& /*backgroundColour*/,
                               bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown) override
    {
        auto bounds = button.getLocalBounds().toFloat();
        bool active = button.getToggleState() || shouldDrawButtonAsDown;

        juce::String style;
        auto& props = button.getProperties();
        if (props.contains ("style"))
            style = props["style"].toString();

        juce::Colour accent = RvznColours::accentBlue;
        if (props.contains ("accentColour"))
        {
            auto v = props["accentColour"];
            if (v.isInt() || v.isInt64())
                accent = juce::Colour (static_cast<juce::uint32> (static_cast<juce::int64> (v)));
        }

        if (style == "filterType")
        {
            if (active)
            {
                g.setColour (accent.withAlpha (0.12f));
                g.fillRoundedRectangle (bounds, 5.f);
            }
            else
            {
                g.setColour (juce::Colours::transparentBlack);
                g.fillRoundedRectangle (bounds, 5.f);
            }
        }
        else if (style == "bypass")
        {
            if (active)
            {
                g.setColour (RvznColours::bypass.withAlpha (0.15f));
                g.fillRoundedRectangle (bounds, 5.f);
                g.setColour (RvznColours::bypass.withAlpha (0.40f));
                g.drawRoundedRectangle (bounds.reduced (0.5f), 5.f, 1.f);
            }
            else
            {
                g.setColour (juce::Colour (0x0AFFFFFF));
                g.fillRoundedRectangle (bounds, 5.f);
                g.setColour (juce::Colour (0x14FFFFFF));
                g.drawRoundedRectangle (bounds.reduced (0.5f), 5.f, 1.f);
            }
        }
        else // "header" or default
        {
            if (active)
            {
                g.setColour (RvznColours::accentBlue.withAlpha (0.10f));
                g.fillRoundedRectangle (bounds, 5.f);
                g.setColour (RvznColours::accentBlue.withAlpha (0.30f));
                g.drawRoundedRectangle (bounds.reduced (0.5f), 5.f, 1.f);
            }
            else
            {
                juce::Colour fill = shouldDrawButtonAsHighlighted
                    ? juce::Colour (0x0DFFFFFF)
                    : juce::Colour (0x0AFFFFFF);
                g.setColour (fill);
                g.fillRoundedRectangle (bounds, 5.f);
                g.setColour (juce::Colour (0x14FFFFFF));
                g.drawRoundedRectangle (bounds.reduced (0.5f), 5.f, 1.f);
            }
        }
    }

    //--------------------------------------------------------------------------
    void drawButtonText (juce::Graphics& g, juce::TextButton& button,
                         bool /*shouldDrawButtonAsHighlighted*/,
                         bool /*shouldDrawButtonAsDown*/) override
    {
        bool active = button.getToggleState();

        juce::String style;
        auto& props = button.getProperties();
        if (props.contains ("style"))
            style = props["style"].toString();

        juce::Colour accent = RvznColours::accentBlue;
        if (props.contains ("accentColour"))
        {
            auto v = props["accentColour"];
            if (v.isInt() || v.isInt64())
                accent = juce::Colour (static_cast<juce::uint32> (static_cast<juce::int64> (v)));
        }

        juce::Colour textCol;
        if (style == "filterType")
            textCol = active ? accent : RvznColours::textMuted;
        else
            textCol = active ? RvznColours::textPrimary : RvznColours::textMuted;

        g.setFont (juce::FontOptions (9.5f));
        g.setColour (textCol);
        g.drawText (button.getButtonText(), button.getLocalBounds(),
                    juce::Justification::centred, true);
    }
};
