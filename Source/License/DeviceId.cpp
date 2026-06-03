// Platform headers are included BEFORE "DeviceId.h" so that the Carbon-
// era ::Point typedef in <MacTypes.h> (pulled in via CoreFoundation) lands
// in scope before juce::Point - otherwise the two collide.
#if defined (__APPLE__)
 #include <IOKit/IOKitLib.h>
 #include <CoreFoundation/CoreFoundation.h>
#elif defined (_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif

#include "DeviceId.h"

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
    auto id = readPlatformId().trim();

    if (id.isEmpty())
        id = juce::SystemStats::getDeviceIdentifiers().joinIntoString ("-");
    if (id.isEmpty())
        id = juce::SystemStats::getComputerName() + "-" + juce::SystemStats::getLogonName();

    return id;
}

}} // namespace rvzn::license
