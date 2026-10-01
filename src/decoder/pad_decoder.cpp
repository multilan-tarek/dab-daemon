//
// Created by tarek on 10/1/26.
//

#include "pad_decoder.h"
#include <algorithm>

PADDecoder::ContentIndicator::ContentIndicator(const size_t length, const int type) {
    this->length = length;
    this->type = type;
}

PADDecoder::ContentIndicator::ContentIndicator(const uint8_t value) {
    length = XPAD_LENGTHS[value >> 5];
    type = value & 0x1F;
}

PADDecoder::PADDecoder(const DynamicLabelCallback &dynamic_label_callback, const MOTCallback &mot_callback) {
    this->dynamic_label_callback = dynamic_label_callback;
    this->mot_callback = mot_callback;
}

void PADDecoder::reset() {
    last_content_indicator = {};
}

void PADDecoder::process(const std::vector<uint8_t> &xpad_data, const std::array<uint8_t, 2> &fpad_data) {
    if (xpad_data.empty()) {
        return;
    }

    const size_t xpad_length = std::min<size_t>(xpad_data.size(), 196);
    std::vector<uint8_t> xpad(xpad_length);

    for (size_t index = 0; index < xpad_length; index++) {
        xpad[index] = xpad_data[xpad_data.size() - 1 - index];
    }

    const uint8_t fpad_type = fpad_data[0] >> 6;
    const uint8_t xpad_indicator = (fpad_data[0] & 0x30) >> 4;
    const bool content_indicator_present = (fpad_data[1] & 0x02) != 0;

    const ContentIndicator previous_content_indicator = last_content_indicator;
    last_content_indicator = {};

    std::vector<ContentIndicator> content_indicators;
    size_t content_indicator_length = 0;

    if (fpad_type == 0) {
        if (content_indicator_present) {
            if (xpad_indicator == 1) {
                const int type = xpad[0] & 0x1F;

                if (type != 0) {
                    content_indicator_length = 1;
                    content_indicators.emplace_back(3, type);
                }
            } else if (xpad_indicator == 2) {
                for (size_t index = 0; index < 4; index++) {
                    if (index >= xpad.size()) {
                        return;
                    }

                    const uint8_t value = xpad[index];
                    content_indicator_length++;

                    if ((value & 0x1F) == 0) {
                        break;
                    }

                    content_indicators.emplace_back(value);
                }
            }
        } else if ((xpad_indicator == 1 || xpad_indicator == 2) && previous_content_indicator.type != -1) {
            content_indicators.push_back(previous_content_indicator);
        }
    }

    if (content_indicators.empty()) {
        last_content_indicator = previous_content_indicator;
        return;
    }

    size_t announced_length = content_indicator_length;

    for (const auto &content_indicator: content_indicators) {
        announced_length += content_indicator.length;
    }

    if (announced_length > xpad.size()) {
        return;
    }

    size_t offset = content_indicator_length;
    bool dynamic_label_continues = false;
    ContentIndicator mot_continuation;

    for (const auto &content_indicator: content_indicators) {
        if (offset + content_indicator.length > xpad.size()) {
            return;
        }

        if ((content_indicator.type == 2 || content_indicator.type == 3) && dynamic_label_callback) {
            dynamic_label_callback(content_indicator.type == 2, std::vector(xpad.begin() + offset, xpad.begin() + offset + content_indicator.length));
            dynamic_label_continues = true;
        } else if ((content_indicator.type == 1 || content_indicator.type == 12 || content_indicator.type == 13) && mot_callback) {
            mot_callback(content_indicator.type, std::vector(xpad.begin() + offset, xpad.begin() + offset + content_indicator.length));

            if (content_indicator.type == 12 || content_indicator.type == 13) {
                mot_continuation = ContentIndicator(content_indicator.length, 13);
            }
        }

        offset += content_indicator.length;
    }

    if (mot_continuation.type != -1) {
        last_content_indicator = mot_continuation;
    } else if (dynamic_label_continues) {
        last_content_indicator = ContentIndicator(offset, 3);
    }
}

const size_t PADDecoder::XPAD_LENGTHS[8] = {4, 6, 8, 12, 16, 24, 32, 48};
