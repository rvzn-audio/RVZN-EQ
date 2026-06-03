#pragma once

namespace rvzn { namespace license {

// Per-plugin license configuration. Each plugin overrides these values
// in its own copy of this header.
struct Config
{
    static constexpr const char* productCode          = "RVZN_EQ";
    static constexpr const char* productName          = "RVZN V1 EQUALIZER";
    static constexpr const char* verifyUrl            = "https://rvzn-audio.com/api/license/verify";
    static constexpr const char* deactivateUrl        = "https://rvzn-audio.com/api/license/deactivate";
    static constexpr int         offlineGraceLaunches = 7;
};

}} // namespace rvzn::license
