#include "spi_decoder.h"
#include "../helper/spi_group_helper.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <format>
#include <ranges>

namespace {
class BitReader {
public:
    explicit BitReader(const std::vector<uint8_t> &data) : data(data) {
    }

    bool read(const size_t count, uint32_t &value) {
        if (count > 32 || offset + count > data.size() * 8) {
            return false;
        }

        value = 0;

        for (size_t index = 0; index < count; index++) {
            value = value << 1 | (data[(offset + index) / 8] >> (7 - (offset + index) % 8) & 1);
        }

        offset += count;
        return true;
    }

private:
    const std::vector<uint8_t> &data;
    size_t offset = 0;
};

void merge_string(std::string &target, const std::string &value) {
    if (!value.empty()) {
        target = value;
    }
}

template<typename Value>
void merge_vector(std::vector<Value> &target, const std::vector<Value> &values) {
    for (const auto &value: values) {
        if (std::find(target.begin(), target.end(), value) == target.end()) {
            target.push_back(value);
        }
    }
}

void merge_service_information(SPIDecoder::ServiceInformation &target, const SPIDecoder::ServiceInformation &source) {
    target.service_id = source.service_id;
    target.data_service = source.data_service;
    target.component_id = source.component_id;
    target.ecc = source.ecc;
    target.ensemble_id = source.ensemble_id;
    merge_string(target.short_name, source.short_name);
    merge_string(target.medium_name, source.medium_name);
    merge_string(target.long_name, source.long_name);
    merge_string(target.short_description, source.short_description);
    merge_string(target.long_description, source.long_description);
    merge_string(target.language, source.language);
    merge_vector(target.genres, source.genres);
    merge_vector(target.multimedia, source.multimedia);
}

void merge_programme(SPIDecoder::Programme &target, const SPIDecoder::Programme &source) {
    target.service_id = source.service_id;
    target.data_service = source.data_service;
    target.component_id = source.component_id;
    merge_string(target.id, source.id);

    if (source.short_id != 0) {
        target.short_id = source.short_id;
    }

    merge_string(target.short_name, source.short_name);
    merge_string(target.medium_name, source.medium_name);
    merge_string(target.long_name, source.long_name);
    merge_string(target.short_description, source.short_description);
    merge_string(target.long_description, source.long_description);
    merge_string(target.language, source.language);
    merge_string(target.start_time, source.start_time);

    if (source.duration != 0) {
        target.duration = source.duration;
    }

    merge_string(target.actual_time, source.actual_time);

    if (source.actual_duration != 0) {
        target.actual_duration = source.actual_duration;
    }

    merge_vector(target.genres, source.genres);
    merge_vector(target.multimedia, source.multimedia);
    merge_vector(target.memberships, source.memberships);
}

void merge_programme_group(SPIDecoder::ProgrammeGroup &target, const SPIDecoder::ProgrammeGroup &source) {
    merge_string(target.id, source.id);

    if (source.short_id <= 0xFFFFFF) {
        target.short_id = source.short_id;
    }

    merge_string(target.short_name, source.short_name);
    merge_string(target.medium_name, source.medium_name);
    merge_string(target.long_name, source.long_name);
    merge_string(target.short_description, source.short_description);
    merge_string(target.long_description, source.long_description);
    merge_vector(target.genres, source.genres);
    merge_vector(target.multimedia, source.multimedia);
    merge_vector(target.memberships, source.memberships);
}

}

SPIDecoder::SPIDecoder(const UpdateCallback &update_callback) {
    this->update_callback = update_callback;
}

void SPIDecoder::reset() {
    objects.clear();
    services.clear();
    programmes.clear();
    programme_groups.clear();
    publish();
}

SPIDecoder::ProcessResult SPIDecoder::process(const uint16_t transport_id, const std::string &content_name, const uint16_t content_subtype, const std::vector<uint8_t> &data, const std::vector<uint8_t> &scope_id, const std::vector<uint8_t> &scope_end) {
    ProcessResult result;
    Node root;
    std::map<uint8_t, std::string> tokens;
    std::string default_language;

    if (!parse_object(data, root, tokens, default_language)) {
        result.error = "Malformed binary SPI object";
        return result;
    }

    result.root_tag = root.tag;
    ObjectState object;
    object.content_subtype = content_subtype;
    object.content_name = content_name;
    object.expiry = decode_time_timestamp(scope_end);

    if (content_subtype == 0) {
        if (root.tag != 0x03) {
            result.error = std::format("SI object has unexpected root 0x{:02X}", root.tag);
            return result;
        }

        process_service_information(root, tokens, default_language, object);
    } else if (content_subtype == 1) {
        if (root.tag != 0x02) {
            result.error = std::format("PI object has unexpected root 0x{:02X}", root.tag);
            return result;
        }

        process_programme_information(root, tokens, default_language, scope_id, object);
    } else if (content_subtype == 2) {
        if (root.tag != 0x02) {
            result.error = std::format("GI object has unexpected root 0x{:02X}", root.tag);
            return result;
        }

        process_group_information(root, tokens, object);
    } else {
        result.error = std::format("Unsupported SPI content subtype {}", content_subtype);
        return result;
    }

    result.accepted = true;
    result.services = static_cast<uint32_t>(object.services.size());
    result.programmes = static_cast<uint32_t>(object.programmes.size());
    result.programme_groups = static_cast<uint32_t>(object.programme_groups.size());

    if (!content_name.empty()) {
        for (auto iterator = objects.begin(); iterator != objects.end();) {
            if (iterator->first != transport_id && iterator->second.content_subtype == content_subtype && iterator->second.content_name == content_name) {
                iterator = objects.erase(iterator);
            } else {
                ++iterator;
            }
        }
    }

    objects[transport_id] = std::move(object);
    rebuild();
    publish();
    return result;
}

void SPIDecoder::remove(const uint16_t transport_id) {
    if (objects.erase(transport_id) == 0) {
        return;
    }

    rebuild();
    publish();
}

void SPIDecoder::expire(const int64_t timestamp) {
    bool changed = false;

    for (auto object_iterator = objects.begin(); object_iterator != objects.end();) {
        ObjectState &object = object_iterator->second;

        if (object.expiry != 0 && object.expiry <= timestamp) {
            object_iterator = objects.erase(object_iterator);
            changed = true;
            continue;
        }

        for (auto expiry_iterator = object.programme_expiry.begin(); expiry_iterator != object.programme_expiry.end();) {
            if (expiry_iterator->second != 0 && expiry_iterator->second <= timestamp) {
                object.programmes.erase(expiry_iterator->first);
                expiry_iterator = object.programme_expiry.erase(expiry_iterator);
                changed = true;
            } else {
                ++expiry_iterator;
            }
        }

        ++object_iterator;
    }

    if (!changed) {
        return;
    }

    rebuild();
    publish();
}

bool SPIDecoder::parse_object(const std::vector<uint8_t> &data, Node &root, std::map<uint8_t, std::string> &tokens, std::string &default_language) const {
    size_t offset = 0;
    uint8_t tag = 0;
    const uint8_t *value = nullptr;
    size_t value_length = 0;

    if (!read_item(data.data(), data.size(), offset, tag, value, value_length) || offset != data.size() || (tag != 0x02 && tag != 0x03)) {
        return false;
    }

    if (!parse_node(tag, value, value_length, true, root)) {
        return false;
    }

    tokens = parse_tokens(root.token_table);
    default_language = decode_string(root.default_language, tokens);
    return true;
}

bool SPIDecoder::parse_node(const uint8_t tag, const uint8_t *data, const size_t length, const bool top_level, Node &node) const {
    node = {};
    node.tag = tag;
    size_t offset = 0;

    while (offset < length) {
        uint8_t child_tag = 0;
        const uint8_t *value = nullptr;
        size_t value_length = 0;

        if (!read_item(data, length, offset, child_tag, value, value_length)) {
            return false;
        }

        if (child_tag == 0x01 || child_tag >= 0x80) {
            node.attributes[child_tag] = std::vector<uint8_t>(value, value + value_length);
        } else if (top_level && child_tag == 0x04) {
            node.token_table.assign(value, value + value_length);
        } else if (top_level && child_tag == 0x06) {
            node.default_language.assign(value, value + value_length);
        } else if (is_element_tag(child_tag)) {
            Node child;

            if (!parse_node(child_tag, value, value_length, false, child)) {
                return false;
            }

            node.children.push_back(std::move(child));
        }
    }

    return true;
}

bool SPIDecoder::read_item(const uint8_t *data, const size_t length, size_t &offset, uint8_t &tag, const uint8_t *&value, size_t &value_length) {
    if (offset + 2 > length) {
        return false;
    }

    tag = data[offset++];
    const uint8_t first_length = data[offset++];

    if (first_length <= 0xFD) {
        value_length = first_length;
    } else if (first_length == 0xFE) {
        if (offset + 2 > length) {
            return false;
        }

        value_length = static_cast<size_t>(data[offset] << 8 | data[offset + 1]);
        offset += 2;
    } else {
        if (offset + 3 > length) {
            return false;
        }

        value_length = static_cast<size_t>(data[offset]) << 16 | static_cast<size_t>(data[offset + 1]) << 8 | data[offset + 2];
        offset += 3;
    }

    if (value_length > length - offset) {
        return false;
    }

    value = data + offset;
    offset += value_length;
    return true;
}

bool SPIDecoder::is_element_tag(const uint8_t tag) {
    switch (tag) {
        case 0x10:
        case 0x11:
        case 0x12:
        case 0x13:
        case 0x14:
        case 0x16:
        case 0x17:
        case 0x18:
        case 0x19:
        case 0x1A:
        case 0x1B:
        case 0x1C:
        case 0x20:
        case 0x21:
        case 0x23:
        case 0x24:
        case 0x25:
        case 0x26:
        case 0x28:
        case 0x29:
        case 0x2A:
        case 0x2B:
        case 0x2C:
        case 0x2D:
        case 0x2E:
        case 0x2F:
        case 0x31:
        case 0x32:
        case 0x33:
        case 0x34:
        case 0x35:
        case 0x36:
        case 0x37:
        case 0x38:
        case 0x39:
        case 0x3A:
            return true;
        default:
            return false;
    }
}

std::map<uint8_t, std::string> SPIDecoder::parse_tokens(const std::vector<uint8_t> &data) {
    std::map<uint8_t, std::string> result;
    size_t offset = 0;

    while (offset + 2 <= data.size() && result.size() < 16) {
        const uint8_t token = data[offset++];
        const size_t length = data[offset++];

        if (length > data.size() - offset) {
            return {};
        }

        result[token] = std::string(data.begin() + static_cast<std::ptrdiff_t>(offset), data.begin() + static_cast<std::ptrdiff_t>(offset + length));
        offset += length;
    }

    if (offset != data.size()) {
        return {};
    }

    return result;
}

std::string SPIDecoder::decode_string(const std::vector<uint8_t> &data, const std::map<uint8_t, std::string> &tokens) {
    std::string result;

    for (const uint8_t value: data) {
        const auto token = tokens.find(value);

        if (token != tokens.end()) {
            result += token->second;
        } else {
            result.push_back(static_cast<char>(value));
        }
    }

    return result;
}

std::string SPIDecoder::get_string_attribute(const Node &node, const uint8_t tag, const std::map<uint8_t, std::string> &tokens) {
    const auto attribute = node.attributes.find(tag);
    return attribute == node.attributes.end() ? std::string{} : decode_string(attribute->second, tokens);
}

std::string SPIDecoder::get_text(const Node &node, const std::map<uint8_t, std::string> &tokens) {
    return get_string_attribute(node, 0x01, tokens);
}

const SPIDecoder::Node *SPIDecoder::find_child(const Node &node, const uint8_t tag) {
    for (const auto &child: node.children) {
        if (child.tag == tag) {
            return &child;
        }
    }

    return nullptr;
}

std::vector<const SPIDecoder::Node *> SPIDecoder::find_children(const Node &node, const uint8_t tag) {
    std::vector<const Node *> result;

    for (const auto &child: node.children) {
        if (child.tag == tag) {
            result.push_back(&child);
        }
    }

    return result;
}

void SPIDecoder::collect_names(const Node &node, const std::map<uint8_t, std::string> &tokens, std::string &short_name, std::string &medium_name, std::string &long_name) {
    for (const auto &child: node.children) {
        if (child.tag == 0x10) {
            merge_string(short_name, get_text(child, tokens));
        } else if (child.tag == 0x11) {
            merge_string(medium_name, get_text(child, tokens));
        } else if (child.tag == 0x12) {
            merge_string(long_name, get_text(child, tokens));
        }
    }
}

void SPIDecoder::collect_media(const Node &node, const std::map<uint8_t, std::string> &tokens, std::string &short_description, std::string &long_description, std::vector<std::string> &genres, std::vector<Multimedia> &multimedia) {
    for (const auto &child: node.children) {
        if (child.tag == 0x13) {
            collect_media(child, tokens, short_description, long_description, genres, multimedia);
        } else if (child.tag == 0x1A) {
            merge_string(short_description, get_text(child, tokens));
        } else if (child.tag == 0x1B) {
            merge_string(long_description, get_text(child, tokens));
        } else if (child.tag == 0x14) {
            const std::string genre = get_string_attribute(child, 0x80, tokens);

            if (!genre.empty() && std::find(genres.begin(), genres.end(), genre) == genres.end()) {
                genres.push_back(genre);
            }
        } else if (child.tag == 0x2B) {
            Multimedia value;
            value.mime_type = get_string_attribute(child, 0x80, tokens);
            value.url = get_string_attribute(child, 0x82, tokens);
            const auto type = child.attributes.find(0x83);
            const auto width = child.attributes.find(0x84);
            const auto height = child.attributes.find(0x85);

            if (type != child.attributes.end() && type->second.size() == 1) {
                value.type = multimedia_type(type->second[0]);
            }

            if (width != child.attributes.end()) {
                value.width = static_cast<uint16_t>(decode_integer(width->second));
            }

            if (height != child.attributes.end()) {
                value.height = static_cast<uint16_t>(decode_integer(height->second));
            }

            if (!value.url.empty() && std::find(multimedia.begin(), multimedia.end(), value) == multimedia.end()) {
                multimedia.push_back(std::move(value));
            }
        }
    }
}

std::vector<SPIDecoder::GroupMembership> SPIDecoder::collect_memberships(const Node &node, const std::map<uint8_t, std::string> &tokens) {
    std::vector<GroupMembership> result;

    for (const auto &member: find_children(node, 0x17)) {
        GroupMembership membership;
        membership.id = get_string_attribute(*member, 0x80, tokens);
        const auto short_id = member->attributes.find(0x81);
        const auto index = member->attributes.find(0x82);

        if (short_id != member->attributes.end() && short_id->second.size() == 3) {
            membership.short_id = decode_integer(short_id->second);
        }

        if (index != member->attributes.end() && index->second.size() == 2) {
            membership.index = static_cast<uint16_t>(decode_integer(index->second));
        }

        if (!membership.id.empty() || membership.short_id <= 0xFFFFFF) {
            merge_vector(result, std::vector<GroupMembership>{membership});
        }
    }

    return result;
}

SPIDecoder::Bearer SPIDecoder::decode_bearer(const std::vector<uint8_t> &data) {
    Bearer result;
    BitReader reader(data);
    uint32_t value = 0;

    if (!reader.read(1, value) || value != 0) {
        return result;
    }

    if (!reader.read(1, value)) {
        return result;
    }

    const bool ensemble_present = value != 0;

    if (!reader.read(1, value)) {
        return result;
    }

    const bool xpad_present = value != 0;

    if (!reader.read(1, value)) {
        return result;
    }

    result.data_service = value != 0;

    if (!reader.read(4, value)) {
        return result;
    }

    result.component_id = static_cast<uint8_t>(value);

    if (ensemble_present) {
        if (!reader.read(8, value)) {
            return {};
        }

        result.ecc = static_cast<uint8_t>(value);

        if (!reader.read(16, value)) {
            return {};
        }

        result.ensemble_id = static_cast<uint16_t>(value);
    }

    if (!reader.read(result.data_service ? 32 : 16, value)) {
        return {};
    }

    result.service_id = value;

    if (xpad_present && !reader.read(8, value)) {
        return {};
    }

    result.valid = result.service_id != 0;
    return result;
}

std::string SPIDecoder::decode_time(const std::vector<uint8_t> &data) {
    BitReader reader(data);
    uint32_t value = 0;

    if (!reader.read(1, value) || !reader.read(17, value)) {
        return {};
    }

    const uint32_t mjd = value;

    if (!reader.read(1, value) || !reader.read(1, value)) {
        return {};
    }

    const bool lto_present = value != 0;

    if (!reader.read(1, value)) {
        return {};
    }

    const bool long_form = value != 0;
    uint32_t hour = 0;
    uint32_t minute = 0;
    uint32_t second = 0;

    if (!reader.read(5, hour) || !reader.read(6, minute)) {
        return {};
    }

    if (long_form) {
        if (!reader.read(6, second) || !reader.read(10, value)) {
            return {};
        }
    }

    int offset_minutes = 0;

    if (lto_present) {
        uint32_t reserved = 0;
        uint32_t sign = 0;
        uint32_t half_hours = 0;

        if (!reader.read(2, reserved) || !reader.read(1, sign) || !reader.read(5, half_hours)) {
            return {};
        }

        offset_minutes = static_cast<int>(half_hours) * 30 * (sign != 0 ? -1 : 1);
    }

    if (hour > 23 || minute > 59 || second > 59) {
        return {};
    }

    using namespace std::chrono;
    const sys_seconds utc = sys_days{1970y / January / 1} + days{static_cast<int64_t>(mjd) - 40587} + hours{hour} + minutes{minute} + seconds{second};
    const sys_seconds local = utc + minutes{offset_minutes};
    const sys_days local_day = floor<days>(local);
    const year_month_day date{local_day};
    const hh_mm_ss time{local - local_day};
    const char sign = offset_minutes < 0 ? '-' : '+';
    const int absolute_offset = std::abs(offset_minutes);

    return std::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}{}{:02}:{:02}", static_cast<int>(date.year()), static_cast<unsigned>(date.month()), static_cast<unsigned>(date.day()), time.hours().count(), time.minutes().count(), time.seconds().count(), sign, absolute_offset / 60, absolute_offset % 60);
}

int64_t SPIDecoder::decode_time_timestamp(const std::vector<uint8_t> &data) {
    BitReader reader(data);
    uint32_t value = 0;

    if (!reader.read(1, value) || !reader.read(17, value)) {
        return 0;
    }

    const uint32_t mjd = value;

    if (!reader.read(1, value) || !reader.read(1, value)) {
        return 0;
    }

    const bool lto_present = value != 0;

    if (!reader.read(1, value)) {
        return 0;
    }

    const bool long_form = value != 0;
    uint32_t hour = 0;
    uint32_t minute = 0;
    uint32_t second = 0;

    if (!reader.read(5, hour) || !reader.read(6, minute)) {
        return 0;
    }

    if (long_form && (!reader.read(6, second) || !reader.read(10, value))) {
        return 0;
    }

    if (lto_present) {
        uint32_t reserved = 0;
        uint32_t sign = 0;
        uint32_t half_hours = 0;

        if (!reader.read(2, reserved) || !reader.read(1, sign) || !reader.read(5, half_hours)) {
            return 0;
        }
    }

    if (hour > 23 || minute > 59 || second > 59) {
        return 0;
    }

    using namespace std::chrono;
    const sys_seconds utc = sys_days{1970y / January / 1} + days{static_cast<int64_t>(mjd) - 40587} + hours{hour} + minutes{minute} + seconds{second};
    return utc.time_since_epoch().count();
}

uint32_t SPIDecoder::decode_integer(const std::vector<uint8_t> &data) {
    uint32_t result = 0;

    for (const uint8_t value: data) {
        result = result << 8 | value;
    }

    return result;
}

std::string SPIDecoder::multimedia_type(const uint8_t value) {
    switch (value) {
        case 0x02:
            return "logo_unrestricted";
        case 0x04:
            return "logo_colour_square";
        case 0x06:
            return "logo_colour_rectangle";
        default:
            return std::format("0x{:02X}", value);
    }
}

uint64_t SPIDecoder::service_key(const Bearer &bearer) {
    return static_cast<uint64_t>(bearer.data_service) << 60 | static_cast<uint64_t>(bearer.ecc) << 52 | static_cast<uint64_t>(bearer.ensemble_id) << 36 | static_cast<uint64_t>(bearer.service_id) << 4 | bearer.component_id;
}

void SPIDecoder::process_service_information(const Node &root, const std::map<uint8_t, std::string> &tokens, const std::string &default_language, ObjectState &object) {
    std::vector<const Node *> service_nodes;

    for (const auto &ensemble: find_children(root, 0x26)) {
        const auto children = find_children(*ensemble, 0x28);
        service_nodes.insert(service_nodes.end(), children.begin(), children.end());
    }

    const auto direct_services = find_children(root, 0x28);
    service_nodes.insert(service_nodes.end(), direct_services.begin(), direct_services.end());

    for (const Node *service_node: service_nodes) {
        Bearer bearer;

        for (const auto &bearer_node: find_children(*service_node, 0x29)) {
            const auto id = bearer_node->attributes.find(0x80);

            if (id != bearer_node->attributes.end()) {
                bearer = decode_bearer(id->second);

                if (bearer.valid) {
                    break;
                }
            }
        }

        if (!bearer.valid) {
            continue;
        }

        ServiceInformation incoming;
        incoming.service_id = bearer.service_id;
        incoming.data_service = bearer.data_service;
        incoming.component_id = bearer.component_id;
        incoming.ecc = bearer.ecc;
        incoming.ensemble_id = bearer.ensemble_id;
        collect_names(*service_node, tokens, incoming.short_name, incoming.medium_name, incoming.long_name);
        collect_media(*service_node, tokens, incoming.short_description, incoming.long_description, incoming.genres, incoming.multimedia);
        incoming.language = default_language;
        merge_service_information(object.services[service_key(bearer)], incoming);
    }
}

void SPIDecoder::process_programme_information(const Node &root, const std::map<uint8_t, std::string> &tokens, const std::string &default_language, const std::vector<uint8_t> &scope_id, ObjectState &object) {
    for (const auto &schedule: find_children(root, 0x21)) {
        Bearer schedule_bearer;

        if (!scope_id.empty()) {
            const Bearer candidate = decode_bearer(scope_id);

            if (candidate.valid) {
                schedule_bearer = candidate;
            }
        }

        const Node *scope = find_child(*schedule, 0x24);

        if (scope) {
            const Node *service_scope = find_child(*scope, 0x25);

            if (service_scope) {
                const auto id = service_scope->attributes.find(0x80);

                if (id != service_scope->attributes.end()) {
                    const Bearer candidate = decode_bearer(id->second);

                    if (candidate.valid) {
                        schedule_bearer = candidate;
                    }
                }
            }
        }

        for (const auto &programme_node: find_children(*schedule, 0x1C)) {
            Programme programme;
            int64_t start_timestamp = 0;
            int64_t actual_timestamp = 0;
            programme.id = get_string_attribute(*programme_node, 0x80, tokens);
            const auto short_id = programme_node->attributes.find(0x81);

            if (short_id != programme_node->attributes.end()) {
                programme.short_id = decode_integer(short_id->second);
            }

            programme.language = get_string_attribute(*programme_node, 0x86, tokens);

            if (programme.language.empty()) {
                programme.language = default_language;
            }

            collect_names(*programme_node, tokens, programme.short_name, programme.medium_name, programme.long_name);
            collect_media(*programme_node, tokens, programme.short_description, programme.long_description, programme.genres, programme.multimedia);
            programme.memberships = collect_memberships(*programme_node, tokens);
            Bearer programme_bearer = schedule_bearer;

            for (const auto &location: find_children(*programme_node, 0x19)) {
                for (const auto &bearer_node: find_children(*location, 0x2D)) {
                    const auto id = bearer_node->attributes.find(0x80);

                    if (id != bearer_node->attributes.end()) {
                        const Bearer candidate = decode_bearer(id->second);

                        if (candidate.valid) {
                            programme_bearer = candidate;
                            break;
                        }
                    }
                }

                const Node *time = find_child(*location, 0x2C);

                if (time && programme.start_time.empty()) {
                    const auto start = time->attributes.find(0x80);
                    const auto duration = time->attributes.find(0x81);
                    const auto actual_time = time->attributes.find(0x82);
                    const auto actual_duration = time->attributes.find(0x83);

                    if (start != time->attributes.end()) {
                        programme.start_time = decode_time(start->second);
                        start_timestamp = decode_time_timestamp(start->second);
                    }

                    if (duration != time->attributes.end()) {
                        programme.duration = decode_integer(duration->second);
                    }

                    if (actual_time != time->attributes.end()) {
                        programme.actual_time = decode_time(actual_time->second);
                        actual_timestamp = decode_time_timestamp(actual_time->second);
                    }

                    if (actual_duration != time->attributes.end()) {
                        programme.actual_duration = decode_integer(actual_duration->second);
                    }
                }
            }

            if (!programme_bearer.valid) {
                continue;
            }

            programme.service_id = programme_bearer.service_id;
            programme.data_service = programme_bearer.data_service;
            programme.component_id = programme_bearer.component_id;
            const std::string key = std::format("{}:{}:{}:{}:{}", programme.data_service, programme.service_id, programme.component_id, programme.id, programme.short_id != 0 ? std::to_string(programme.short_id) : programme.start_time);
            merge_programme(object.programmes[key], programme);

            if (actual_timestamp != 0 && programme.actual_duration != 0) {
                object.programme_expiry[key] = actual_timestamp + programme.actual_duration;
            } else if (start_timestamp != 0 && programme.duration != 0) {
                object.programme_expiry[key] = start_timestamp + programme.duration;
            }
        }
    }
}

void SPIDecoder::process_group_information(const Node &root, const std::map<uint8_t, std::string> &tokens, ObjectState &object) {
    for (const auto &groups: find_children(root, 0x20)) {
        for (const auto &group_node: find_children(*groups, 0x23)) {
            ProgrammeGroup incoming;
            incoming.id = get_string_attribute(*group_node, 0x80, tokens);
            const auto short_id = group_node->attributes.find(0x81);

            if (short_id != group_node->attributes.end() && short_id->second.size() == 3) {
                incoming.short_id = decode_integer(short_id->second);
            }

            collect_names(*group_node, tokens, incoming.short_name, incoming.medium_name, incoming.long_name);
            collect_media(*group_node, tokens, incoming.short_description, incoming.long_description, incoming.genres, incoming.multimedia);
            incoming.memberships = collect_memberships(*group_node, tokens);
            const std::string key = !incoming.id.empty() ? incoming.id : std::to_string(incoming.short_id);

            if (incoming.id.empty() && incoming.short_id > 0xFFFFFF) {
                continue;
            }

            merge_programme_group(object.programme_groups[key], incoming);
        }
    }
}

void SPIDecoder::rebuild() {
    services.clear();
    programmes.clear();
    programme_groups.clear();

    const auto merge_group = [this](const ProgrammeGroup &group) {
        const auto existing = std::ranges::find_if(programme_groups, [&](const auto &entry) {
            return SPIGroupHelper::matches(group.id, group.short_id, entry.second.id, entry.second.short_id);
        });

        if (existing != programme_groups.end()) {
            merge_programme_group(existing->second, group);
        } else {
            const std::string key = group.id.empty() ? std::format("short:{}", group.short_id) : group.id;
            merge_programme_group(programme_groups[key], group);
        }
    };

    for (const auto &object: objects | std::views::values) {
        for (const auto &[key, service]: object.services) {
            merge_service_information(services[key], service);
        }

        for (const auto &[key, programme]: object.programmes) {
            merge_programme(programmes[key], programme);
        }

        for (const auto &[key, group]: object.programme_groups) {
            merge_group(group);
        }
    }

    std::vector<GroupMembership> memberships;

    for (const auto &programme: programmes | std::views::values) {
        merge_vector(memberships, programme.memberships);
    }

    for (const auto &group: programme_groups | std::views::values) {
        merge_vector(memberships, group.memberships);
    }

    for (const auto &membership: memberships) {
        ProgrammeGroup group;
        group.id = membership.id;
        group.short_id = membership.short_id;
        merge_group(group);
    }
}

void SPIDecoder::publish() const {
    if (!update_callback) {
        return;
    }

    std::vector<ServiceInformation> service_values;
    std::vector<Programme> programme_values;
    std::vector<ProgrammeGroup> group_values;
    service_values.reserve(services.size());
    programme_values.reserve(programmes.size());
    group_values.reserve(programme_groups.size());

    for (const auto &[key, service]: services) {
        service_values.push_back(service);
    }

    for (const auto &[key, programme]: programmes) {
        programme_values.push_back(programme);
    }

    for (const auto &[key, group]: programme_groups) {
        group_values.push_back(group);
    }

    std::ranges::sort(programme_values, [](const Programme &left, const Programme &right) {
        if (left.start_time == right.start_time) {
            return left.service_id < right.service_id;
        }

        return left.start_time < right.start_time;
    });

    update_callback(service_values, programme_values, group_values);
}
