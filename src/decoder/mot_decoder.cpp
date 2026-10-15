#include "mot_decoder.h"
#include <algorithm>
#include <zlib.h>

namespace {
constexpr size_t MAX_OBJECT_SIZE = 8 * 1024 * 1024;
constexpr size_t MAX_DIRECTORY_SIZE = 8 * 1024 * 1024;
constexpr size_t MAX_SEGMENTS = 4096;
constexpr size_t MAX_OBJECTS = 512;
}

void MOTDecoder::Segments::reset() {
    values.clear();
    last = -1;
}

bool MOTDecoder::Segments::complete() const {
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

std::vector<uint8_t> MOTDecoder::Segments::get_data() const {
    std::vector<uint8_t> result;

    if (!complete()) {
        return result;
    }

    for (int index = 0; index <= last; index++) {
        const auto &segment = values.at(static_cast<uint16_t>(index));

        if (result.size() + segment.size() > MAX_DIRECTORY_SIZE) {
            return {};
        }

        result.insert(result.end(), segment.begin(), segment.end());
    }

    return result;
}

MOTDecoder::MOTDecoder(const ObjectCallback &object_callback, const ObjectRemovedCallback &object_removed_callback) {
    this->object_callback = object_callback;
    this->object_removed_callback = object_removed_callback;
}

void MOTDecoder::reset() {
    xpad_data_group_length = 0;
    xpad_data_group.clear();
    xpad_application_type = 0xFF;
    objects.clear();
    directories.clear();
    active_directory_objects.clear();
#if DAB_ENABLE_DIAGNOSTICS
    statistics_groups_received.store(0, std::memory_order_relaxed);
    statistics_groups_accepted.store(0, std::memory_order_relaxed);
    statistics_group_crc_errors.store(0, std::memory_order_relaxed);
    statistics_segments.store(0, std::memory_order_relaxed);
    statistics_directories.store(0, std::memory_order_relaxed);
    statistics_objects.store(0, std::memory_order_relaxed);
#endif
}

void MOTDecoder::configure_xpad(const uint8_t application_type) {
    xpad_data_group_length = 0;
    xpad_data_group.clear();
    xpad_application_type = application_type;
}

void MOTDecoder::process_xpad(const int type, const std::vector<uint8_t> &data) {
    if (xpad_application_type == 0xFF) {
        return;
    }

    if (type == 1) {
        if (data.size() < 2) {
            return;
        }

        xpad_data_group_length = static_cast<size_t>((data[0] & 0x3F) << 8 | data[1]);

        if (xpad_data_group_length < 2 || xpad_data_group_length > 8191) {
            xpad_data_group_length = 0;
        }

        return;
    }

    if (type == xpad_application_type) {
        xpad_data_group.clear();
    } else if (type != xpad_application_type + 1 || xpad_data_group.empty()) {
        return;
    }

    if (xpad_data_group_length == 0 || xpad_data_group.size() >= xpad_data_group_length) {
        return;
    }

    const size_t copy_length = std::min(data.size(), xpad_data_group_length - xpad_data_group.size());
    xpad_data_group.insert(xpad_data_group.end(), data.begin(), data.begin() + static_cast<std::ptrdiff_t>(copy_length));

    if (xpad_data_group.size() == xpad_data_group_length) {
        process_group(xpad_data_group);
        xpad_data_group.clear();
        xpad_data_group_length = 0;
    }
}

void MOTDecoder::process_group(const std::vector<uint8_t> &group) {
#if DAB_ENABLE_DIAGNOSTICS
    statistics_groups_received.fetch_add(1, std::memory_order_relaxed);
#endif

    if (group.size() < 4) {
        return;
    }

    const bool extension_present = (group[0] & 0x80) != 0;
    const bool crc_present = (group[0] & 0x40) != 0;
    const bool segment_present = (group[0] & 0x20) != 0;
    const bool user_access_present = (group[0] & 0x10) != 0;
    const uint8_t type = group[0] & 0x0F;

    if (type != 3 && type != 4 && type != 6 && type != 7) {
        return;
    }

    size_t end = group.size();

    if (crc_present) {
        if (end < 2) {
            return;
        }

        const uint16_t stored_crc = static_cast<uint16_t>(group[end - 2] << 8 | group[end - 1]);

        if (stored_crc != calculate_crc(group.data(), end - 2)) {
#if DAB_ENABLE_DIAGNOSTICS
            statistics_group_crc_errors.fetch_add(1, std::memory_order_relaxed);
#endif
            return;
        }

        end -= 2;
    }

    size_t offset = 2;

    if (extension_present) {
        if (offset + 2 > end) {
            return;
        }

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

    if (segment_size > end - offset || segment_number >= MAX_SEGMENTS) {
        return;
    }

#if DAB_ENABLE_DIAGNOSTICS
    statistics_groups_accepted.fetch_add(1, std::memory_order_relaxed);
#endif
    process_segment(type, transport_id, segment_number, last, std::vector<uint8_t>(group.begin() + static_cast<std::ptrdiff_t>(offset), group.begin() + static_cast<std::ptrdiff_t>(offset + segment_size)));
}

void MOTDecoder::process_segment(const uint8_t type, const uint16_t transport_id, const uint16_t segment_number, const bool last, const std::vector<uint8_t> &data) {
#if DAB_ENABLE_DIAGNOSTICS
    statistics_segments.fetch_add(1, std::memory_order_relaxed);
#endif

    if (type == 6 || type == 7) {
        if (!directories.contains(transport_id) && directories.size() >= MAX_OBJECTS) {
            directories.erase(directories.begin());
        }

        DirectoryState &directory = directories[transport_id];

        if (directory.type != type) {
            directory = {};
        }

        if (directory.processed) {
            return;
        }

        directory.type = type;
        directory.segments.values[segment_number] = data;

        if (last) {
            directory.segments.last = segment_number;
        }

        if (directory.segments.complete()) {
            process_directory(transport_id, type);
        }

        return;
    }

    if (!objects.contains(transport_id) && objects.size() >= MAX_OBJECTS) {
        objects.erase(objects.begin());
    }

    ObjectState &object = objects[transport_id];

    // TransportId identifies the version; continuity changes on carousel passes.
    if (object.emitted || (type == 3 && object.header_complete)) {
        return;
    }

    if (type == 3) {
        object.header_segments.values[segment_number] = data;

        if (last) {
            object.header_segments.last = segment_number;
        }

        if (object.header_segments.complete()) {
            parse_header(transport_id, object.header_segments.get_data());
        }
    } else {
        object.body_segments.values[segment_number] = data;

        if (last) {
            object.body_segments.last = segment_number;
        }
    }

    update_object(transport_id);
}

void MOTDecoder::process_directory(const uint16_t transport_id, const uint8_t type) {
    const auto directory_iterator = directories.find(transport_id);

    if (directory_iterator == directories.end()) {
        return;
    }

    const std::vector<uint8_t> entity = directory_iterator->second.segments.get_data();

    if (entity.empty()) {
        return;
    }

    std::vector<uint8_t> directory = entity;

    if (type == 7) {
        if (entity.size() < 9 || (entity[0] & 0x80) == 0 || (entity[0] & 0x40) != 0) {
            return;
        }

        const size_t entity_size = static_cast<size_t>(entity[0] & 0x3F) << 24 | static_cast<size_t>(entity[1]) << 16 | static_cast<size_t>(entity[2]) << 8 | entity[3];
        const uint8_t compression_id = entity[4];
        const size_t uncompressed_length = static_cast<size_t>(entity[5] & 0x3F) << 24 | static_cast<size_t>(entity[6]) << 16 | static_cast<size_t>(entity[7]) << 8 | entity[8];

        if (entity_size != entity.size() || uncompressed_length > MAX_DIRECTORY_SIZE || compression_id != 1) {
            return;
        }

        if (!decompress_gzip(entity.data() + 9, entity.size() - 9, uncompressed_length, directory)) {
            return;
        }
    }

    if (parse_directory(directory)) {
#if DAB_ENABLE_DIAGNOSTICS
        statistics_directories.fetch_add(1, std::memory_order_relaxed);
#endif
        directory_iterator->second.processed = true;
        directory_iterator->second.segments.reset();
    }
}

bool MOTDecoder::parse_directory(const std::vector<uint8_t> &data) {
    if (data.size() < 13 || (data[0] & 0xC0) != 0) {
        return false;
    }

    const size_t directory_size = static_cast<size_t>(data[0] & 0x3F) << 24 | static_cast<size_t>(data[1]) << 16 | static_cast<size_t>(data[2]) << 8 | data[3];
    const size_t object_count = static_cast<size_t>(data[4] << 8 | data[5]);
    const size_t extension_length = static_cast<size_t>(data[11] << 8 | data[12]);

    if (directory_size != data.size() || extension_length > data.size() - 13 || object_count > MAX_OBJECTS) {
        return false;
    }

    size_t offset = 13 + extension_length;
    std::vector<std::pair<uint16_t, std::vector<uint8_t>>> headers;
    headers.reserve(object_count);

    for (size_t index = 0; index < object_count; index++) {
        if (offset + 9 > data.size()) {
            return false;
        }

        const uint16_t transport_id = static_cast<uint16_t>(data[offset] << 8 | data[offset + 1]);
        offset += 2;
        const size_t header_size = static_cast<size_t>((data[offset + 3] & 0x0F) << 9 | data[offset + 4] << 1 | data[offset + 5] >> 7);

        if (header_size < 7 || header_size > data.size() - offset) {
            return false;
        }

        headers.emplace_back(transport_id, std::vector<uint8_t>(data.begin() + static_cast<std::ptrdiff_t>(offset), data.begin() + static_cast<std::ptrdiff_t>(offset + header_size)));
        offset += header_size;
    }

    if (offset != data.size()) {
        return false;
    }

    std::vector<uint16_t> directory_objects;
    directory_objects.reserve(headers.size());

    for (const auto &[transport_id, header]: headers) {
        if (!parse_header(transport_id, header)) {
            return false;
        }

        directory_objects.push_back(transport_id);
        update_object(transport_id);
    }

    for (const uint16_t transport_id: active_directory_objects) {
        if (std::find(directory_objects.begin(), directory_objects.end(), transport_id) != directory_objects.end()) {
            continue;
        }

        objects.erase(transport_id);

        if (object_removed_callback) {
            object_removed_callback(transport_id);
        }
    }

    active_directory_objects = std::move(directory_objects);

    return true;
}

bool MOTDecoder::parse_header(const uint16_t transport_id, const std::vector<uint8_t> &header) {
    const auto existing = objects.find(transport_id);

    if (existing != objects.end() && existing->second.header_complete) {
        return true;
    }

    if (header.size() < 7) {
        return false;
    }

    const uint32_t body_size = static_cast<uint32_t>(header[0]) << 20 | static_cast<uint32_t>(header[1]) << 12 | static_cast<uint32_t>(header[2]) << 4 | header[3] >> 4;
    const size_t header_size = static_cast<size_t>((header[3] & 0x0F) << 9 | header[4] << 1 | header[5] >> 7);

    if (header_size < 7 || header_size > header.size() || body_size > MAX_OBJECT_SIZE) {
        return false;
    }

    Object parsed;
    parsed.transport_id = transport_id;
    parsed.body_size = body_size;
    parsed.content_type = header[5] >> 1 & 0x3F;
    parsed.content_subtype = static_cast<uint16_t>((header[5] & 0x01) << 8 | header[6]);
    size_t offset = 7;

    while (offset < header_size) {
        uint8_t parameter_id = 0;
        std::vector<uint8_t> parameter_data;

        if (!read_parameter(header, offset, parameter_id, parameter_data)) {
            return false;
        }

        if (parameter_id == 0x0C && !parameter_data.empty()) {
            parsed.content_name.assign(parameter_data.begin() + 1, parameter_data.end());
        } else if (parameter_id == 0x10) {
            parsed.mime_type.assign(parameter_data.begin(), parameter_data.end());
        } else if (parameter_id == 0x11 && parameter_data.size() == 1) {
            parsed.compression_type = parameter_data[0];
        } else if (parameter_id == 0x21) {
            parsed.profile_subset = parameter_data;
        } else if (parameter_id == 0x23) {
            return false;
        } else if (parameter_id == 0x25) {
            parsed.scope_start = parameter_data;
        } else if (parameter_id == 0x26) {
            parsed.scope_end = parameter_data;
        } else if (parameter_id == 0x27) {
            parsed.scope_id = parameter_data;
        }
    }

    ObjectState &state = objects[transport_id];
    state.object = std::move(parsed);
    state.header_complete = true;
    state.emitted = false;

    return true;
}

void MOTDecoder::update_object(const uint16_t transport_id) {
    const auto iterator = objects.find(transport_id);

    if (iterator == objects.end()) {
        return;
    }

    ObjectState &state = iterator->second;

    if (!state.header_complete || !state.body_segments.complete() || state.emitted) {
        return;
    }

    std::vector<uint8_t> body = state.body_segments.get_data();

    if (body.size() != state.object.body_size) {
        return;
    }

    if (state.object.compression_type >= 0) {
        if (state.object.compression_type != 1) {
            return;
        }

        std::vector<uint8_t> decompressed;

        if (!decompress_gzip(body.data(), body.size(), 0, decompressed)) {
            return;
        }

        body = std::move(decompressed);
    }

    if (body.size() > MAX_OBJECT_SIZE) {
        return;
    }

    state.object.body = std::move(body);
    state.emitted = true;
#if DAB_ENABLE_DIAGNOSTICS
    statistics_objects.fetch_add(1, std::memory_order_relaxed);
#endif

    if (object_callback) {
        object_callback(state.object);
    }
}

#if DAB_ENABLE_DIAGNOSTICS
MOTDecoder::Statistics MOTDecoder::get_statistics() const {
    return {
        statistics_groups_received.load(std::memory_order_relaxed),
        statistics_groups_accepted.load(std::memory_order_relaxed),
        statistics_group_crc_errors.load(std::memory_order_relaxed),
        statistics_segments.load(std::memory_order_relaxed),
        statistics_directories.load(std::memory_order_relaxed),
        statistics_objects.load(std::memory_order_relaxed)
    };
}
#endif

bool MOTDecoder::read_parameter(const std::vector<uint8_t> &data, size_t &offset, uint8_t &parameter_id, std::vector<uint8_t> &parameter_data) {
    if (offset >= data.size()) {
        return false;
    }

    const uint8_t header = data[offset++];
    const uint8_t length_indicator = header >> 6;
    parameter_id = header & 0x3F;
    size_t length = 0;

    if (length_indicator == 1) {
        length = 1;
    } else if (length_indicator == 2) {
        length = 4;
    } else if (length_indicator == 3) {
        if (offset >= data.size()) {
            return false;
        }

        const uint8_t first_length = data[offset++];

        if ((first_length & 0x80) == 0) {
            length = first_length;
        } else {
            if (offset >= data.size()) {
                return false;
            }

            length = static_cast<size_t>(first_length & 0x7F) << 8 | data[offset++];
        }
    }

    if (length > data.size() - offset) {
        return false;
    }

    parameter_data.assign(data.begin() + static_cast<std::ptrdiff_t>(offset), data.begin() + static_cast<std::ptrdiff_t>(offset + length));
    offset += length;
    return true;
}

bool MOTDecoder::decompress_gzip(const uint8_t *data, const size_t length, const size_t expected_length, std::vector<uint8_t> &result) {
    if (length == 0 || expected_length > MAX_OBJECT_SIZE) {
        return false;
    }

    z_stream stream{};
    stream.next_in = const_cast<Bytef *>(reinterpret_cast<const Bytef *>(data));
    stream.avail_in = static_cast<uInt>(length);

    if (inflateInit2(&stream, 16 + MAX_WBITS) != Z_OK) {
        return false;
    }

    result.clear();
    result.reserve(expected_length != 0 ? expected_length : std::min<size_t>(length * 4, MAX_OBJECT_SIZE));
    uint8_t output[4096];
    int status = Z_OK;

    while (status == Z_OK) {
        stream.next_out = output;
        stream.avail_out = sizeof(output);
        status = inflate(&stream, Z_NO_FLUSH);
        const size_t produced = sizeof(output) - stream.avail_out;

        if (result.size() + produced > MAX_OBJECT_SIZE) {
            inflateEnd(&stream);
            result.clear();
            return false;
        }

        result.insert(result.end(), output, output + produced);
    }

    inflateEnd(&stream);

    if (status != Z_STREAM_END || (expected_length != 0 && result.size() != expected_length)) {
        result.clear();
        return false;
    }

    return true;
}

uint16_t MOTDecoder::calculate_crc(const uint8_t *data, const size_t length) {
    uint16_t crc = 0xFFFF;

    for (size_t offset = 0; offset < length; offset++) {
        crc ^= static_cast<uint16_t>(data[offset]) << 8;

        for (int bit = 0; bit < 8; bit++) {
            crc = (crc & 0x8000) != 0 ? static_cast<uint16_t>(crc << 1 ^ 0x1021) : static_cast<uint16_t>(crc << 1);
        }
    }

    return static_cast<uint16_t>(~crc);
}
