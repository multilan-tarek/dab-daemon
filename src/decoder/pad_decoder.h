//
// Created by tarek on 10/1/26.
//

#ifndef PAD_DECODER_H
#define PAD_DECODER_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

class PADDecoder {
public:
    using DynamicLabelCallback = std::function<void(bool, const std::vector<uint8_t> &)>;
    using MOTCallback = std::function<void(int, const std::vector<uint8_t> &)>;

    PADDecoder(const DynamicLabelCallback &dynamic_label_callback, const MOTCallback &mot_callback);

    void reset();

    void process(const std::vector<uint8_t> &xpad_data, const std::array<uint8_t, 2> &fpad_data);

private:
    struct ContentIndicator {
        size_t length = 0;
        int type = -1;

        ContentIndicator() = default;

        ContentIndicator(size_t length, int type);

        explicit ContentIndicator(uint8_t value);
    };

    DynamicLabelCallback dynamic_label_callback = nullptr;
    MOTCallback mot_callback = nullptr;
    ContentIndicator last_content_indicator;

    static const size_t XPAD_LENGTHS[8];
};

#endif //PAD_DECODER_H
