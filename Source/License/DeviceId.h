#pragma once
#include <JuceHeader.h>

namespace rvzn { namespace license {

// Stable per-machine identifier. Derived from platform hardware ID
// (IOPlatformUUID on macOS, MachineGuid on Windows) and hashed so the
// raw value never leaves the device.
juce::String getDeviceId();

}} // namespace rvzn::license
