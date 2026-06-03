#include "SecureStore.h"

#if JUCE_MAC
 #include <Security/Security.h>
 #include <CoreFoundation/CoreFoundation.h>
#elif JUCE_WINDOWS
 #include <windows.h>
 #include <wincred.h>
 #pragma comment(lib, "Advapi32.lib")
#endif

namespace rvzn { namespace license {

SecureStore::SecureStore (juce::String name) : serviceName (std::move (name)) {}

#if JUCE_MAC

namespace
{
    CFStringRef cfStr (const juce::String& s)
    {
        return CFStringCreateWithCString (kCFAllocatorDefault,
                                          s.toRawUTF8(),
                                          kCFStringEncodingUTF8);
    }

    void deleteItem (const juce::String& service, const juce::String& account)
    {
        CFStringRef svc  = cfStr (service);
        CFStringRef acct = cfStr (account);

        const void* keys[]   = { kSecClass,                kSecAttrService, kSecAttrAccount };
        const void* values[] = { kSecClassGenericPassword, svc,             acct };

        CFDictionaryRef query = CFDictionaryCreate (kCFAllocatorDefault, keys, values, 3,
                                                    &kCFTypeDictionaryKeyCallBacks,
                                                    &kCFTypeDictionaryValueCallBacks);
        SecItemDelete (query);
        CFRelease (query);
        CFRelease (svc);
        CFRelease (acct);
    }

    bool setItem (const juce::String& service, const juce::String& account, const juce::String& value)
    {
        // Always remove the existing item first; SecItemAdd fails if a duplicate is present.
        deleteItem (service, account);

        CFStringRef svc  = cfStr (service);
        CFStringRef acct = cfStr (account);
        CFDataRef   data = CFDataCreate (kCFAllocatorDefault,
                                         (const UInt8*) value.toRawUTF8(),
                                         (CFIndex) value.getNumBytesAsUTF8());

        const void* keys[]   = { kSecClass,                kSecAttrService, kSecAttrAccount, kSecValueData };
        const void* values[] = { kSecClassGenericPassword, svc,             acct,            data };

        CFDictionaryRef query = CFDictionaryCreate (kCFAllocatorDefault, keys, values, 4,
                                                    &kCFTypeDictionaryKeyCallBacks,
                                                    &kCFTypeDictionaryValueCallBacks);
        OSStatus status = SecItemAdd (query, nullptr);
        CFRelease (query);
        CFRelease (svc);
        CFRelease (acct);
        CFRelease (data);
        return status == errSecSuccess;
    }

    juce::String getItem (const juce::String& service, const juce::String& account)
    {
        CFStringRef svc  = cfStr (service);
        CFStringRef acct = cfStr (account);

        const void* keys[]   = { kSecClass,                kSecAttrService, kSecAttrAccount,
                                 kSecReturnData,           kSecMatchLimit };
        const void* values[] = { kSecClassGenericPassword, svc,             acct,
                                 kCFBooleanTrue,           kSecMatchLimitOne };

        CFDictionaryRef query = CFDictionaryCreate (kCFAllocatorDefault, keys, values, 5,
                                                    &kCFTypeDictionaryKeyCallBacks,
                                                    &kCFTypeDictionaryValueCallBacks);
        CFTypeRef out = nullptr;
        OSStatus status = SecItemCopyMatching (query, &out);
        CFRelease (query);
        CFRelease (svc);
        CFRelease (acct);

        if (status != errSecSuccess || out == nullptr)
            return {};

        juce::String result ((const char*) CFDataGetBytePtr ((CFDataRef) out),
                             (size_t) CFDataGetLength    ((CFDataRef) out));
        CFRelease (out);
        return result;
    }
}

bool SecureStore::save (const Credentials& c)
{
    bool a = setItem (serviceName, "email",       c.email);
    bool b = setItem (serviceName, "license_key", c.licenseKey);
    return a && b;
}

Credentials SecureStore::load()
{
    Credentials c;
    c.email      = getItem (serviceName, "email");
    c.licenseKey = getItem (serviceName, "license_key");
    return c;
}

bool SecureStore::clear()
{
    deleteItem (serviceName, "email");
    deleteItem (serviceName, "license_key");
    return true;
}

#elif JUCE_WINDOWS

namespace
{
    bool setItem (const juce::String& service, const juce::String& account, const juce::String& value)
    {
        // Keep the juce::String alive while we hand its UTF-16 storage to Win32.
        const juce::String target = service + "/" + account;
        const auto bytes = value.toRawUTF8();
        const auto len   = (DWORD) value.getNumBytesAsUTF8();

        CREDENTIALW cred {};
        cred.Type             = CRED_TYPE_GENERIC;
        cred.TargetName       = const_cast<LPWSTR> ((LPCWSTR) target.toWideCharPointer());
        cred.CredentialBlobSize = len;
        cred.CredentialBlob   = (LPBYTE) const_cast<char*> (bytes);
        cred.Persist          = CRED_PERSIST_LOCAL_MACHINE;
        cred.UserName         = const_cast<LPWSTR> (L"rvzn");
        return CredWriteW (&cred, 0) != FALSE;
    }

    juce::String getItem (const juce::String& service, const juce::String& account)
    {
        const juce::String target = service + "/" + account;
        PCREDENTIALW cred = nullptr;
        if (! CredReadW ((LPCWSTR) target.toWideCharPointer(), CRED_TYPE_GENERIC, 0, &cred))
            return {};

        juce::String out ((const char*) cred->CredentialBlob,
                          (size_t) cred->CredentialBlobSize);
        CredFree (cred);
        return out;
    }

    void deleteItem (const juce::String& service, const juce::String& account)
    {
        const juce::String target = service + "/" + account;
        CredDeleteW ((LPCWSTR) target.toWideCharPointer(), CRED_TYPE_GENERIC, 0);
    }
}

bool SecureStore::save (const Credentials& c)
{
    return setItem (serviceName, "email",       c.email)
        && setItem (serviceName, "license_key", c.licenseKey);
}

Credentials SecureStore::load()
{
    Credentials c;
    c.email      = getItem (serviceName, "email");
    c.licenseKey = getItem (serviceName, "license_key");
    return c;
}

bool SecureStore::clear()
{
    deleteItem (serviceName, "email");
    deleteItem (serviceName, "license_key");
    return true;
}

#else

namespace
{
    juce::File storeFile (const juce::String& serviceName)
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                  .getChildFile ("RVZN")
                  .getChildFile (serviceName + ".lic");
    }
}

bool SecureStore::save (const Credentials& c)
{
    auto f = storeFile (serviceName);
    f.getParentDirectory().createDirectory();
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("e", c.email);
    obj->setProperty ("k", c.licenseKey);
    juce::var v (obj);
    return f.replaceWithText (juce::JSON::toString (v));
}

Credentials SecureStore::load()
{
    Credentials c;
    auto txt = storeFile (serviceName).loadFileAsString();
    if (txt.isEmpty()) return c;
    auto v = juce::JSON::parse (txt);
    if (auto* obj = v.getDynamicObject())
    {
        c.email      = obj->getProperty ("e").toString();
        c.licenseKey = obj->getProperty ("k").toString();
    }
    return c;
}

bool SecureStore::clear()
{
    storeFile (serviceName).deleteFile();
    return true;
}

#endif

}} // namespace rvzn::license
