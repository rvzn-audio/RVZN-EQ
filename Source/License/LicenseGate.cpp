#include "LicenseGate.h"
#include "../LookAndFeel/RvznColours.h"

namespace rvzn { namespace license {

namespace
{
    constexpr int   kCardWidth      = 440;
    constexpr int   kCardHeight     = 360;
    constexpr int   kInfoWidth      = 380;
    constexpr int   kInfoHeight     = 280;
    constexpr int   kBadgeWidth     = 96;
    constexpr int   kBadgeHeight    = 18;
    constexpr float kBadgeMargin    = 8.0f;

    void styleHeading (juce::Label& l, const juce::String& text, float size)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (juce::FontOptions (size, juce::Font::plain));
        l.setColour (juce::Label::textColourId, RvznColours::textPrimary);
        l.setJustificationType (juce::Justification::centredLeft);
        l.setInterceptsMouseClicks (false, false);
    }

    void styleBody (juce::Label& l, const juce::String& text, juce::Colour colour, float size = 12.0f)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (juce::FontOptions (size, juce::Font::plain));
        l.setColour (juce::Label::textColourId, colour);
        l.setJustificationType (juce::Justification::centredLeft);
        l.setInterceptsMouseClicks (false, false);
    }

    void styleInputLabel (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (juce::FontOptions (10.f, juce::Font::plain));
        l.setColour (juce::Label::textColourId, RvznColours::textMuted);
        l.setJustificationType (juce::Justification::centredLeft);
        l.setInterceptsMouseClicks (false, false);
    }

    void styleEditor (juce::TextEditor& e, const juce::String& placeholder)
    {
        e.setColour (juce::TextEditor::backgroundColourId, RvznColours::header);
        e.setColour (juce::TextEditor::textColourId,       RvznColours::textPrimary);
        e.setColour (juce::TextEditor::outlineColourId,    RvznColours::borderMid);
        e.setColour (juce::TextEditor::focusedOutlineColourId, RvznColours::accentBlue);
        e.setColour (juce::TextEditor::highlightColourId,  RvznColours::accentBlue.withAlpha (0.35f));
        e.setColour (juce::TextEditor::shadowColourId,     juce::Colours::transparentBlack);
        e.setTextToShowWhenEmpty (placeholder, RvznColours::textDim);
        e.setIndents (8, 6);
        e.setFont (juce::FontOptions (13.f, juce::Font::plain));
    }

    void styleCardButton (juce::TextButton& b, juce::Colour accent)
    {
        b.setColour (juce::TextButton::buttonColourId,   accent);
        b.setColour (juce::TextButton::buttonOnColourId, accent.brighter (0.15f));
        b.setColour (juce::TextButton::textColourOnId,   juce::Colours::white);
        b.setColour (juce::TextButton::textColourOffId,  juce::Colours::white);
    }

    void styleLinkButton (juce::TextButton& b)
    {
        b.setColour (juce::TextButton::buttonColourId,   juce::Colours::transparentBlack);
        b.setColour (juce::TextButton::buttonOnColourId, juce::Colours::transparentBlack);
        b.setColour (juce::TextButton::textColourOffId,  RvznColours::textMuted);
        b.setColour (juce::TextButton::textColourOnId,   RvznColours::accentBlue);
    }
}

LicenseGate::LicenseGate (LicenseManager& m) : manager (m)
{
    setOpaque (false);

    // --- Activation form ----------------------------------------------------
    styleHeading   (titleLabel,    "ACTIVATE " + manager.getProductName(), 14.f);
    titleLabel.setColour (juce::Label::textColourId, RvznColours::accentBlue);

    styleBody      (subtitleLabel,
                    "Enter the email address and license key from your RVZN purchase to activate this plugin.",
                    RvznColours::textPrimary, 12.f);
    subtitleLabel.setJustificationType (juce::Justification::topLeft);

    styleInputLabel (emailLabel, "EMAIL");
    styleInputLabel (keyLabel,   "LICENSE KEY");
    styleEditor     (emailEditor, "you@example.com");
    emailEditor.setInputRestrictions (320);

    styleEditor     (keyEditor, "XXXX-XXXX-XXXX-XXXX");
    keyEditor.setInputRestrictions (32, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-");
    keyEditor.onTextChange = [this]()
    {
        const auto t = keyEditor.getText();
        const auto u = t.toUpperCase();
        if (t != u)
        {
            const auto caret = keyEditor.getCaretPosition();
            keyEditor.setText (u, juce::dontSendNotification);
            keyEditor.setCaretPosition (caret);
        }
    };
    emailEditor.onReturnKey = [this] { keyEditor.grabKeyboardFocus(); };
    keyEditor.onReturnKey   = [this] { onActivateClicked(); };

    styleBody (statusLabel, {}, RvznColours::accentOrange, 11.f);
    statusLabel.setJustificationType (juce::Justification::topLeft);

    styleBody (hintLabel,
               "Your credentials are stored in the system keychain. "
               "Activations are limited per license - use \"Deactivate this machine\" "
               "from the license badge to free a slot.",
               RvznColours::textMuted, 10.f);
    hintLabel.setJustificationType (juce::Justification::topLeft);

    styleCardButton (activateButton, RvznColours::accentBlue);
    activateButton.onClick = [this] { onActivateClicked(); };

    styleLinkButton (supportButton);
    supportButton.onClick = [this] { openSupportPage(); };

    addChildComponent (titleLabel);
    addChildComponent (subtitleLabel);
    addChildComponent (emailLabel);
    addChildComponent (keyLabel);
    addChildComponent (emailEditor);
    addChildComponent (keyEditor);
    addChildComponent (statusLabel);
    addChildComponent (hintLabel);
    addChildComponent (activateButton);
    addChildComponent (supportButton);

    // --- Licensed-mode badge -----------------------------------------------
    badgeButton.setButtonText ("\xe2\x97\x8f  LICENSED");
    badgeButton.setColour (juce::TextButton::buttonColourId,   RvznColours::header.withAlpha (0.75f));
    badgeButton.setColour (juce::TextButton::buttonOnColourId, RvznColours::header.withAlpha (0.9f));
    badgeButton.setColour (juce::TextButton::textColourOffId,  RvznColours::accentEmerald);
    badgeButton.setColour (juce::TextButton::textColourOnId,   RvznColours::accentEmerald);
    badgeButton.setTooltip ("Click for license info or to deactivate this machine.");
    badgeButton.onClick = [this] { openInfoPanel(); };
    addChildComponent (badgeButton);

    // --- Info panel --------------------------------------------------------
    infoPanel.setOpaque (false);
    infoPanel.setInterceptsMouseClicks (true, true);
    addChildComponent (infoPanel);

    styleHeading (infoTitleLabel, "LICENSE", 13.f);
    infoTitleLabel.setColour (juce::Label::textColourId, RvznColours::accentBlue);
    styleBody (infoProductLabel,     {}, RvznColours::textPrimary, 12.f);
    styleBody (infoEmailLabel,       {}, RvznColours::textPrimary, 12.f);
    styleBody (infoKeyLabel,         {}, RvznColours::textPrimary, 12.f);
    styleBody (infoActivationsLabel, {}, RvznColours::textPrimary, 12.f);
    styleBody (infoExpiresLabel,     {}, RvznColours::textPrimary, 12.f);
    styleBody (infoStatusLabel,      {}, RvznColours::textMuted,   11.f);
    infoStatusLabel.setJustificationType (juce::Justification::topLeft);

    infoPanel.addAndMakeVisible (infoTitleLabel);
    infoPanel.addAndMakeVisible (infoProductLabel);
    infoPanel.addAndMakeVisible (infoEmailLabel);
    infoPanel.addAndMakeVisible (infoKeyLabel);
    infoPanel.addAndMakeVisible (infoActivationsLabel);
    infoPanel.addAndMakeVisible (infoExpiresLabel);
    infoPanel.addAndMakeVisible (infoStatusLabel);

    styleCardButton (deactivateButton, RvznColours::accentOrange);
    deactivateButton.onClick = [this] { onDeactivateClicked(); };
    infoPanel.addAndMakeVisible (deactivateButton);

    styleLinkButton (infoCloseButton);
    infoCloseButton.onClick = [this] { closeInfoPanel(); };
    infoPanel.addAndMakeVisible (infoCloseButton);

    manager.addChangeListener (this);
    startTimerHz (4);
    updateForState();
}

LicenseGate::~LicenseGate()
{
    manager.removeChangeListener (this);
}

void LicenseGate::changeListenerCallback (juce::ChangeBroadcaster*)
{
    updateForState();
    repaint();
}

void LicenseGate::timerCallback()
{
    // Re-check the busy spinner ellipsis and keep the visible status in sync.
    auto msg = manager.getStatusMessage();
    statusLabel.setText (msg, juce::dontSendNotification);
    infoStatusLabel.setText (msg, juce::dontSendNotification);
}

void LicenseGate::updateForState()
{
    const auto s = manager.getState();
    const bool unlocked = (s == State::Activated);

    // Form visibility
    const bool showForm = ! unlocked;
    titleLabel.setVisible    (showForm);
    subtitleLabel.setVisible (showForm);
    emailLabel.setVisible    (showForm);
    keyLabel.setVisible      (showForm);
    emailEditor.setVisible   (showForm);
    keyEditor.setVisible     (showForm);
    statusLabel.setVisible   (showForm);
    hintLabel.setVisible     (showForm);
    activateButton.setVisible(showForm);
    supportButton.setVisible (showForm);

    // Mouse interception: opaque & blocking when gated; pass-through when unlocked.
    setInterceptsMouseClicks (! unlocked, true);

    // Badge & info panel
    badgeButton.setVisible (unlocked);
    if (! unlocked)
    {
        infoPanel.setVisible (false);
        infoPanelVisible = false;
    }

    // Verifying / busy
    busy = (s == State::Verifying);
    activateButton.setEnabled (! busy);
    deactivateButton.setEnabled (! busy);
    emailEditor.setReadOnly (busy);
    keyEditor.setReadOnly   (busy);

    // Update form text
    statusLabel.setText (manager.getStatusMessage(), juce::dontSendNotification);

    if (s == State::NeedsActivation && emailEditor.getText().isEmpty())
        emailEditor.setText (manager.getEmail(), juce::dontSendNotification);

    if (s == State::Blocked || s == State::OfflineLocked)
        statusLabel.setColour (juce::Label::textColourId, RvznColours::accentPink);
    else
        statusLabel.setColour (juce::Label::textColourId, RvznColours::accentOrange);

    // Info panel contents
    infoProductLabel.setText ("Product:      " + manager.getProductName(), juce::dontSendNotification);
    infoEmailLabel.setText   ("Email:        " + manager.getEmail(),       juce::dontSendNotification);
    infoKeyLabel.setText     ("Key:          " + manager.getLicenseKeyMasked(), juce::dontSendNotification);
    {
        const int u = manager.getActivationsUsed();
        const int l = manager.getActivationsLimit();
        juce::String act = "Activations:  " + juce::String (u) + " of " + juce::String (l);
        if (l <= 0) act = "Activations:  -";
        infoActivationsLabel.setText (act, juce::dontSendNotification);
    }
    infoExpiresLabel.setText ("Expires:      -", juce::dontSendNotification);
    infoStatusLabel.setText  (manager.getStatusMessage(), juce::dontSendNotification);

    resized();
}

void LicenseGate::onActivateClicked()
{
    auto email = emailEditor.getText().trim();
    auto key   = keyEditor.getText().trim().toUpperCase();
    if (email.isEmpty() || key.isEmpty())
    {
        statusLabel.setColour (juce::Label::textColourId, RvznColours::accentPink);
        statusLabel.setText ("Please enter both your email and license key.", juce::dontSendNotification);
        return;
    }
    manager.submitCredentials (email, key);
}

void LicenseGate::onDeactivateClicked()
{
    deactivateButton.setEnabled (false);
    manager.deactivateThisMachine ([this] (bool, juce::String)
    {
        if (auto* mm = juce::MessageManager::getInstanceWithoutCreating())
            (void) mm;
        deactivateButton.setEnabled (true);
        closeInfoPanel();
    });
}

void LicenseGate::openInfoPanel()
{
    infoPanelVisible = true;
    infoPanel.setVisible (true);
    infoPanel.toFront (false);
    resized();
    repaint();
}

void LicenseGate::closeInfoPanel()
{
    infoPanelVisible = false;
    infoPanel.setVisible (false);
    repaint();
}

void LicenseGate::openSupportPage()
{
    juce::URL ("https://rvzn-audio.com/support").launchInDefaultBrowser();
}

void LicenseGate::resized()
{
    auto b = getLocalBounds();
    layoutActivationForm (b);
    layoutBadge          (b);
    layoutInfoPanel      (b);
}

void LicenseGate::layoutActivationForm (juce::Rectangle<int> bounds)
{
    const int cw = juce::jmin (kCardWidth,  bounds.getWidth()  - 40);
    const int ch = juce::jmin (kCardHeight, bounds.getHeight() - 40);
    auto card = juce::Rectangle<int> (0, 0, cw, ch).withCentre (bounds.getCentre());

    auto inner = card.reduced (24);

    titleLabel.setBounds    (inner.removeFromTop (22));
    inner.removeFromTop (6);
    subtitleLabel.setBounds (inner.removeFromTop (36));
    inner.removeFromTop (12);

    emailLabel.setBounds  (inner.removeFromTop (14));
    emailEditor.setBounds (inner.removeFromTop (28));
    inner.removeFromTop (10);

    keyLabel.setBounds  (inner.removeFromTop (14));
    keyEditor.setBounds (inner.removeFromTop (28));
    inner.removeFromTop (10);

    statusLabel.setBounds (inner.removeFromTop (30));
    inner.removeFromTop (4);

    auto buttonRow = inner.removeFromTop (32);
    supportButton.setBounds  (buttonRow.removeFromLeft (130));
    buttonRow.removeFromLeft (8);
    activateButton.setBounds (buttonRow);
    inner.removeFromTop (8);

    hintLabel.setBounds (inner);
}

void LicenseGate::layoutBadge (juce::Rectangle<int> bounds)
{
    badgeButton.setBounds (bounds.getRight()  - kBadgeWidth  - (int) kBadgeMargin,
                           bounds.getBottom() - kBadgeHeight - (int) kBadgeMargin,
                           kBadgeWidth, kBadgeHeight);
}

void LicenseGate::layoutInfoPanel (juce::Rectangle<int> bounds)
{
    infoPanel.setBounds (bounds);

    const int iw = juce::jmin (kInfoWidth,  bounds.getWidth()  - 40);
    const int ih = juce::jmin (kInfoHeight, bounds.getHeight() - 40);
    auto card = juce::Rectangle<int> (0, 0, iw, ih).withCentre (bounds.getCentre());

    auto inner = card.reduced (20);

    infoTitleLabel.setBounds (inner.removeFromTop (22));
    inner.removeFromTop (10);

    infoProductLabel.setBounds      (inner.removeFromTop (18));
    infoEmailLabel.setBounds        (inner.removeFromTop (18));
    infoKeyLabel.setBounds          (inner.removeFromTop (18));
    infoActivationsLabel.setBounds  (inner.removeFromTop (18));
    infoExpiresLabel.setBounds      (inner.removeFromTop (18));
    inner.removeFromTop (8);
    infoStatusLabel.setBounds       (inner.removeFromTop (32));
    inner.removeFromTop (8);

    auto buttonRow = inner.removeFromBottom (32);
    infoCloseButton.setBounds    (buttonRow.removeFromLeft (80));
    buttonRow.removeFromLeft (8);
    deactivateButton.setBounds   (buttonRow);
}

void LicenseGate::paint (juce::Graphics& g)
{
    const auto s = manager.getState();
    const bool unlocked = (s == State::Activated);

    if (! unlocked)
    {
        // Full-editor scrim
        g.fillAll (RvznColours::background.withAlpha (0.97f));

        // Card background
        const int cw = juce::jmin (kCardWidth,  getWidth()  - 40);
        const int ch = juce::jmin (kCardHeight, getHeight() - 40);
        auto card = juce::Rectangle<int> (0, 0, cw, ch).withCentre (getLocalBounds().getCentre()).toFloat();

        g.setColour (RvznColours::surface);
        g.fillRoundedRectangle (card, 10.f);
        g.setColour (RvznColours::borderMid);
        g.drawRoundedRectangle (card.reduced (0.5f), 10.f, 1.f);

        // Small brand strip
        g.setColour (RvznColours::accentBlue);
        g.fillRect (card.withHeight (3.f));
    }

    if (infoPanelVisible)
    {
        // Dim background behind the info panel
        g.setColour (juce::Colours::black.withAlpha (unlocked ? 0.55f : 0.0f));
        g.fillRect (getLocalBounds());

        const int iw = juce::jmin (kInfoWidth,  getWidth()  - 40);
        const int ih = juce::jmin (kInfoHeight, getHeight() - 40);
        auto card = juce::Rectangle<int> (0, 0, iw, ih).withCentre (getLocalBounds().getCentre()).toFloat();

        g.setColour (RvznColours::surface);
        g.fillRoundedRectangle (card, 10.f);
        g.setColour (RvznColours::borderMid);
        g.drawRoundedRectangle (card.reduced (0.5f), 10.f, 1.f);
        g.setColour (RvznColours::accentBlue);
        g.fillRect (card.withHeight (3.f));
    }
}

}} // namespace rvzn::license
