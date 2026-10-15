#ifndef SIGNAL_METRICS_H
#define SIGNAL_METRICS_H

#include <array>
#include <cstdint>

inline constexpr std::array<uint32_t, 7> DAB_ANTENNA_LEVEL_CER_THRESHOLDS{
    810,
    700,
    490,
    400,
    250,
    180,
    0
};

#endif
