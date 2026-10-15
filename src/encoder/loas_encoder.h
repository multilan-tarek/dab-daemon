#ifndef LOAS_ENCODER_H
#define LOAS_ENCODER_H

#include <cstdint>
#include <vector>

class LOASEncoder {
public:
    static std::vector<uint8_t> encode(const std::vector<uint8_t> &access_unit, uint32_t sampling_rate, uint8_t channels, bool sbr);
};

#endif //LOAS_ENCODER_H
