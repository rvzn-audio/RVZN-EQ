#pragma once
#include <JuceHeader.h>

namespace rvzn { namespace license {

struct Credentials
{
    juce::String email;
    juce::String licenseKey;

    bool isValid() const noexcept
    {
        return email.isNotEmpty() && licenseKey.isNotEmpty();
    }
};

// Stores license credentials in the OS-native secure credential store:
//   macOS    -> Keychain (kSecClassGenericPassword)
//   Windows  -> Credential Manager (CRED_TYPE_GENERIC)
//   Linux    -> JSON file in userApplicationDataDirectory (best-effort)
class SecureStore
{
public:
    explicit SecureStore (juce::String serviceName);

    bool save  (const Credentials& creds);
    Credentials load();
    bool clear();

private:
    juce::String serviceName;
};

}} // namespace rvzn::license
