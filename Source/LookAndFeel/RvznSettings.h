#pragma once
#include <JuceHeader.h>
#include "RvznColours.h"

struct RvznTheme
{
    const char* name;

    juce::Colour background, surface, header, border, borderMid;
    juce::Colour textPrimary, textMuted, textDim;
    juce::Colour gridLine, nodeBorder;
    juce::Colour accentPrimary;      // becomes accentBlue / curveBlue
    juce::Colour accentSecondary;    // becomes accentOrange / bypass
    juce::Colour bandColours[7];
};

namespace RvznThemesData
{
    // 10 hand-picked themes. Index 0 is the original default.
    inline const RvznTheme themes[] =
    {
        // 0 — MIDNIGHT (default)
        {
            "Midnight",
            juce::Colour (0xFF0D0F14), juce::Colour (0xFF13161F), juce::Colour (0xFF111318),
            juce::Colour (0x0FFFFFFF), juce::Colour (0x1AFFFFFF),
            juce::Colour (0xFFE2E4ED), juce::Colour (0xFF5A607A), juce::Colour (0xFF3D4560),
            juce::Colour (0x0AFFFFFF), juce::Colour (0x40FFFFFF),
            juce::Colour (0xFF6B9FFF), juce::Colour (0xFFFB923C),
            { juce::Colour (0xFF6B9FFF), juce::Colour (0xFF7DD3FC), juce::Colour (0xFFA78BFA),
              juce::Colour (0xFFF472B6), juce::Colour (0xFFFB923C), juce::Colour (0xFF34D399),
              juce::Colour (0xFFFACC15) }
        },
        // 1 — CYBER
        {
            "Cyber",
            juce::Colour (0xFF08070D), juce::Colour (0xFF11101A), juce::Colour (0xFF0B0A12),
            juce::Colour (0x14FF00FF), juce::Colour (0x22FF00FF),
            juce::Colour (0xFFE8E0FF), juce::Colour (0xFF7B6F95), juce::Colour (0xFF463D5C),
            juce::Colour (0x0AFF00FF), juce::Colour (0x40FF66FF),
            juce::Colour (0xFFFF36C8), juce::Colour (0xFF00F5FF),
            { juce::Colour (0xFFFF36C8), juce::Colour (0xFF00F5FF), juce::Colour (0xFFB94BFF),
              juce::Colour (0xFFFFD23F), juce::Colour (0xFFFF6B6B), juce::Colour (0xFF50FA7B),
              juce::Colour (0xFFFFAA00) }
        },
        // 2 — FOREST
        {
            "Forest",
            juce::Colour (0xFF0A140E), juce::Colour (0xFF111E16), juce::Colour (0xFF0D1812),
            juce::Colour (0x0F66FFAA), juce::Colour (0x1F66FFAA),
            juce::Colour (0xFFDDF0E2), juce::Colour (0xFF607A66), juce::Colour (0xFF3F564A),
            juce::Colour (0x0A66FFAA), juce::Colour (0x4080FFC0),
            juce::Colour (0xFF65D6A6), juce::Colour (0xFFE8C547),
            { juce::Colour (0xFF65D6A6), juce::Colour (0xFF8FE6B5), juce::Colour (0xFF4DBE8C),
              juce::Colour (0xFFB6E876), juce::Colour (0xFFE8C547), juce::Colour (0xFF7FB069),
              juce::Colour (0xFFD4A574) }
        },
        // 3 — SUNSET
        {
            "Sunset",
            juce::Colour (0xFF15090B), juce::Colour (0xFF1F1115), juce::Colour (0xFF180A0D),
            juce::Colour (0x14FF8866), juce::Colour (0x22FF8866),
            juce::Colour (0xFFFFE4D6), juce::Colour (0xFF8E6B5D), juce::Colour (0xFF553B33),
            juce::Colour (0x0AFFAA88), juce::Colour (0x40FFAA88),
            juce::Colour (0xFFFF7B54), juce::Colour (0xFFFFD56A),
            { juce::Colour (0xFFFF7B54), juce::Colour (0xFFFFA552), juce::Colour (0xFFFFD56A),
              juce::Colour (0xFFFF5469), juce::Colour (0xFFE94560), juce::Colour (0xFFC23B5F),
              juce::Colour (0xFF8E2C5E) }
        },
        // 4 — MONO
        {
            "Mono",
            juce::Colour (0xFF0A0A0A), juce::Colour (0xFF141414), juce::Colour (0xFF0F0F0F),
            juce::Colour (0x12FFFFFF), juce::Colour (0x22FFFFFF),
            juce::Colour (0xFFE8E8E8), juce::Colour (0xFF666666), juce::Colour (0xFF3C3C3C),
            juce::Colour (0x0CFFFFFF), juce::Colour (0x40FFFFFF),
            juce::Colour (0xFFCCCCCC), juce::Colour (0xFF888888),
            { juce::Colour (0xFFE8E8E8), juce::Colour (0xFFCCCCCC), juce::Colour (0xFFAAAAAA),
              juce::Colour (0xFF888888), juce::Colour (0xFF707070), juce::Colour (0xFFBBBBBB),
              juce::Colour (0xFF999999) }
        },
        // 5 — AURORA
        {
            "Aurora",
            juce::Colour (0xFF071017), juce::Colour (0xFF0E1A23), juce::Colour (0xFF0A141B),
            juce::Colour (0x1466DDFF), juce::Colour (0x2266DDFF),
            juce::Colour (0xFFD8F3FF), juce::Colour (0xFF5C7A8C), juce::Colour (0xFF3A5260),
            juce::Colour (0x0966EEFF), juce::Colour (0x4080F0FF),
            juce::Colour (0xFF4DD0E1), juce::Colour (0xFF89F7FE),
            { juce::Colour (0xFF4DD0E1), juce::Colour (0xFF89F7FE), juce::Colour (0xFF66FFE3),
              juce::Colour (0xFFB6F5FF), juce::Colour (0xFF7FE0F5), juce::Colour (0xFFA1FFCE),
              juce::Colour (0xFF5CC8F4) }
        },
        // 6 — VAPOR
        {
            "Vapor",
            juce::Colour (0xFF120B1F), juce::Colour (0xFF1B1330), juce::Colour (0xFF150D26),
            juce::Colour (0x18FF99FF), juce::Colour (0x28FF99FF),
            juce::Colour (0xFFF5E4FF), juce::Colour (0xFF8273A4), juce::Colour (0xFF564A75),
            juce::Colour (0x0CFF99FF), juce::Colour (0x40FFB0FF),
            juce::Colour (0xFFFF6EC7), juce::Colour (0xFF7FE5FF),
            { juce::Colour (0xFFFF6EC7), juce::Colour (0xFFC678FF), juce::Colour (0xFF7FE5FF),
              juce::Colour (0xFFB388FF), juce::Colour (0xFFFFB7E5), juce::Colour (0xFFFFD6E0),
              juce::Colour (0xFF9D7DFF) }
        },
        // 7 — SANDSTONE
        {
            "Sandstone",
            juce::Colour (0xFF14110D), juce::Colour (0xFF1E1A14), juce::Colour (0xFF18140F),
            juce::Colour (0x14C9A66B), juce::Colour (0x22C9A66B),
            juce::Colour (0xFFF0E4D0), juce::Colour (0xFF8A7A5E), juce::Colour (0xFF564B3B),
            juce::Colour (0x0AC9A66B), juce::Colour (0x40D9B989),
            juce::Colour (0xFFD9A05B), juce::Colour (0xFFC97C4F),
            { juce::Colour (0xFFD9A05B), juce::Colour (0xFFE8C28E), juce::Colour (0xFFC97C4F),
              juce::Colour (0xFFB85C38), juce::Colour (0xFFF0D080), juce::Colour (0xFFA67C52),
              juce::Colour (0xFFE3B47A) }
        },
        // 8 — LAVENDER
        {
            "Lavender",
            juce::Colour (0xFF0F0B1A), juce::Colour (0xFF181225), juce::Colour (0xFF130E1F),
            juce::Colour (0x16A78BFA), juce::Colour (0x24A78BFA),
            juce::Colour (0xFFE6DDFF), juce::Colour (0xFF6E5F95), juce::Colour (0xFF453A65),
            juce::Colour (0x0CA78BFA), juce::Colour (0x40C5A8FF),
            juce::Colour (0xFFA78BFA), juce::Colour (0xFFFB923C),
            { juce::Colour (0xFFA78BFA), juce::Colour (0xFF8B7CD9), juce::Colour (0xFFCBA9FF),
              juce::Colour (0xFFD891EF), juce::Colour (0xFFAE93FE), juce::Colour (0xFF7C6FCC),
              juce::Colour (0xFFE2BBFF) }
        },
        // 9 — CITRUS
        {
            "Citrus",
            juce::Colour (0xFF111007), juce::Colour (0xFF1B1A10), juce::Colour (0xFF15140A),
            juce::Colour (0x14E8E866), juce::Colour (0x22E8E866),
            juce::Colour (0xFFF4F0D5), juce::Colour (0xFF8A8762), juce::Colour (0xFF55523B),
            juce::Colour (0x0AE8E866), juce::Colour (0x40FFFF80),
            juce::Colour (0xFFFACC15), juce::Colour (0xFFA3E635),
            { juce::Colour (0xFFFACC15), juce::Colour (0xFFA3E635), juce::Colour (0xFFE8E84A),
              juce::Colour (0xFFFFD93D), juce::Colour (0xFFCDDC39), juce::Colour (0xFFFFE066),
              juce::Colour (0xFFB7E04B) }
        },
    };

    inline int getNumThemes() { return (int) (sizeof (themes) / sizeof (themes[0])); }
}

class RvznSettings : public juce::ChangeBroadcaster
{
public:
    static RvznSettings& getInstance()
    {
        static RvznSettings instance;
        return instance;
    }

    int  getThemeIndex() const { return themeIndex; }

    void setThemeIndex (int idx)
    {
        idx = juce::jlimit (0, RvznThemesData::getNumThemes() - 1, idx);
        if (idx == themeIndex) return;
        themeIndex = idx;
        applyCurrentTheme();
        save();
        sendChangeMessage();
    }

    static const RvznTheme& getTheme (int idx)
    {
        return RvznThemesData::themes[juce::jlimit (0, RvznThemesData::getNumThemes() - 1, idx)];
    }

    static int getNumThemes() { return RvznThemesData::getNumThemes(); }

    void applyCurrentTheme()
    {
        const auto& t = getTheme (themeIndex);

        RvznColours::background  = t.background;
        RvznColours::surface     = t.surface;
        RvznColours::header      = t.header;
        RvznColours::border      = t.border;
        RvznColours::borderMid   = t.borderMid;

        RvznColours::textPrimary = t.textPrimary;
        RvznColours::textMuted   = t.textMuted;
        RvznColours::textDim     = t.textDim;

        RvznColours::gridLine    = t.gridLine;
        RvznColours::nodeBorder  = t.nodeBorder;

        RvznColours::accentBlue   = t.accentPrimary;
        RvznColours::curveBlue    = t.accentPrimary;
        RvznColours::curveFill    = t.accentPrimary.withAlpha (0.12f);
        RvznColours::accentOrange = t.accentSecondary;
        RvznColours::bypass       = t.accentSecondary;

        for (int i = 0; i < 7; ++i)
            RvznColours::bandPalette[i] = t.bandColours[i];
    }

private:
    RvznSettings()
    {
        load();
        applyCurrentTheme();
    }

    juce::File getSettingsFile() const
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("RVZNEQ")
                   .getChildFile ("settings.xml");
    }

    void load()
    {
        auto f = getSettingsFile();
        if (! f.existsAsFile()) return;
        if (auto xml = juce::XmlDocument::parse (f))
            themeIndex = juce::jlimit (0, RvznThemesData::getNumThemes() - 1,
                                       xml->getIntAttribute ("theme", 0));
    }

    void save() const
    {
        auto f = getSettingsFile();
        f.getParentDirectory().createDirectory();
        juce::XmlElement xml ("RvznSettings");
        xml.setAttribute ("theme", themeIndex);
        xml.writeTo (f);
    }

    int themeIndex = 0;

    JUCE_DECLARE_NON_COPYABLE (RvznSettings)
};
