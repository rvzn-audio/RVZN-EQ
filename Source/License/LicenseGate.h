#pragma once
#include <JuceHeader.h>
#include "LicenseManager.h"

namespace rvzn { namespace license {

// Full-editor overlay that gates the plugin until the license is verified.
// In Activated state the component becomes mouse-transparent except for a
// small badge in the bottom-right that opens a license-info panel containing
// "Deactivate this machine".
class LicenseGate : public juce::Component,
                    private juce::ChangeListener,
                    private juce::Timer
{
public:
    explicit LicenseGate (LicenseManager& manager);
    ~LicenseGate() override;

    void paint   (juce::Graphics&) override;
    void resized () override;

    bool isUnlocked() const noexcept { return manager.isUnlocked(); }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    void layoutActivationForm  (juce::Rectangle<int> bounds);
    void layoutInfoPanel       (juce::Rectangle<int> bounds);
    void layoutBadge           (juce::Rectangle<int> bounds);

    void updateForState();
    void onActivateClicked();
    void onDeactivateClicked();
    void openInfoPanel();
    void closeInfoPanel();
    void openSupportPage();

    LicenseManager& manager;

    // --- Activation form (visible when not unlocked) ----------------------
    juce::Label    titleLabel, subtitleLabel, statusLabel,
                   emailLabel, keyLabel, hintLabel;
    juce::TextEditor emailEditor, keyEditor;
    juce::TextButton activateButton { "ACTIVATE" };
    juce::TextButton supportButton  { "Contact support" };

    // --- Licensed-mode UI -------------------------------------------------
    juce::TextButton badgeButton;             // Small badge always visible when unlocked
    juce::Component  infoPanel;               // Modal-ish info window inside the gate
    juce::Label      infoTitleLabel, infoProductLabel, infoEmailLabel,
                     infoKeyLabel, infoActivationsLabel, infoExpiresLabel,
                     infoStatusLabel;
    juce::TextButton deactivateButton { "Deactivate this machine" };
    juce::TextButton infoCloseButton  { "Close" };

    bool infoPanelVisible = false;
    bool busy             = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LicenseGate)
};

}} // namespace rvzn::license
