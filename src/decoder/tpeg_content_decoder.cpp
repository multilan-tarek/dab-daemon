#include "tpeg_content_decoder.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <format>
#include <iterator>
#include <limits>
#include <span>
#include <string_view>
#include <utility>

// SNI, MMC, TEC and TMC field layouts follow the TISA Evaluation Kit.
// Copyright 2022 TISA ASBL. See docs/licenses/tisa-evaluation-kit.txt.
namespace {
constexpr size_t MAX_SERVICES = 64;
constexpr size_t MAX_MESSAGES = 2048;
constexpr size_t MAX_CACHE_SIZE = 8 * 1024 * 1024;
constexpr size_t MAX_FIELDS = 512;
constexpr size_t MAX_NODES = 512;
constexpr size_t MAX_DEPTH = 12;

struct ParseError {};
struct UnsupportedData {};

class Reader {
public:
    std::span<const uint8_t> data;

    explicit Reader(const std::span<const uint8_t> data) : data(data) {}

    uint8_t byte() {
        if (data.empty()) {
            throw ParseError{};
        }
        const uint8_t value = data.front();
        data = data.subspan(1);
        return value;
    }

    uint32_t number(const size_t size) {
        uint32_t value = 0;
        for (size_t index = 0; index < size; index++) {
            value = (value << 8) | byte();
        }
        return value;
    }

    uint32_t multibyte() {
        uint32_t value = 0;
        for (size_t index = 0; index < 5; index++) {
            const uint8_t next = byte();
            if (value > (std::numeric_limits<uint32_t>::max() >> 7)) {
                throw ParseError{};
            }
            value = (value << 7) | (next & 0x7F);
            if (!(next & 0x80)) {
                return value;
            }
        }
        throw ParseError{};
    }

    uint64_t selector() {
        uint64_t value = 0;
        for (size_t index = 0; index < 5; index++) {
            const uint8_t next = byte();
            for (size_t bit = 0; bit < 7; bit++) {
                if (next & (0x40 >> bit)) {
                    value |= uint64_t{1} << (index * 7 + bit);
                }
            }
            if (!(next & 0x80)) {
                return value;
            }
        }
        throw ParseError{};
    }

    Reader take(const size_t size) {
        if (size > data.size()) {
            throw ParseError{};
        }
        Reader result(data.first(size));
        data = data.subspan(size);
        return result;
    }

    std::string string() {
        const auto text = take(byte()).data;
        std::string result;
        // Broadcast strings must not pass invalid UTF-8 or embedded NULs to D-Bus.
        for (size_t offset = 0; offset < text.size();) {
            const uint8_t first = text[offset];
            size_t count = first < 0x80 ? 1 : first >= 0xC2 && first <= 0xDF ? 2 : first >= 0xE0 && first <= 0xEF ? 3 : first >= 0xF0 && first <= 0xF4 ? 4 : 0;
            bool valid = count != 0 && count <= text.size() - offset && first != 0;
            for (size_t index = 1; valid && index < count; index++) {
                valid = (text[offset + index] & 0xC0) == 0x80;
            }
            if (valid && count >= 3) {
                valid = !(first == 0xE0 && text[offset + 1] < 0xA0) && !(first == 0xED && text[offset + 1] >= 0xA0) && !(first == 0xF0 && text[offset + 1] < 0x90) && !(first == 0xF4 && text[offset + 1] >= 0x90);
            }
            if (valid) {
                result.append(reinterpret_cast<const char *>(text.data() + offset), count);
                offset += count;
            } else {
                result += "\xEF\xBF\xBD";
                offset++;
            }
        }
        return result;
    }
};

struct Node {
    uint8_t id;
    std::span<const uint8_t> attributes;
    std::span<const uint8_t> data;
    std::vector<Node> children;
};

std::vector<Node> read_nodes(Reader reader, size_t &count, const size_t depth = 0) {
    if (depth >= MAX_DEPTH) {
        throw ParseError{};
    }
    std::vector<Node> result;
    while (!reader.data.empty()) {
        if (++count > MAX_NODES) {
            throw ParseError{};
        }
        const auto start = reader.data;
        const uint8_t id = reader.byte();
        auto body = reader.take(reader.multibyte());
        const auto attributes = body.take(body.multibyte()).data;
        result.push_back({id, attributes, start.first(start.size() - reader.data.size()), read_nodes(body, count, depth + 1)});
    }
    return result;
}

uint16_t crc(const std::span<const uint8_t> data) {
    uint16_t value = 0xFFFF;
    for (const uint8_t byte: data) {
        value ^= static_cast<uint16_t>(byte) << 8;
        for (size_t bit = 0; bit < 8; bit++) {
            value = value & 0x8000 ? static_cast<uint16_t>((value << 1) ^ 0x1021) : static_cast<uint16_t>(value << 1);
        }
    }
    return value ^ 0xFFFF;
}

std::string hex(const std::span<const uint8_t> data) {
    std::string result;
    for (const uint8_t byte: data) {
        result += std::format("{:02X}", byte);
    }
    return result;
}

void field(TPEGContentDecoder::Message &message, const std::string &component, const std::string &name, const std::string &value) {
    if (message.fields.size() >= MAX_FIELDS) {
        throw ParseError{};
    }
    message.fields.push_back({component, name, value});
}

void field(TPEGContentDecoder::Message &message, const std::string &component, const std::string &name, const uint64_t value) {
    field(message, component, name, std::to_string(value));
}

std::string code(const uint8_t value, const std::span<const std::string_view> names) {
    return value < names.size() && !names[value].empty() ? std::format("{} ({})", names[value], value) : std::to_string(value);
}

constexpr std::array<std::string_view, 8> EFFECTS = {"", "Traffic flow unknown", "Free traffic flow", "Heavy traffic", "Slow traffic", "Queuing traffic", "Stationary traffic", "No traffic flow"};
constexpr std::array<std::string_view, 32> CAUSES = {"", "Traffic congestion", "Accident", "Roadworks", "Narrow lanes", "Impassability", "Slippery road", "Aquaplaning", "Fire", "Hazardous driving conditions", "Objects on the road", "Animals on roadway", "People on roadway", "Broken down vehicles", "Vehicle on wrong carriageway", "Rescue and recovery work", "Regulatory measure", "Extreme weather", "Reduced visibility", "Precipitation", "Reckless persons", "Overheight warning", "Changed traffic regulations", "Major event", "Service not operating", "Service not usable", "Slow moving vehicles", "Dangerous end of queue", "Risk of fire", "Time delay", "Police checkpoint", "Malfunctioning roadside equipment"};

void optional_fields(Reader &reader, TPEGContentDecoder::Message &message, const std::string &path, const uint64_t flags, const std::span<const std::pair<std::string_view, uint8_t>> definitions) {
    for (size_t bit = 0; bit < definitions.size(); bit++) {
        if (flags & (uint64_t{1} << bit)) {
            const auto &[name, width] = definitions[bit];
            field(message, path, std::string(name), width == 0 ? reader.multibyte() : reader.number(width));
        }
    }
}

void remaining_fields(const Reader &reader, TPEGContentDecoder::Message &message, const std::string &path) {
    if (!reader.data.empty()) {
        field(message, path, "Additional attributes", hex(reader.data));
    }
}

void parse_tec(const Node &node, TPEGContentDecoder::Message &message, const std::string &path, const uint8_t minor_version) {
    if (node.id != 3 && node.id != 4 && node.id != 5 && node.id != 6) {
        field(message, path, "Unhandled component", hex(node.data));
        return;
    }
    Reader reader(node.attributes);
    if (node.id == 3) {
        const auto effect = reader.byte();
        message.summary = effect < EFFECTS.size() ? std::string(EFFECTS[effect]) : "Traffic event";
        field(message, path, "Effect", code(effect, EFFECTS));
        const auto flags = reader.selector();
        constexpr std::array<std::pair<std::string_view, uint8_t>, 8> fields = {{{"Start time (UTC seconds)", 4}, {"Stop time (UTC seconds)", 4}, {"Tendency", 1}, {"Affected length (m)", 0}, {"Average speed (m/s)", 1}, {"Delay (min)", 0}, {"Speed limit (m/s)", 1}, {"Expected speed (m/s)", 1}}};
        optional_fields(reader, message, path, flags, fields);
    } else if (node.id == 4) {
        const auto cause = reader.byte();
        const auto warning = reader.byte();
        field(message, path, "Cause", code(cause, CAUSES));
        field(message, path, "Warning level", warning);
        if (cause < CAUSES.size()) {
            message.summary += " · " + std::string(CAUSES[cause]);
        }
        const auto flags = reader.selector();
        field(message, path, "Unverified", flags & 1 ? "Yes" : "No");
        if (flags & 2) {
            field(message, path, "Subcause", reader.byte());
        }
        if (flags & 4) {
            field(message, path, "Affected length (m)", reader.multibyte());
        }
        if (flags & 8) {
            field(message, path, "Lane restriction", reader.byte());
        }
        if (flags & 16) {
            field(message, path, "Number of lanes", reader.byte());
        }
        if (flags & 32) {
            const auto count = reader.multibyte();
            if (count > MAX_FIELDS) {
                throw ParseError{};
            }
            for (size_t index = 0; index < count; index++) {
                const auto language = reader.byte();
                field(message, path, std::format("Text (language {})", language), reader.string());
            }
        }
        if (flags & 64) {
            field(message, path, "Cause offset (m)", reader.multibyte());
        }
    } else if (node.id == 5) {
        if (minor_version < 2) {
            field(message, path, "Cause", code(reader.byte(), CAUSES));
        }
        field(message, path, "Linked message", reader.multibyte());
        const auto flags = reader.selector();
        if (flags & 1) {
            field(message, path, "Content ID", reader.byte());
        }
        if (flags & 2) {
            field(message, path, "Service ID", hex(reader.take(3).data));
        }
    } else if (node.id == 6) {
        const auto flags = reader.selector();
        if (flags & 1) {
            field(message, path, "Advice code", reader.byte());
        }
        if (flags & 2) {
            field(message, path, "Subadvice code", reader.byte());
        }
        if (flags & 4) {
            const auto count = reader.multibyte();
            if (count > MAX_FIELDS) {
                throw ParseError{};
            }
            for (size_t index = 0; index < count; index++) {
                const auto language = reader.byte();
                field(message, path, std::format("Advice (language {})", language), reader.string());
            }
        }
    }
    remaining_fields(reader, message, path);
    for (const auto &child: node.children) {
        parse_tec(child, message, path + "/" + std::to_string(child.id), minor_version);
    }
}

void parse_tmc(const Node &node, TPEGContentDecoder::Message &message, const std::string &path) {
    Reader reader(node.attributes);
    field(message, path, "Location ID", reader.number(2));
    field(message, path, "Country code", reader.byte());
    field(message, path, "Location table", reader.byte());
    const auto flags = reader.selector();
    field(message, path, "Direction", flags & 2 ? "Both" : flags & 1 ? "Positive" : "Negative");
    if (flags & 4) {
        field(message, path, "Extent", reader.byte());
    }
    if (flags & 8) {
        field(message, path, "ECC", std::format("0x{:02X}", reader.byte()));
    }
    if (flags & 16) {
        const auto version = reader.multibyte();
        field(message, path, "Table version", version > 127 ? std::format("{}.{}", version >> 7, version & 0x7F) : std::format("{}.{}", version >> 3, version & 7));
    }
    if (flags & 32) {
        const auto precise = reader.selector();
        constexpr std::array<std::pair<std::string_view, uint8_t>, 5> fields = {{{"Distance accuracy", 1}, {"Hazard distance 1", 1}, {"Hazard distance 2", 2}, {"Problem length 1", 1}, {"Problem length 2", 2}}};
        optional_fields(reader, message, path, precise, fields);
    }
    remaining_fields(reader, message, path);
}

void raw_location(const Node &node, TPEGContentDecoder::Message &message, const std::string &path) {
    field(message, path, "Attributes", hex(node.attributes));
    for (const auto &child: node.children) {
        raw_location(child, message, path + "/" + std::to_string(child.id));
    }
}

void parse_location(const Node &node, TPEGContentDecoder::Message &message, const std::string &path) {
    for (const auto &child: node.children) {
        if (child.id == 2) {
            parse_tmc(child, message, path + "/TMC");
        } else {
            raw_location(child, message, path + "/" + std::to_string(child.id));
        }
    }
}

void parse_status_parameters(Reader &reader, TPEGContentDecoder::Message &message, const std::string &path) {
    const auto flags = reader.selector();
    if (flags & ~uint64_t{15}) {
        throw UnsupportedData{};
    }
    constexpr std::array<std::pair<std::string_view, uint8_t>, 4> fields = {{{"Level of service", 1}, {"Average speed (km/h)", 1}, {"Free-flow travel time (s)", 0}, {"Delay (s)", 0}}};
    optional_fields(reader, message, path, flags, fields);
}

void parse_tfp(const Node &node, TPEGContentDecoder::Message &message, const std::string &path) {
    if (node.id != 5 && node.id != 6 && node.id != 7) {
        throw UnsupportedData{};
    }
    Reader reader(node.attributes);
    if (node.id == 6) {
        field(message, path, "Start time (UTC seconds)", reader.number(4));
        const auto flags = reader.selector();
        if (flags & ~uint64_t{1}) {
            throw UnsupportedData{};
        }
        if (flags & 1) {
            field(message, path, "Duration (min)", reader.multibyte());
        }
        field(message, path, "Spatial resolution", reader.byte());
        message.summary = "Traffic flow matrix";
    } else if (node.id == 7) {
        field(message, path, "Time offset (min)", reader.multibyte());
        const auto count = reader.multibyte();
        if (count > 128) {
            throw ParseError{};
        }
        field(message, path, "Sections", count);
        for (size_t index = 0; index < count; index++) {
            const auto section_path = path + "/Section " + std::to_string(index + 1);
            field(message, section_path, "Spatial offset", reader.multibyte());
            parse_status_parameters(reader, message, section_path);
            const auto flags = reader.selector();
            if (flags & ~uint64_t{3}) {
                throw UnsupportedData{};
            }
            if (flags & 1) {
                field(message, section_path, "Spatial resolution", reader.byte());
            }
            if (flags & 2) {
                field(message, section_path, "Section type", reader.byte());
            }
        }
        const auto flags = reader.selector();
        if (flags & ~uint64_t{1}) {
            throw UnsupportedData{};
        }
        if (flags & 1) {
            field(message, path, "Spatial resolution", reader.byte());
        }
    } else if (node.id == 5) {
        field(message, path, "Start time (UTC seconds)", reader.number(4));
        const auto flags = reader.selector();
        if (flags & ~uint64_t{1}) {
            throw UnsupportedData{};
        }
        if (flags & 1) {
            field(message, path, "Duration (min)", reader.multibyte());
        }
        parse_status_parameters(reader, message, path);
        message.summary = "Traffic flow status";
    }
    remaining_fields(reader, message, path);
    for (const auto &child: node.children) {
        parse_tfp(child, message, path + "/" + std::to_string(child.id));
    }
}
}

void TPEGContentDecoder::reset() {
    services.clear();
    messages.clear();
    status = {};
}

void TPEGContentDecoder::process_frame(const TPEGDecoder::Frame &frame) {
    if (frame.type != 1 || frame.encryption != 0) {
        return;
    }
    auto found = std::ranges::find(services, frame.service, &Service::id);
    if (found == services.end()) {
        if (services.size() >= MAX_SERVICES) {
            return;
        }
        Service service;
        service.id = frame.service;
        services.push_back(std::move(service));
        found = std::prev(services.end());
    }
    // SNI can follow application data in the same frame.
    bool sni_valid = true;
    for (const auto &component: frame.components) {
        if (component.id == 0) {
            Service updated = *found;
            if (process_sni(component, updated)) {
                std::erase_if(messages, [&](const Message &message) {
                    if (message.service != frame.service) {
                        return false;
                    }
                    const auto before = std::ranges::find(found->applications, message.component_id, &Application::component_id);
                    const auto after = std::ranges::find(updated.applications, message.component_id, &Application::component_id);
                    return after == updated.applications.end() || before == found->applications.end() || before->id != after->id || before->content_id != after->content_id;
                });
                *found = std::move(updated);
            } else {
                sni_valid = false;
            }
        }
    }
    if (!sni_valid) {
        return;
    }
    for (const auto &component: frame.components) {
        if (component.id == 0) {
            continue;
        }
        const auto application = std::ranges::find(found->applications, component.id, &Application::component_id);
        if (application == found->applications.end() || application->encryption != 0 || (application->id != 5 && application->id != 7) || (application->major_version != 0 && application->major_version != (application->id == 5 ? 3 : 1))) {
            status.unsupported_components++;
            continue;
        }
        process_application(component, *application, frame);
    }
}

bool TPEGContentDecoder::process_sni(const TPEGDecoder::Component &component, Service &service) {
    const auto &data = component.data;
    if (data.size() < 3) {
        status.malformed_components++;
        return false;
    }
    if (crc(std::span(data).first(data.size() - 2)) != (static_cast<uint16_t>(data[data.size() - 2]) << 8 | data.back())) {
        status.data_crc_errors++;
        return false;
    }
    try {
        Reader reader{std::span(data).first(data.size() - 2)};
        const auto count = reader.byte();
        std::vector<std::pair<uint8_t, std::span<const uint8_t>>> records;
        for (size_t index = 0; index < count; index++) {
            const auto id = reader.byte();
            records.emplace_back(id, reader.take(reader.number(2)).data);
        }
        // Resolve SCIDs before applying version, content and message-count tables.
        std::stable_sort(records.begin(), records.end(), [](const auto &left, const auto &right) {
            return (left.first == 1) > (right.first == 1);
        });
        for (const auto &[id, data]: records) {
            Reader body(data);
            if (id == 0) {
                service.name = body.string();
                service.description = body.string();
            } else if (id == 1) {
                const auto version = body.byte();
                service.character_encoding = body.byte();
                auto applications = service.applications;
                service.applications.clear();
                service.table_version = version;
                while (!body.data.empty()) {
                    Application application;
                    application.component_id = body.byte();
                    const auto flags = body.byte();
                    if (flags & 1) {
                        body.take(3);
                    }
                    application.content_id = body.byte();
                    application.id = static_cast<uint16_t>(body.number(2));
                    if (flags & 4) {
                        body.take(8);
                    }
                    if (flags & 8) {
                        application.encryption = body.byte();
                    }
                    const auto previous = std::ranges::find(applications, application.component_id, &Application::component_id);
                    if (previous != applications.end() && previous->id == application.id && previous->content_id == application.content_id) {
                        application.major_version = previous->major_version;
                        application.minor_version = previous->minor_version;
                        application.message_count = previous->message_count;
                        application.description = previous->description;
                    }
                    if (std::ranges::find(service.applications, application.component_id, &Application::component_id) != service.applications.end()) {
                        throw ParseError{};
                    }
                    service.applications.push_back(application);
                }
            } else if (id == 3 || id == 14 || id == 33) {
                body.byte();
                while (!body.data.empty()) {
                    const auto component_id = body.byte();
                    auto application = std::ranges::find(service.applications, component_id, &Application::component_id);
                    std::string description;
                    uint8_t major = 0;
                    uint8_t minor = 0;
                    uint32_t count = 0;
                    if (id == 3) {
                        description = body.string();
                    }
                    if (id == 14) {
                        major = body.byte();
                        minor = body.byte();
                    }
                    if (id == 33) {
                        count = body.number(4);
                    }
                    if (application != service.applications.end()) {
                        if (id == 3) {
                            application->description = description;
                        }
                        if (id == 14) {
                            application->major_version = major;
                            application->minor_version = minor;
                        }
                        if (id == 33) {
                            application->message_count = count;
                        }
                    }
                }
            }
        }
        if (!reader.data.empty()) {
            throw ParseError{};
        }
    } catch (const ParseError &) {
        status.malformed_components++;
        return false;
    }
    return true;
}

bool TPEGContentDecoder::process_application(const TPEGDecoder::Component &component, const Application &application, const TPEGDecoder::Frame &frame) {
    const auto &data = component.data;
    if (data.size() < 4) {
        status.malformed_components++;
        return false;
    }
    if (crc(std::span(data).first(data.size() - 2)) != (static_cast<uint16_t>(data[data.size() - 2]) << 8 | data.back())) {
        status.data_crc_errors++;
        return false;
    }
    try {
        Reader reader{std::span(data).first(data.size() - 2)};
        const auto priority = reader.byte();
        const auto count = reader.byte();
        size_t node_count = 0;
        const auto nodes = read_nodes(reader, node_count);
        if (nodes.size() != count) {
            throw ParseError{};
        }
        std::vector<Message> decoded;
        for (const auto &node: nodes) {
            if (node.id != 0 || node.children.empty() || node.children.front().id != 1) {
                status.unsupported_components++;
                continue;
            }
            Message message;
            message.service = frame.service;
            message.component_id = component.id;
            message.application_id = application.id;
            message.sequence = frame.sequence;
            message.priority = priority;
            message.data.assign(node.data.begin(), node.data.end());
            Reader management(node.children.front().attributes);
            message.id = management.multibyte();
            message.version = management.byte();
            message.expiry = management.number(4);
            const auto flags = management.selector();
            message.cancelled = flags & 1;
            if (flags & 2) {
                message.generation_time = management.number(4);
            }
            if (flags & 4) {
                message.priority = management.byte();
            }
            for (size_t index = 1; index < node.children.size(); index++) {
                const auto &child = node.children[index];
                if (child.id == 2) {
                    parse_location(child, message, "Location");
                } else {
                    try {
                        if (application.id == 5) {
                            parse_tec(child, message, "TEC/" + std::to_string(child.id), application.minor_version);
                        }
                        if (application.id == 7) {
                            parse_tfp(child, message, "TFP/" + std::to_string(child.id));
                        }
                    } catch (const UnsupportedData &) {
                        status.unsupported_components++;
                        field(message, "Application", "Unsupported attributes", hex(child.attributes));
                    }
                }
            }
            if (message.cancelled) {
                message.summary = "Cancelled";
            }
            if (message.summary.empty()) {
                message.summary = application.id == 5 ? "Traffic event" : "Traffic flow";
            }
            decoded.push_back(std::move(message));
        }
        for (auto &message: decoded) {
            status.messages_received++;
            const auto previous = std::ranges::find_if(messages, [&](const Message &item) {
                return item.service == message.service && item.component_id == message.component_id && item.id == message.id;
            });
            if (previous != messages.end()) {
                if (message.generation_time && previous->generation_time && message.generation_time < previous->generation_time) {
                    continue;
                }
                const auto now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                if (previous->expiry > now && !message.generation_time && static_cast<uint8_t>(message.version - previous->version) > 127) {
                    continue;
                }
                *previous = std::move(message);
            } else {
                messages.push_back(std::move(message));
            }
        }
        size_t cache_size = 0;
        for (const auto &message: messages) {
            cache_size += message.data.size() + message.summary.size();
            for (const auto &field: message.fields) {
                cache_size += field.component.size() + field.name.size() + field.value.size() + sizeof(Field);
            }
        }
        while (messages.size() > MAX_MESSAGES || cache_size > MAX_CACHE_SIZE) {
            const auto oldest = std::ranges::min_element(messages, {}, &Message::sequence);
            cache_size -= oldest->data.size() + oldest->summary.size();
            for (const auto &field: oldest->fields) {
                cache_size -= field.component.size() + field.name.size() + field.value.size() + sizeof(Field);
            }
            messages.erase(oldest);
        }
    } catch (const ParseError &) {
        status.malformed_components++;
        return false;
    }
    return true;
}

std::vector<TPEGContentDecoder::Service> TPEGContentDecoder::get_services() const {
    return services;
}

std::vector<TPEGContentDecoder::Message> TPEGContentDecoder::get_messages() const {
    return messages;
}

TPEGContentDecoder::Status TPEGContentDecoder::get_status() const {
    return status;
}
