#include "LicenseManager.h"
#include "DeviceId.h"

namespace rvzn { namespace license {

namespace
{
    constexpr int kHttpConnectionTimeoutMs = 7000;

    juce::String buildRequestBody (const juce::String& email,
                                   const juce::String& licenseKey,
                                   const juce::String& deviceId,
                                   const juce::String& productCode)
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("email",        email);
        obj->setProperty ("license_key",  licenseKey);
        obj->setProperty ("device_id",    deviceId);
        obj->setProperty ("product_code", productCode);
        juce::var v (obj);
        return juce::JSON::toString (v, true);
    }

    VerifyResult parseResponse (int statusCode, const juce::String& body)
    {
        VerifyResult r;
        r.networkError = false;

        if (body.isEmpty())
        {
            r.ok = false;
            r.networkError = true;
            r.message = "No response from license server.";
            return r;
        }

        auto v = juce::JSON::parse (body);
        auto* obj = v.getDynamicObject();

        if (obj == nullptr)
        {
            r.ok = false;
            r.message = "License server returned an invalid response.";
            return r;
        }

        r.ok               = (bool) obj->getProperty ("valid");
        r.product          = obj->getProperty ("product").toString();
        r.activationsUsed  = (int)  obj->getProperty ("activations_used");
        r.activationsLimit = (int)  obj->getProperty ("activations_limit");
        r.expiresAt        = obj->getProperty ("expires_at").toString();
        r.message          = obj->getProperty ("message").toString();

        // The HTTP status code overrides "valid": ok on 200, otherwise treat as failure.
        if (statusCode != 200)
        {
            r.ok = false;
            if (r.message.isEmpty())
                r.message = "License verification failed (HTTP " + juce::String (statusCode) + ").";
        }
        else if (r.ok && r.message.isEmpty())
        {
            r.message = "License active.";
        }

        return r;
    }

    VerifyResult postJson (const juce::String& url,
                           const juce::String& jsonBody)
    {
        juce::URL u (url);
        u = u.withPOSTData (jsonBody);

        juce::StringPairArray responseHeaders;
        int statusCode = 0;

        auto opts = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                       .withExtraHeaders ("Content-Type: application/json\r\nAccept: application/json")
                       .withConnectionTimeoutMs (kHttpConnectionTimeoutMs)
                       .withResponseHeaders     (&responseHeaders)
                       .withStatusCode          (&statusCode);

        auto stream = u.createInputStream (opts);
        if (stream == nullptr)
        {
            VerifyResult r;
            r.ok = false;
            r.networkError = true;
            r.message = "Could not reach the license server. Check your internet connection.";
            return r;
        }

        auto body = stream->readEntireStreamAsString();
        return parseResponse (statusCode, body);
    }
}

LicenseManager::LicenseManager (juce::String productCode_,
                                juce::String productName_,
                                juce::String verifyUrl_,
                                juce::String deactivateUrl_,
                                int          offlineGraceLaunches_)
    : productCode (std::move (productCode_)),
      productName (std::move (productName_)),
      verifyUrl   (std::move (verifyUrl_)),
      deactivateUrl (std::move (deactivateUrl_)),
      offlineGraceLaunches (offlineGraceLaunches_),
      store ("com.rvzn.license." + productCode)
{
}

LicenseManager::~LicenseManager() = default;

juce::File LicenseManager::propsDir() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
              .getChildFile ("RVZN")
              .getChildFile (productCode);
}

juce::File LicenseManager::propsFile() const
{
    return propsDir().getChildFile ("license.settings");
}

juce::PropertiesFile& LicenseManager::props()
{
    if (propsCache == nullptr)
    {
        juce::PropertiesFile::Options opts;
        opts.applicationName     = productCode + "-license";
        opts.filenameSuffix      = ".settings";
        opts.osxLibrarySubFolder = "Application Support";
        opts.folderName          = "RVZN/" + productCode;
        opts.storageFormat       = juce::PropertiesFile::storeAsXML;
        propsCache = std::make_unique<juce::PropertiesFile> (opts);
    }
    return *propsCache;
}

int LicenseManager::getOfflineCounter() const
{
    auto& p = const_cast<LicenseManager*> (this)->props();
    return p.getIntValue ("offlineLaunches", 0);
}

void LicenseManager::incrementOfflineCounter()
{
    auto& p = props();
    p.setValue ("offlineLaunches", p.getIntValue ("offlineLaunches", 0) + 1);
    p.saveIfNeeded();
}

void LicenseManager::resetOfflineCounter()
{
    auto& p = props();
    p.removeValue ("offlineLaunches");
    p.setValue ("lastOnlineVerify", (juce::int64) juce::Time::getCurrentTime().toMilliseconds());
    p.saveIfNeeded();
}

juce::String LicenseManager::getStatusMessage() const
{
    const juce::ScopedLock l (stringLock);
    return statusMessage;
}

juce::String LicenseManager::getEmail() const
{
    const juce::ScopedLock l (stringLock);
    return currentEmail;
}

juce::String LicenseManager::getLicenseKeyMasked() const
{
    auto c = const_cast<LicenseManager*> (this)->store.load();
    if (c.licenseKey.length() < 8) return {};
    return juce::String ("\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2-")
         + "\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2-"
         + "\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2-"
         + c.licenseKey.getLastCharacters (4);
}

void LicenseManager::setState (State s)
{
    state.store (s);
    juce::WeakReference<LicenseManager> self (this);
    juce::MessageManager::callAsync ([self]()
    {
        if (auto* m = self.get())
            m->sendChangeMessage();
    });
}

void LicenseManager::setStatusMessage (juce::String s)
{
    const juce::ScopedLock l (stringLock);
    statusMessage = std::move (s);
}

void LicenseManager::setEmail (juce::String s)
{
    const juce::ScopedLock l (stringLock);
    currentEmail = std::move (s);
}

VerifyResult LicenseManager::verifyBlocking (const juce::String& email,
                                             const juce::String& key) const
{
    auto body = buildRequestBody (email, key, getDeviceId(), productCode);
    return postJson (verifyUrl, body);
}

VerifyResult LicenseManager::deactivateBlocking (const juce::String& email,
                                                 const juce::String& key) const
{
    auto body = buildRequestBody (email, key, getDeviceId(), productCode);
    return postJson (deactivateUrl, body);
}

void LicenseManager::handleVerifyResult (const VerifyResult& r,
                                         const Credentials& submitted,
                                         bool wasSubmission)
{
    if (r.ok)
    {
        if (wasSubmission)
            store.save (submitted);

        setEmail (submitted.email);
        setStatusMessage (r.message);
        lastActivationsUsed.store  (r.activationsUsed);
        lastActivationsLimit.store (r.activationsLimit);
        resetOfflineCounter();
        setState (State::Activated);
        return;
    }

    if (r.networkError)
    {
        // Apply offline grace period: only previously-activated machines are
        // allowed to launch while offline.
        const auto saved = store.load();
        if (! saved.isValid())
        {
            setStatusMessage (r.message);
            setState (State::NeedsActivation);
            return;
        }

        const int offlineCount = getOfflineCounter();
        if (offlineCount + 1 >= offlineGraceLaunches)
        {
            setStatusMessage ("Offline grace period exhausted. Please connect to the internet and re-verify your license.");
            setState (State::OfflineLocked);
            return;
        }

        incrementOfflineCounter();
        setEmail (saved.email);
        const int remaining = offlineGraceLaunches - offlineCount - 1;
        setStatusMessage ("Running offline (" + juce::String (remaining)
                          + " launch" + (remaining == 1 ? "" : "es") + " remaining before re-verification is required).");
        setState (State::Activated);
        return;
    }

    // Hard failure (invalid / revoked / blacklisted / expired / device limit)
    setStatusMessage (r.message.isEmpty()
                      ? juce::String ("This license could not be verified.")
                      : r.message);
    if (wasSubmission)
        setState (State::NeedsActivation);
    else
        setState (State::Blocked);
}

void LicenseManager::startupVerify()
{
    auto creds = store.load();
    if (! creds.isValid())
    {
        setStatusMessage ({});
        setState (State::NeedsActivation);
        return;
    }

    setEmail (creds.email);
    setStatusMessage ("Verifying license\xe2\x80\xa6");
    setState (State::Verifying);

    juce::WeakReference<LicenseManager> self (this);
    juce::Thread::launch ([self, creds]()
    {
        VerifyResult r;
        if (auto* m = self.get())
            r = m->verifyBlocking (creds.email, creds.licenseKey);
        else
            return;

        juce::MessageManager::callAsync ([self, r, creds]()
        {
            if (auto* m = self.get())
                m->handleVerifyResult (r, creds, false);
        });
    });
}

void LicenseManager::submitCredentials (juce::String email, juce::String licenseKey)
{
    Credentials creds;
    creds.email      = email.trim();
    creds.licenseKey = licenseKey.trim().toUpperCase();

    setEmail (creds.email);
    setStatusMessage ("Verifying license\xe2\x80\xa6");
    setState (State::Verifying);

    juce::WeakReference<LicenseManager> self (this);
    juce::Thread::launch ([self, creds]()
    {
        VerifyResult r;
        if (auto* m = self.get())
            r = m->verifyBlocking (creds.email, creds.licenseKey);
        else
            return;

        juce::MessageManager::callAsync ([self, r, creds]()
        {
            if (auto* m = self.get())
                m->handleVerifyResult (r, creds, true);
        });
    });
}

void LicenseManager::deactivateThisMachine (std::function<void (bool, juce::String)> done)
{
    const auto creds = store.load();
    if (! creds.isValid())
    {
        if (done) done (false, "No license is active on this machine.");
        return;
    }

    setStatusMessage ("Deactivating\xe2\x80\xa6");
    setState (State::Verifying);

    juce::WeakReference<LicenseManager> self (this);
    juce::Thread::launch ([self, creds, done]()
    {
        VerifyResult r;
        if (auto* m = self.get())
            r = m->deactivateBlocking (creds.email, creds.licenseKey);
        else
            return;

        juce::MessageManager::callAsync ([self, r, done]()
        {
            auto* m = self.get();
            if (m == nullptr) return;

            if (r.networkError)
            {
                m->setStatusMessage (r.message);
                m->setState (State::Activated);
                if (done) done (false, r.message);
                return;
            }

            // Whether the server reports success or "already deactivated", we clear
            // local credentials so the user can re-enter on next launch.
            m->store.clear();
            m->setEmail ({});
            m->resetOfflineCounter();
            m->setStatusMessage ("This machine has been deactivated.");
            m->setState (State::NeedsActivation);
            if (done) done (true, r.message.isEmpty()
                                    ? juce::String ("This machine has been deactivated.")
                                    : r.message);
        });
    });
}

}} // namespace rvzn::license
