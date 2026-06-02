#pragma once
#include <JuceHeader.h>

// Mutable so the theme system can update them at runtime.
// inline ensures one shared definition across all translation units (C++17).
namespace RvznColours
{
    inline juce::Colour background    { 0xFF0D0F14 };
    inline juce::Colour surface       { 0xFF13161F };
    inline juce::Colour header        { 0xFF111318 };
    inline juce::Colour border        { 0x0FFFFFFF };
    inline juce::Colour borderMid     { 0x1AFFFFFF };

    inline juce::Colour accentBlue    { 0xFF6B9FFF };
    inline juce::Colour accentSky     { 0xFF7DD3FC };
    inline juce::Colour accentViolet  { 0xFFA78BFA };
    inline juce::Colour accentPink    { 0xFFF472B6 };
    inline juce::Colour accentOrange  { 0xFFFB923C };
    inline juce::Colour accentEmerald { 0xFF34D399 };
    inline juce::Colour accentYellow  { 0xFFFACC15 };

    inline juce::Colour textPrimary   { 0xFFE2E4ED };
    inline juce::Colour textMuted     { 0xFF5A607A };
    inline juce::Colour textDim       { 0xFF3D4560 };

    inline juce::Colour gridLine      { 0x0AFFFFFF };
    inline juce::Colour curveBlue     { 0xFF6B9FFF };
    inline juce::Colour curveFill     { 0x1F6B9FFF };
    inline juce::Colour nodeBorder    { 0x40FFFFFF };
    inline juce::Colour bypass        { 0xFFFB923C };

    inline juce::Colour bandPalette[7] = {
        juce::Colour (0xFF6B9FFF),
        juce::Colour (0xFF7DD3FC),
        juce::Colour (0xFFA78BFA),
        juce::Colour (0xFFF472B6),
        juce::Colour (0xFFFB923C),
        juce::Colour (0xFF34D399),
        juce::Colour (0xFFFACC15),
    };

    inline juce::Colour bandColour (int b)
    {
        return bandPalette[((b % 7) + 7) % 7];
    }
}
