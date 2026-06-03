#pragma once
#include <JuceHeader.h>
#include "SecureStore.h"

namespace rvzn { namespace license {

enum class State
{
    Idle,             // Not yet checked
    Verifying,        // Network call in flight
    NeedsActivation,  // No saved credentials
    Activated,        // Verified valid
    Blocked,          // Verified invalid (message is shown to user)
    OfflineLocked     // No network and grace period exhausted
};

struct VerifyResult
{
    bool         ok = false;
    bool         networkError = false;
    juce::String message;
    juce::String product;
    int          activationsUsed  = 0;
    int          activationsLimit = 0;
    juce::String expiresAt;
};

// Coordinates license verification, secure storage of credentials,
// and the per-launch offline grace counter.
//
// Thread model: public mutators are called from the message thread.
// Network calls run on an internal worker thread; results are dispatched
// back to the message thread via juce::MessageManager::callAsync.
class LicenseManager : public juce::ChangeBroadcaster
{
public:
    LicenseManager (juce::String productCode,
                    juce::String productName,
                    juce::String verifyUrl,
                    juce::String deactivateUrl,
                    int          offlineGraceLaunches);

    ~LicenseManager() override;

    // Called once after construction. Loads saved credentials and either
    // moves to NeedsActivation or kicks off a verify request.
    void startupVerify();

    // Submit user-entered credentials. Saves them on success.
    void submitCredentials (juce::String email, juce::String licenseKey);

    // Releases a device slot on the server and clears the local credentials.
    // `done` is called on the message thread.
    void deactivateThisMachine (std::function<void (bool ok, juce::String message)> done);

    State        getState()             const { return state.load(); }
    juce::String getStatusMessage()     const;
    juce::String getEmail()             const;
    juce::String getLicenseKeyMasked()  const;
    juce::String getProductName()       const { return productName; }
    int          getActivationsUsed()   const { return lastActivationsUsed.load(); }
    int          getActivationsLimit()  const { return lastActivationsLimit.load(); }

    bool isUnlocked() const noexcept
    {
        const auto s = state.load();
        return s == State::Activated;
    }

private:
    VerifyResult verifyBlocking     (const juce::String& email, const juce::String& key) const;
    VerifyResult deactivateBlocking (const juce::String& email, const juce::String& key) const;

    void handleVerifyResult (const VerifyResult& result, const Credentials& submitted, bool wasSubmission);
    void setState           (State s);
    void setStatusMessage   (juce::String s);
    void setEmail           (juce::String s);

    juce::File   propsDir() const;
    juce::File   propsFile() const;
    juce::PropertiesFile& props();

    int  getOfflineCounter() const;
    void incrementOfflineCounter();
    void resetOfflineCounter();

    // Background worker for blocking HTTP calls.
    class HttpJob;

    juce::String productCode, productName, verifyUrl, deactivateUrl;
    int          offlineGraceLaunches;
    SecureStore  store;

    std::atomic<State> state { State::Idle };
    std::atomic<int>   lastActivationsUsed  { 0 };
    std::atomic<int>   lastActivationsLimit { 0 };

    juce::CriticalSection stringLock;
    juce::String statusMessage;
    juce::String currentEmail;

    std::unique_ptr<juce::PropertiesFile> propsCache;

    JUCE_DECLARE_WEAK_REFERENCEABLE (LicenseManager)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LicenseManager)
};

}} // namespace rvzn::license
