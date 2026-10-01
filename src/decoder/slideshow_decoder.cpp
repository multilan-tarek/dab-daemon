//
// Created by tarek on 10/1/26.
//

#include "slideshow_decoder.h"
#include <algorithm>

void SlideShowDecoder::Segments::reset() {
    values.clear();
    last = -1;
}

bool SlideShowDecoder::Segments::complete() const {
    if (last < 0) {
        return false;
    }

    for (int index = 0; index <= last; index++) {
        if (!values.contains(static_cast<uint16_t>(index))) {
            return false;
        }
    }

    return true;
}

std::vector<uint8_t> SlideShowDecoder::Segments::get_data() const {
    std::vector<uint8_t> result;

    if (!complete()) {
        return result;
    }

    for (int index = 0; index <= last; index++) {
        const auto &segment = values.at(static_cast<uint16_t>(index));
        result.insert(result.end(), segment.begin(), segment.end());
    }

    return result;
}

SlideShowDecoder::SlideShowDecoder(const SlideCallback &slide_callback) {
    this->slide_callback = slide_callback;
}

void SlideShowDecoder::reset() {
    data_group_length = 0;
    data_group.clear();
    objects.clear();
}

void SlideShowDecoder::process(const int type, const std::vector<uint8_t> &data) {
    if (type == 1) {
        if (data.size() < 2) {
            return;
        }

        data_group_length = static_cast<size_t>((data[0] & 0x3F) << 8 | data[1]);

        if (data_group_length < 2 || data_group_length > 8191) {
            data_group_length = 0;
        }

        return;
    }

    if (type == 12) {
        data_group.clear();
    } else if (type != 13 || data_group.empty()) {
        return;
    }

    if (data_group_length == 0 || data_group.size() >= data_group_length) {
        return;
    }

    const size_t copy_length = std::min(data.size(), data_group_length - data_group.size());
    data_group.insert(data_group.end(), data.begin(), data.begin() + static_cast<std::ptrdiff_t>(copy_length));

    if (data_group.size() == data_group_length) {
        process_group(data_group);
        data_group.clear();
        data_group_length = 0;
    }
}

void SlideShowDecoder::process_group(const std::vector<uint8_t> &group) {
    if (group.size() < 4) {
        return;
    }

    const bool extension_present = (group[0] & 0x80) != 0;
    const bool crc_present = (group[0] & 0x40) != 0;
    const bool segment_present = (group[0] & 0x20) != 0;
    const bool user_access_present = (group[0] & 0x10) != 0;
    const uint8_t type = group[0] & 0x0F;
    const int continuity = group[1] >> 4;

    if (type != 3 && type != 4) {
        return;
    }

    size_t end = group.size();

    if (crc_present) {
        if (end < 2) {
            return;
        }

        const uint16_t stored_crc = static_cast<uint16_t>(group[end - 2] << 8 | group[end - 1]);

        if (stored_crc != calculate_crc(group.data(), end - 2)) {
            return;
        }

        end -= 2;
    }

    size_t offset = 2;

    if (extension_present) {
        offset += 2;
    }

    bool last = true;
    uint16_t segment_number = 0;

    if (segment_present) {
        if (offset + 2 > end) {
            return;
        }

        const uint16_t segment = static_cast<uint16_t>(group[offset] << 8 | group[offset + 1]);
        last = (segment & 0x8000) != 0;
        segment_number = segment & 0x7FFF;
        offset += 2;
    }

    if (!user_access_present || offset >= end) {
        return;
    }

    const bool transport_id_present = (group[offset] & 0x10) != 0;
    const size_t user_access_length = group[offset] & 0x0F;
    offset++;

    if (!transport_id_present || user_access_length < 2 || offset + user_access_length > end) {
        return;
    }

    const uint16_t transport_id = static_cast<uint16_t>(group[offset] << 8 | group[offset + 1]);
    offset += user_access_length;

    if (offset + 2 > end) {
        return;
    }

    const uint16_t segmentation_header = static_cast<uint16_t>(group[offset] << 8 | group[offset + 1]);
    const size_t segment_size = segmentation_header & 0x1FFF;
    offset += 2;

    if (segment_size > end - offset) {
        return;
    }

    if (segment_number > 2048) {
        return;
    }

    if (!objects.contains(transport_id) && objects.size() >= 64) {
        objects.erase(objects.begin());
    }

    Object &object = objects[transport_id];

    if (type == 3 && segment_number == 0 && object.header_continuity >= 0 && object.header_continuity != continuity) {
        object = {};
    } else if (type == 4 && segment_number == 0 && object.body_continuity >= 0 && object.body_continuity != continuity) {
        object.body_segments.reset();
        object.emitted = false;
    }

    if (type == 3) {
        object.header_continuity = continuity;
    } else {
        object.body_continuity = continuity;
    }

    Segments &segments = type == 3 ? object.header_segments : object.body_segments;
    segments.values[segment_number] = std::vector<uint8_t>(group.begin() + static_cast<std::ptrdiff_t>(offset), group.begin() + static_cast<std::ptrdiff_t>(offset + segment_size));

    if (last) {
        segments.last = segment_number;
    }

    update_object(transport_id);
}

void SlideShowDecoder::update_object(const uint16_t transport_id) {
    Object &object = objects[transport_id];

    if (object.header_segments.complete()) {
        const std::vector<uint8_t> header = object.header_segments.get_data();

        if (header.size() < 7) {
            return;
        }

        object.body_size = static_cast<uint32_t>(header[0]) << 20 | static_cast<uint32_t>(header[1]) << 12 | static_cast<uint32_t>(header[2]) << 4 | header[3] >> 4;
        object.header_size = static_cast<uint16_t>((header[3] & 0x0F) << 9 | header[4] << 1 | header[5] >> 7);
        object.content_type = header[5] >> 1 & 0x3F;
        object.content_subtype = static_cast<uint16_t>((header[5] & 0x01) << 8 | header[6]);
        object.header_complete = object.header_size >= 7 && object.header_size <= header.size() && object.body_size <= 4 * 1024 * 1024;
    }

    if (!object.header_complete || !object.body_segments.complete()) {
        return;
    }

    const std::vector<uint8_t> body = object.body_segments.get_data();

    if (body.size() != object.body_size) {
        return;
    }

    const std::string extension = detect_extension(body);

    if (extension.empty() || object.emitted) {
        return;
    }

    object.emitted = true;

    if (slide_callback) {
        slide_callback(body, extension);
    }
}

uint16_t SlideShowDecoder::calculate_crc(const uint8_t *data, const size_t length) {
    uint16_t crc = 0xFFFF;

    for (size_t offset = 0; offset < length; offset++) {
        crc ^= static_cast<uint16_t>(data[offset]) << 8;

        for (int bit = 0; bit < 8; bit++) {
            crc = (crc & 0x8000) != 0 ? static_cast<uint16_t>(crc << 1 ^ 0x1021) : static_cast<uint16_t>(crc << 1);
        }
    }

    return static_cast<uint16_t>(~crc);
}

std::string SlideShowDecoder::detect_extension(const std::vector<uint8_t> &data) {
    if (data.size() >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF) {
        return "jpg";
    }

    if (data.size() >= 8 && data[0] == 0x89 && data[1] == 0x50 && data[2] == 0x4E && data[3] == 0x47 && data[4] == 0x0D && data[5] == 0x0A && data[6] == 0x1A && data[7] == 0x0A) {
        return "png";
    }

    if (data.size() >= 6 && data[0] == 'G' && data[1] == 'I' && data[2] == 'F' && data[3] == '8' && (data[4] == '7' || data[4] == '9') && data[5] == 'a') {
        return "gif";
    }

    if (data.size() >= 12 && data[0] == 'R' && data[1] == 'I' && data[2] == 'F' && data[3] == 'F' && data[8] == 'W' && data[9] == 'E' && data[10] == 'B' && data[11] == 'P') {
        return "webp";
    }

    return {};
}
