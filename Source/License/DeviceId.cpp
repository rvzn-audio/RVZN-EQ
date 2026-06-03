#include "DeviceId.h"

#if JUCE_MAC
 #include <IOKit/IOKitLib.h>
 #include <CoreFoundation/CoreFoundation.h>
#elif JUCE_WINDOWS
 #include <windows.h>
#endif

namespace rvzn { namespace license {

#if JUCE_MAC

static juce::String readPlatformId()
{
    io_service_t service = IOServiceGetMatchingService (0, IOServiceMatching ("IOPlatformExpertDevice"));
    if (service == 0)
        return {};

    juce::String result;
    if (CFTypeRef cfUUID = IORegistryEntryCreateCFProperty (service, CFSTR ("IOPlatformUUID"),
                                                            kCFAllocatorDefault, 0))
    {
        char buf[128] = {};
        if (CFStringGetCString ((CFStringRef) cfUUID, buf, sizeof (buf), kCFStringEncodingUTF8))
            result = juce::String (buf);
        CFRelease (cfUUID);
    }
    IOObjectRelease (service);
    return result;
}

#elif JUCE_WINDOWS

static juce::String readPlatformId()
{
    HKEY key {};
    if (RegOpenKeyExA (HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Cryptography",
                       0, KEY_READ | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS)
        return {};

    char buf[128] = {};
    DWORD len = sizeof (buf);
    DWORD type = REG_SZ;
    auto res = RegQueryValueExA (key, "MachineGuid", nullptr, &type, (LPBYTE) buf, &len);
    RegCloseKey (key);
    if (res != ERROR_SUCCESS)
        return {};
    return juce::String (buf);
}

#else

static juce::String readPlatformId()
{
    return juce::SystemStats::getDeviceIdentifiers().joinIntoString (";");
}

#endif

juce::String getDeviceId()
{
    juce::String raw = readPlatformId();

    if (raw.isEmpty())
        raw = juce::SystemStats::getDeviceIdentifiers().joinIntoString (";");
    if (raw.isEmpty())
        raw = juce::SystemStats::getComputerName() + ";" + juce::SystemStats::getLogonName();

    // Hash so the raw platform UUID never leaves the machine.
    const juce::SHA256 hash (raw.toRawUTF8(), raw.getNumBytesAsUTF8());
    auto hex = hash.toHexString();

    // Format as 8-4-4-4-12 for readability on the server side.
    if (hex.length() >= 32)
    {
        return hex.substring (0, 8) + "-" + hex.substring (8, 12) + "-"
             + hex.substring (12, 16) + "-" + hex.substring (16, 20) + "-"
             + hex.substring (20, 32);
    }
    return hex;
}

}} // namespace rvzn::license
