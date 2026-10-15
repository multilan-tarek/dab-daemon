#include "journaline_decoder.h"
#include <algorithm>
#include <ranges>
#include <zlib.h>

namespace {
constexpr size_t MAX_OBJECT_SIZE = 4092;
constexpr size_t MAX_DATA_GROUP_SIZE = 4096;
constexpr int64_t JOURNALINE_EPOCH = 946684800;

std::string bytes_to_string(const std::vector<uint8_t> &data, const size_t start, const size_t end) {
    if (start >= end || end > data.size()) {
        return {};
    }

    return std::string(data.begin() + static_cast<std::ptrdiff_t>(start), data.begin() + static_cast<std::ptrdiff_t>(end));
}

void append_text(std::string *text, const uint8_t value) {
    if (text) {
        text->push_back(static_cast<char>(value));
    }
}
}

JournalineDecoder::JournalineDecoder(const UpdateCallback &update_callback) {
    this->update_callback = update_callback;
}

void JournalineDecoder::reset() {
    const bool changed = status != Status{} || !objects.empty();
    status = {};
    objects.clear();
    toc_fragments.clear();
    toc_block_count = 0;
    service_expiry_present = false;
    service_expiry = 0;
    xpad_data_group_length = 0;
    xpad_data_group.clear();
    xpad_application_type = 0xFF;

    if (changed) {
        emit_update();
    }
}

bool JournalineDecoder::configure(const std::vector<uint8_t> &parameters) {
    if (parameters.size() < 2) {
        return false;
    }

    const Status previous_status = status;
    status.configured = true;
    status.version = parameters[0];
    status.extended_header_length = parameters[1];
    status.point_of_entry = parameters.size() >= 4 ? static_cast<uint16_t>(parameters[2] << 8 | parameters[3]) : 0;

    if (status != previous_status) {
        emit_update();
    }

    return true;
}

bool JournalineDecoder::configure_xpad(const uint8_t application_type, const std::vector<uint8_t> &parameters) {
    if (!configure(parameters)) {
        return false;
    }

    xpad_application_type = application_type;
    xpad_data_group_length = 0;
    xpad_data_group.clear();
    return true;
}

void JournalineDecoder::process_xpad(const int type, const std::vector<uint8_t> &data) {
    if (xpad_application_type == 0xFF) {
        return;
    }

    if (type == 1) {
        if (data.size() < 2) {
            return;
        }

        xpad_data_group_length = static_cast<size_t>((data[0] & 0x3F) << 8 | data[1]);

        if (xpad_data_group_length < 4 || xpad_data_group_length > MAX_DATA_GROUP_SIZE) {
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

void JournalineDecoder::process_group(const std::vector<uint8_t> &group) {
    if (group.size() < 4 || group.size() > MAX_DATA_GROUP_SIZE) {
        return;
    }

    const bool extension_present = (group[0] & 0x80) != 0;
    const bool crc_present = (group[0] & 0x40) != 0;
    const bool segment_present = (group[0] & 0x20) != 0;
    const bool user_access_present = (group[0] & 0x10) != 0;
    const uint8_t type = group[0] & 0x0F;

    if (extension_present || !crc_present || segment_present || user_access_present || (type != 0 && type != 6)) {
        return;
    }

    const uint16_t stored_crc = static_cast<uint16_t>(group[group.size() - 2] << 8 | group[group.size() - 1]);

    if (stored_crc != calculate_crc(group.data(), group.size() - 2)) {
        return;
    }

    const std::vector<uint8_t> data(group.begin() + 2, group.end() - 2);
    bool changed = false;

    if (type == 0) {
        changed = process_object(data);
    } else {
        changed = process_management(data);
    }

    if (changed) {
        expire();
        emit_update();
    }
}

void JournalineDecoder::update_time(const int64_t unix_time) {
    current_time = unix_time;
    const size_t previous_size = objects.size();
    const bool service_active = service_expiry_present;
    expire();

    if (objects.size() != previous_size || service_active != service_expiry_present) {
        emit_update();
    }
}

std::vector<JournalineDecoder::IndexEntry> JournalineDecoder::get_index() const {
    std::vector<IndexEntry> result;
    result.reserve(objects.size());

    for (const auto &object: objects | std::views::values) {
        result.push_back({object.id, object.type, object.static_content, object.revision, object.title});
    }

    return result;
}

std::vector<JournalineDecoder::Object> JournalineDecoder::get_object(const uint16_t object_id) const {
    const auto object = objects.find(object_id);

    if (object == objects.end()) {
        return {};
    }

    return {object->second};
}

bool JournalineDecoder::process_object(const std::vector<uint8_t> &data) {
    if (data.size() < 3 || data.size() > MAX_OBJECT_SIZE) {
        return false;
    }

    Object object;
    object.id = static_cast<uint16_t>(data[0] << 8 | data[1]);
    const uint8_t description = data[2];
    object.type = description >> 5;
    object.static_content = (description & 0x10) != 0;
    const bool compressed = (description & 0x08) != 0;
    object.revision = description & 0x07;

    if (object.type < 1 || object.type > 4) {
        return false;
    }

    size_t offset = 3 + status.extended_header_length;

    if (offset > data.size()) {
        return false;
    }

    std::vector<uint8_t> content;

    if (compressed) {
        if (offset >= data.size() || data[offset] != 0x08) {
            return false;
        }

        offset++;

        if (!decompress_deflate(data.data() + offset, data.size() - offset, content)) {
            return false;
        }
    } else {
        content.assign(data.begin() + static_cast<std::ptrdiff_t>(offset), data.end());
    }

    if (3 + status.extended_header_length + content.size() > MAX_OBJECT_SIZE || !parse_content(object, content)) {
        return false;
    }

    const auto existing = objects.find(object.id);
    const bool changed = existing == objects.end() || existing->second != object;
    objects[object.id] = std::move(object);
    return changed;
}

bool JournalineDecoder::process_management(const std::vector<uint8_t> &data) {
    if (data.size() < 13 || data[0] != 0x54) {
        return false;
    }

    const uint8_t revision = data[1];
    const uint8_t block_count = data[2];
    const uint8_t block_index = data[3];
    const uint16_t preceding_object_id = static_cast<uint16_t>(data[4] << 8 | data[5]);
    const uint16_t object_count = static_cast<uint16_t>(data[6] << 8 | data[7]);
    const uint16_t timeout = static_cast<uint16_t>(data[8] << 8 | data[9]);
    const size_t entity_length = data[10];
    const size_t extended_header_length = static_cast<size_t>(data[11] << 8 | data[12]);

    if (block_count == 0 || block_index >= block_count || entity_length < 3 || 13 + extended_header_length >= data.size()) {
        return false;
    }

    const size_t entities_offset = 13 + extended_header_length;
    const size_t entity_bytes = data.size() - entities_offset;

    if (entity_bytes < entity_length || entity_bytes % entity_length != 0) {
        return false;
    }

    if (!status.toc_present || status.toc_revision != revision || toc_block_count != block_count) {
        toc_fragments.clear();
        toc_block_count = block_count;
    }

    TocFragment fragment;
    fragment.preceding_object_id = preceding_object_id;

    for (size_t offset = entities_offset; offset + entity_length <= data.size(); offset += entity_length) {
        const uint16_t object_id = static_cast<uint16_t>(data[offset] << 8 | data[offset + 1]);

        if (!fragment.object_ids.empty() && object_id <= fragment.object_ids.back()) {
            return false;
        }

        fragment.object_ids.push_back(object_id);
    }

    if (fragment.object_ids.empty()) {
        return false;
    }

    bool changed = false;
    const uint16_t first_object_id = fragment.object_ids.front();

    for (auto iterator = objects.begin(); iterator != objects.end();) {
        if (iterator->first > preceding_object_id && iterator->first < first_object_id) {
            iterator = objects.erase(iterator);
            changed = true;
        } else {
            ++iterator;
        }
    }

    toc_fragments[block_index] = fragment;
    const Status previous_status = status;
    status.toc_present = true;
    status.toc_revision = revision;
    status.object_count = object_count;
    status.timeout_minutes = timeout;

    if (timeout != 0 && current_time != 0) {
        service_expiry_present = true;
        service_expiry = current_time + static_cast<int64_t>(timeout) * 60;
    } else if (timeout == 0) {
        service_expiry_present = false;
        service_expiry = 0;
    }

    changed = changed || status != previous_status;

    if (toc_fragments.size() == block_count) {
        std::vector<uint16_t> active_objects;

        for (uint16_t index = 0; index < block_count; index++) {
            const auto fragment_iterator = toc_fragments.find(static_cast<uint8_t>(index));

            if (fragment_iterator == toc_fragments.end()) {
                return changed;
            }

            active_objects.insert(active_objects.end(), fragment_iterator->second.object_ids.begin(), fragment_iterator->second.object_ids.end());
        }

        std::ranges::sort(active_objects);
        active_objects.erase(std::unique(active_objects.begin(), active_objects.end()), active_objects.end());

        for (auto iterator = objects.begin(); iterator != objects.end();) {
            if (!std::ranges::binary_search(active_objects, iterator->first)) {
                iterator = objects.erase(iterator);
                changed = true;
            } else {
                ++iterator;
            }
        }
    }

    return changed;
}

bool JournalineDecoder::parse_content(Object &object, const std::vector<uint8_t> &content) const {
    std::string *text = nullptr;
    Link *link = nullptr;
    std::vector<std::string> *item = nullptr;

    for (size_t offset = 0; offset < content.size();) {
        const uint8_t value = content[offset++];

        if (value >= 0x20) {
            append_text(text, value);
            continue;
        }

        if (value <= 0x0F) {
            if (value > 0x05) {
                continue;
            }

            link = nullptr;
            item = nullptr;
            text = nullptr;

            if (value == 0x00) {
                continue;
            }

            if (value == 0x01) {
                text = &object.title;
            } else if (value == 0x02) {
                if (offset + 2 > content.size()) {
                    return false;
                }

                object.links.push_back({static_cast<uint16_t>(content[offset] << 8 | content[offset + 1]), {}});
                offset += 2;
                link = &object.links.back();
                text = &link->text;
            } else if (value == 0x03) {
                text = &object.body;
            } else if (value == 0x04) {
                object.items.emplace_back();
                object.items.back().emplace_back();
                item = &object.items.back();
                text = &item->back();
            } else if (value == 0x05) {
                if (object.items.empty()) {
                    object.items.emplace_back();
                }

                item = &object.items.back();
                item->emplace_back();
                text = &item->back();
            }

            continue;
        }

        if (value == 0x10) {
            append_text(text, '\n');
        } else if (value == 0x1A) {
            offset--;
            std::vector<uint8_t> payload;

            if (!parse_data_section(content, offset, payload)) {
                return false;
            }

            apply_data_section(object, payload, current_time);
        } else if (value == 0x1B) {
            return false;
        } else if (value == 0x1C || value == 0x1D) {
            if (offset >= content.size()) {
                return false;
            }

            offset++;
        }
    }

    return true;
}

bool JournalineDecoder::parse_data_section(const std::vector<uint8_t> &content, size_t &offset, std::vector<uint8_t> &payload) {
    if (offset + 2 > content.size() || content[offset] != 0x1A) {
        return false;
    }

    while (offset + 2 <= content.size()) {
        const uint8_t code = content[offset++];

        if (code != 0x1A && code != 0x1B) {
            return false;
        }

        const size_t length = static_cast<size_t>(content[offset++]) + 1;

        if (offset + length > content.size()) {
            return false;
        }

        payload.insert(payload.end(), content.begin() + static_cast<std::ptrdiff_t>(offset), content.begin() + static_cast<std::ptrdiff_t>(offset + length));
        offset += length;

        if (length != 256 || offset >= content.size() || content[offset] != 0x1B) {
            break;
        }
    }

    return !payload.empty();
}

void JournalineDecoder::apply_data_section(Object &object, const std::vector<uint8_t> &payload, const int64_t current_time) {
    if (payload.empty()) {
        return;
    }

    const uint8_t type = payload[0];

    if (type == 0x01 && payload.size() >= 4) {
        const uint32_t value = static_cast<uint32_t>(payload[1] << 16 | payload[2] << 8 | payload[3]);
        object.expiry_present = true;
        object.expiry = JOURNALINE_EPOCH + static_cast<int64_t>(value) * 15 * 60;
    } else if (type == 0x02 && payload.size() >= 3 && !object.expiry_present && current_time != 0) {
        const uint16_t minutes = static_cast<uint16_t>(payload[1] << 8 | payload[2]);
        object.expiry_present = true;
        object.expiry = current_time + static_cast<int64_t>(minutes) * 60;
    } else if (type == 0x03 && payload.size() >= 2) {
        GeneralLink link;
        link.type = payload[1];

        if (link.type == 0x00 && payload.size() >= 4) {
            link.address = std::to_string(static_cast<uint16_t>(payload[2] << 8 | payload[3]));
            link.label = bytes_to_string(payload, 4, payload.size());
        } else if (link.type >= 0x01 && link.type <= 0x04) {
            size_t end = 2;

            while (end < payload.size() && payload[end] != 0) {
                end++;
            }

            link.address = bytes_to_string(payload, 2, end);

            if (end < payload.size()) {
                link.label = bytes_to_string(payload, end + 1, payload.size());
            }
        }

        if (!link.address.empty()) {
            object.general_links.push_back(std::move(link));
        }
    } else if (type == 0xA0 && payload.size() >= 4) {
        object.language = bytes_to_string(payload, 1, 4);
    }
}

bool JournalineDecoder::decompress_deflate(const uint8_t *data, const size_t length, std::vector<uint8_t> &result) {
    z_stream stream{};
    stream.next_in = const_cast<Bytef *>(reinterpret_cast<const Bytef *>(data));
    stream.avail_in = static_cast<uInt>(length);

    if (inflateInit2(&stream, -12) != Z_OK) {
        return false;
    }

    result.clear();
    std::vector<uint8_t> buffer(4096);
    int status = Z_OK;

    while (status == Z_OK) {
        stream.next_out = buffer.data();
        stream.avail_out = static_cast<uInt>(buffer.size());
        status = inflate(&stream, Z_NO_FLUSH);
        const size_t produced = buffer.size() - stream.avail_out;

        if (result.size() + produced > MAX_OBJECT_SIZE) {
            inflateEnd(&stream);
            result.clear();
            return false;
        }

        result.insert(result.end(), buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(produced));
    }

    inflateEnd(&stream);
    return status == Z_STREAM_END;
}

uint16_t JournalineDecoder::calculate_crc(const uint8_t *data, const size_t length) {
    uint16_t crc = 0xFFFF;

    for (size_t index = 0; index < length; index++) {
        crc ^= static_cast<uint16_t>(data[index]) << 8;

        for (int bit = 0; bit < 8; bit++) {
            crc = (crc & 0x8000) != 0 ? static_cast<uint16_t>(crc << 1 ^ 0x1021) : static_cast<uint16_t>(crc << 1);
        }
    }

    return static_cast<uint16_t>(~crc);
}

void JournalineDecoder::expire() {
    if (current_time == 0) {
        return;
    }

    if (service_expiry_present && current_time >= service_expiry) {
        objects.clear();
        toc_fragments.clear();
        service_expiry_present = false;
        service_expiry = 0;
        return;
    }

    for (auto iterator = objects.begin(); iterator != objects.end();) {
        if (iterator->second.expiry_present && current_time >= iterator->second.expiry) {
            iterator = objects.erase(iterator);
        } else {
            ++iterator;
        }
    }
}

void JournalineDecoder::emit_update() const {
    if (!update_callback) {
        return;
    }

    std::vector<Object> result;
    result.reserve(objects.size());

    for (const auto &object: objects | std::views::values) {
        result.push_back(object);
    }

    update_callback(status, result);
}
