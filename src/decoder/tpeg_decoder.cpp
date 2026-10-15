#include "tpeg_decoder.h"
#include <algorithm>
#include <array>

namespace {
constexpr size_t MAX_GROUP_SIZE = 64 * 1024;
constexpr uint8_t DATA_GROUP_TYPE_GENERAL = 0;
}

TPEGDecoder::TPEGDecoder(const UpdateCallback &update_callback, const FrameCallback &frame_callback) {
    this->update_callback = update_callback;
    this->frame_callback = frame_callback;
}

void TPEGDecoder::reset() {
    const bool changed = status != Status{} || !services.empty();
    status = {};
    services.clear();
    sequence = 0;
    continuity_valid = false;
    continuity_index = 0;

    if (changed) {
        emit_update();
    }
}

void TPEGDecoder::process_group(const std::vector<uint8_t> &group) {
    if (group.size() < 2 || group.size() > MAX_GROUP_SIZE) {
        status.malformed_frames++;
        emit_update();
        return;
    }

    const bool extension_present = (group[0] & 0x80) != 0;
    const bool crc_present = (group[0] & 0x40) != 0;
    const bool segment_present = (group[0] & 0x20) != 0;
    const bool user_access_present = (group[0] & 0x10) != 0;
    const uint8_t type = group[0] & 0x0F;
    const uint8_t current_continuity_index = group[1] >> 4;
    const uint8_t repetition_index = group[1] & 0x0F;

    if (extension_present || segment_present || user_access_present || type != DATA_GROUP_TYPE_GENERAL || repetition_index != 0) {
        status.malformed_frames++;
        emit_update();
        return;
    }

    size_t end = group.size();

    if (crc_present) {
        if (end < 4) {
            status.malformed_frames++;
            emit_update();
            return;
        }

        const uint16_t stored_crc = read_uint16(group.data() + end - 2);

        if (stored_crc != calculate_crc(group.data(), end - 2)) {
            status.group_crc_errors++;
            emit_update();
            return;
        }

        end -= 2;
    }

    if (end <= 2) {
        status.malformed_frames++;
        emit_update();
        return;
    }

    status.groups_received++;

    if (continuity_valid && current_continuity_index != static_cast<uint8_t>((continuity_index + 1) & 0x0F)) {
        status.continuity_errors++;
    }

    continuity_valid = true;
    continuity_index = current_continuity_index;
    process_stream(group.data() + 2, end - 2);
    emit_update();
}

void TPEGDecoder::process_stream(const uint8_t *data, const size_t length) {
    size_t offset = 0;
    bool frame_found = false;

    while (offset < length) {
        while (offset < length && data[offset] == 0) {
            offset++;
        }

        if (offset == length) {
            break;
        }

        if (length - offset < 7 || data[offset] != 0xFF || data[offset + 1] != 0x0F) {
            status.malformed_frames++;
            return;
        }

        const size_t service_length = read_uint16(data + offset + 2);
        const size_t frame_length = 7 + service_length;

        if (frame_length > length - offset) {
            status.malformed_frames++;
            return;
        }

        const uint16_t stored_crc = read_uint16(data + offset + 4);
        const uint8_t frame_type = data[offset + 6];
        const size_t crc_service_length = std::min<size_t>(11, service_length);
        std::array<uint8_t, 16> crc_data{};
        size_t crc_length = 0;
        crc_data[crc_length++] = data[offset];
        crc_data[crc_length++] = data[offset + 1];
        crc_data[crc_length++] = data[offset + 2];
        crc_data[crc_length++] = data[offset + 3];
        crc_data[crc_length++] = frame_type;

        for (size_t index = 0; index < crc_service_length; index++) {
            crc_data[crc_length++] = data[offset + 7 + index];
        }

        if (stored_crc != calculate_crc(crc_data.data(), crc_length)) {
            status.transport_crc_errors++;
            return;
        }

        if (!process_transport_frame(data + offset, frame_length)) {
            status.malformed_frames++;
            return;
        }

        frame_found = true;
        offset += frame_length;
    }

    if (!frame_found) {
        status.malformed_frames++;
    }
}

bool TPEGDecoder::process_transport_frame(const uint8_t *data, const size_t length) {
    if (length < 7 || data[0] != 0xFF || data[1] != 0x0F) {
        return false;
    }

    const size_t service_length = read_uint16(data + 2);

    if (length != 7 + service_length) {
        return false;
    }

    Frame frame;
    frame.type = data[6];
    bool valid = false;

    if (frame.type == 0) {
        valid = process_directory(data + 7, service_length, frame);
    } else if (frame.type == 1) {
        valid = process_service_data(data + 7, service_length, frame);
    } else {
        frame.data.assign(data + 7, data + 7 + service_length);
        valid = true;
    }

    if (!valid) {
        return false;
    }

    frame.sequence = ++sequence;
    status.frames_received++;

    if (frame_callback) {
        frame_callback(frame);
    }

    return true;
}

bool TPEGDecoder::process_directory(const uint8_t *data, const size_t length, Frame &frame) {
    if (length < 3) {
        return false;
    }

    const size_t service_count = data[0];
    const size_t expected_length = 1 + service_count * 3 + 2;

    if (expected_length != length) {
        return false;
    }

    const uint16_t stored_crc = read_uint16(data + length - 2);

    if (stored_crc != calculate_crc(data, length - 2)) {
        status.directory_crc_errors++;
        return false;
    }

    std::vector<ServiceIdentifier> new_services;
    new_services.reserve(service_count);

    for (size_t index = 0; index < service_count; index++) {
        const size_t offset = 1 + index * 3;
        new_services.push_back({data[offset], data[offset + 1], data[offset + 2]});
    }

    services = std::move(new_services);
    status.service_count = static_cast<uint16_t>(services.size());
    frame.data.assign(data, data + length);
    return true;
}

bool TPEGDecoder::process_service_data(const uint8_t *data, const size_t length, Frame &frame) {
    if (length < 4) {
        return false;
    }

    frame.service = {data[0], data[1], data[2]};
    frame.encryption = data[3];
    frame.data.assign(data, data + length);

    if (frame.encryption != 0) {
        return true;
    }

    size_t offset = 4;

    while (offset < length) {
        if (length - offset < 5) {
            return false;
        }

        Component component;
        component.id = data[offset];
        const size_t field_length = read_uint16(data + offset + 1);
        component.header_crc = read_uint16(data + offset + 3);
        offset += 5;

        if (field_length > length - offset) {
            return false;
        }

        component.data.assign(data + offset, data + offset + field_length);
        std::array<uint8_t, 16> crc_data{};
        size_t crc_length = 0;
        crc_data[crc_length++] = component.id;
        crc_data[crc_length++] = static_cast<uint8_t>(field_length >> 8);
        crc_data[crc_length++] = static_cast<uint8_t>(field_length);
        const size_t crc_component_length = std::min<size_t>(13, field_length);

        for (size_t index = 0; index < crc_component_length; index++) {
            crc_data[crc_length++] = component.data[index];
        }

        if (component.header_crc == calculate_crc(crc_data.data(), crc_length)) {
            frame.components.push_back(std::move(component));
        } else {
            status.component_crc_errors++;
        }

        offset += field_length;
    }

    return true;
}

uint16_t TPEGDecoder::calculate_crc(const uint8_t *data, const size_t length) {
    uint16_t crc = 0xFFFF;

    for (size_t index = 0; index < length; index++) {
        crc ^= static_cast<uint16_t>(data[index]) << 8;

        for (int bit = 0; bit < 8; bit++) {
            crc = (crc & 0x8000) != 0 ? static_cast<uint16_t>(crc << 1 ^ 0x1021) : static_cast<uint16_t>(crc << 1);
        }
    }

    return static_cast<uint16_t>(~crc);
}

uint16_t TPEGDecoder::read_uint16(const uint8_t *data) {
    return static_cast<uint16_t>(data[0] << 8 | data[1]);
}

void TPEGDecoder::emit_update() const {
    if (update_callback) {
        update_callback(status, services);
    }
}
