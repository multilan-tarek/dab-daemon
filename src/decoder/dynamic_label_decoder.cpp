//
// Created by tarek on 10/1/26.
//

#include "dynamic_label_decoder.h"
#include <algorithm>

bool DynamicLabelDecoder::Segment::toggle() const {
    return (prefix[0] & 0x80) != 0;
}

bool DynamicLabelDecoder::Segment::first() const {
    return (prefix[0] & 0x40) != 0;
}

bool DynamicLabelDecoder::Segment::last() const {
    return (prefix[0] & 0x20) != 0;
}

bool DynamicLabelDecoder::Segment::dl_plus_link() const {
    return (prefix[1] & 0x80) != 0;
}

int DynamicLabelDecoder::Segment::number() const {
    return first() ? 0 : (prefix[1] & 0x70) >> 4;
}

void DynamicLabelDecoder::SegmentReassembler::reset() {
    segments.clear();
    data.clear();
}

bool DynamicLabelDecoder::SegmentReassembler::add(const Segment &segment) {
    bool toggle = false;

    if (get_toggle(toggle) && toggle != segment.toggle()) {
        segments.clear();
    }

    if (segments.contains(segment.number())) {
        return false;
    }

    segments[segment.number()] = segment;
    return complete();
}

bool DynamicLabelDecoder::SegmentReassembler::complete() {
    int count = 0;

    for (int index = 0; index < 8; index++) {
        const auto segment = segments.find(index);

        if (segment == segments.end()) {
            return false;
        }

        count++;

        if (segment->second.last()) {
            break;
        }

        if (index == 7) {
            return false;
        }
    }

    data.clear();

    for (int index = 0; index < count; index++) {
        const auto &segment = segments[index];
        data.insert(data.end(), segment.characters.begin(), segment.characters.end());
    }

    return true;
}

bool DynamicLabelDecoder::SegmentReassembler::get_toggle(bool &value) const {
    if (segments.empty()) {
        return false;
    }

    value = segments.begin()->second.toggle();
    return true;
}

bool DynamicLabelDecoder::SegmentReassembler::get_dl_plus_link(bool &value) const {
    if (segments.empty()) {
        return false;
    }

    value = segments.begin()->second.dl_plus_link();
    return true;
}

int DynamicLabelDecoder::SegmentReassembler::get_charset() const {
    const auto segment = segments.find(0);

    if (segment == segments.end()) {
        return -1;
    }

    return segment->second.prefix[1] >> 4;
}

const std::vector<uint8_t> &DynamicLabelDecoder::SegmentReassembler::get_data() const {
    return data;
}

void DynamicLabelDecoder::DataGroup::reset() {
    data.clear();
    needed = 4;
}

bool DynamicLabelDecoder::DataGroup::append(const bool start, const std::vector<uint8_t> &new_data, std::vector<uint8_t> &complete_group) {
    if (start) {
        reset();
    } else if (data.empty()) {
        return false;
    }

    if (data.size() >= needed) {
        return false;
    }

    const size_t copy_length = std::min<size_t>(20 - data.size(), new_data.size());
    data.insert(data.end(), new_data.begin(), new_data.begin() + copy_length);

    if (data.size() >= 2) {
        update_needed();
    }

    if (data.size() < needed) {
        return false;
    }

    const size_t payload_length = needed - 2;
    const uint16_t stored_crc = static_cast<uint16_t>(data[payload_length] << 8 | data[payload_length + 1]);

    if (stored_crc != calculate_crc(data.data(), payload_length)) {
        reset();
        return false;
    }

    complete_group.assign(data.begin(), data.begin() + needed);
    reset();
    return true;
}

bool DynamicLabelDecoder::DataGroup::update_needed() {
    if (data.size() < 2) {
        return false;
    }

    const bool command = (data[0] & 0x10) != 0;
    size_t field_length = 0;

    if (command) {
        const uint8_t command_type = data[0] & 0x0F;

        if (command_type == 0x01) {
            field_length = 0;
        } else if (command_type == 0x02) {
            field_length = (data[1] & 0x0F) + 1;
        } else {
            reset();
            return false;
        }
    } else {
        field_length = (data[0] & 0x0F) + 1;
    }

    needed = 2 + field_length + 2;
    return true;
}

DynamicLabelDecoder::DynamicLabelDecoder(const AudioTextCallback &audio_text_callback) {
    this->audio_text_callback = audio_text_callback;
    reset();
}

void DynamicLabelDecoder::reset() {
    data_group.reset();
    label_segments.reset();
    dl_plus_segments.reset();
    audio_text = {};
    emitted_audio_text = {};
}

void DynamicLabelDecoder::process(const bool start, const std::vector<uint8_t> &data) {
    std::vector<uint8_t> group;

    if (data_group.append(start, data, group)) {
        process_group(group);
    }
}

void DynamicLabelDecoder::process_group(const std::vector<uint8_t> &group) {
    if (group.size() < 4) {
        return;
    }

    const bool command = (group[0] & 0x10) != 0;
    bool remove_label = false;
    bool dl_plus = false;
    size_t field_length = 0;

    if (command) {
        switch (group[0] & 0x0F) {
            case 0x01:
                remove_label = true;
                break;
            case 0x02:
                dl_plus = true;
                field_length = (group[1] & 0x0F) + 1;
                break;
            default:
                return;
        }
    } else {
        field_length = (group[0] & 0x0F) + 1;
    }

    if (remove_label) {
        label_segments.reset();
        dl_plus_segments.reset();
        audio_text = {};
        emit_audio_text();
        return;
    }

    if (group.size() < 2 + field_length + 2) {
        return;
    }

    Segment segment;
    segment.prefix[0] = group[0];
    segment.prefix[1] = group[1];
    segment.characters.assign(group.begin() + 2, group.begin() + 2 + field_length);

    const bool current_flag = dl_plus ? segment.dl_plus_link() : segment.toggle();
    bool previous_flag = false;

    if (label_segments.get_toggle(previous_flag) && previous_flag != current_flag) {
        label_segments.reset();
    }

    if (dl_plus_segments.get_dl_plus_link(previous_flag) && previous_flag != current_flag) {
        dl_plus_segments.reset();
    }

    if (dl_plus) {
        if (!dl_plus_segments.add(segment) || !label_segments.complete()) {
            return;
        }
    } else if (!label_segments.add(segment)) {
        return;
    }

    publish_audio_text();
}

void DynamicLabelDecoder::publish_audio_text() {
    const auto &data = label_segments.get_data();

    if (data.empty()) {
        return;
    }

    audio_text.dynamic_label = decode_text(data, label_segments.get_charset());

    if (dl_plus_segments.complete()) {
        apply_dl_plus();
    }

    emit_audio_text();
}

void DynamicLabelDecoder::apply_dl_plus() {
    const auto &command = dl_plus_segments.get_data();

    if (command.empty() || command[0] >> 4 != 0) {
        return;
    }

    const bool item_running = (command[0] & 0x04) != 0;
    const size_t object_count = (command[0] & 0x03) + 1;

    if (command.size() < 1 + object_count * 3) {
        return;
    }

    std::string title;
    std::string artist;
    std::string album;

    for (size_t index = 0; index < object_count; index++) {
        const uint8_t *object = command.data() + 1 + index * 3;
        const uint8_t content_type = object[0] & 0x7F;
        const size_t position = object[1] & 0x7F;
        const size_t length = (object[2] & 0x7F) + 1;
        const std::string value = content_type == 0 ? std::string{} : utf8_substring(audio_text.dynamic_label, position, length);

        if (content_type == 1) {
            title = value;
        } else if (content_type == 2) {
            album = value;
        } else if (content_type == 4) {
            artist = value;
        }
    }

    audio_text.title = title;
    audio_text.artist = artist;
    audio_text.album = album;
    audio_text.item_running = item_running;
}

void DynamicLabelDecoder::emit_audio_text() {
    if (audio_text == emitted_audio_text) {
        return;
    }

    emitted_audio_text = audio_text;

    if (audio_text_callback) {
        audio_text_callback(audio_text);
    }
}

uint16_t DynamicLabelDecoder::calculate_crc(const uint8_t *data, const size_t length) {
    uint16_t crc = 0xFFFF;

    for (size_t offset = 0; offset < length; offset++) {
        crc ^= static_cast<uint16_t>(data[offset]) << 8;

        for (int bit = 0; bit < 8; bit++) {
            crc = (crc & 0x8000) != 0 ? static_cast<uint16_t>(crc << 1 ^ 0x1021) : static_cast<uint16_t>(crc << 1);
        }
    }

    return static_cast<uint16_t>(~crc);
}

std::string DynamicLabelDecoder::decode_text(const std::vector<uint8_t> &data, const int charset) {
    if (charset == 0) {
        return decode_ebu_latin(data);
    }

    if (charset == 6) {
        return decode_ucs2(data);
    }

    if (charset == 15) {
        std::string result;
        result.reserve(data.size());

        for (const uint8_t value: data) {
            if (value != 0x00 && value != 0x0A && value != 0x0B && value != 0x1F) {
                result += static_cast<char>(value);
            }
        }

        return result;
    }

    return {};
}

std::string DynamicLabelDecoder::decode_ebu_latin(const std::vector<uint8_t> &data) {
    static constexpr const char *VALUES_00_1F[] = {
        "", "Ę", "Į", "Ų", "Ă", "Ė", "Ď", "Ș", "Ț", "Ċ", "", "", "Ġ", "Ĺ", "Ż", "Ń",
        "ą", "ę", "į", "ų", "ă", "ė", "ď", "ș", "ț", "ċ", "Ň", "Ě", "ġ", "ĺ", "ż", ""
    };

    static constexpr const char *VALUES_7B_FF[] = {
        "«", "ů", "»", "Ľ", "Ħ", "á", "à", "é", "è", "í", "ì", "ó", "ò", "ú", "ù", "Ñ", "Ç", "Ş", "ß", "¡", "Ÿ",
        "â", "ä", "ê", "ë", "î", "ï", "ô", "ö", "û", "ü", "ñ", "ç", "ş", "ğ", "ı", "ÿ", "Ķ", "Ņ", "©", "Ģ", "Ğ", "ě", "ň", "ő", "Ő", "€", "£", "$", "Ā", "Ē", "Ī", "Ū",
        "ķ", "ņ", "Ļ", "ģ", "ļ", "İ", "ń", "ű", "Ű", "¿", "ľ", "°", "ā", "ē", "ī", "ū", "Á", "À", "É", "È", "Í", "Ì", "Ó", "Ò", "Ú", "Ù", "Ř", "Č", "Š", "Ž", "Ð", "Ŀ",
        "Â", "Ä", "Ê", "Ë", "Î", "Ï", "Ô", "Ö", "Û", "Ü", "ř", "č", "š", "ž", "đ", "ŀ", "Ã", "Å", "Æ", "Œ", "ŷ", "Ý", "Õ", "Ø", "Þ", "Ŋ", "Ŕ", "Ć", "Ś", "Ź", "Ť", "ð",
        "ã", "å", "æ", "œ", "ŵ", "ý", "õ", "ø", "þ", "ŋ", "ŕ", "ć", "ś", "ź", "ť", "ħ"
    };

    std::string result;

    for (const uint8_t value: data) {
        if (value == 0x00 || value == 0x0A || value == 0x0B || value == 0x1F) {
            continue;
        }

        if (value <= 0x1F) {
            result += VALUES_00_1F[value];
        } else if (value >= 0x7B) {
            result += VALUES_7B_FF[value - 0x7B];
        } else if (value == 0x24) {
            result += "ł";
        } else if (value == 0x5C) {
            result += "Ů";
        } else if (value == 0x5E) {
            result += "Ł";
        } else if (value == 0x60) {
            result += "Ą";
        } else {
            result += static_cast<char>(value);
        }
    }

    return result;
}

std::string DynamicLabelDecoder::decode_ucs2(const std::vector<uint8_t> &data) {
    std::string result;

    for (size_t offset = 0; offset + 1 < data.size(); offset += 2) {
        const uint16_t codepoint = static_cast<uint16_t>(data[offset] << 8 | data[offset + 1]);

        if (codepoint != 0x0000 && codepoint != 0x000A && codepoint != 0x000B && codepoint != 0x001F) {
            append_utf8(result, codepoint);
        }
    }

    return result;
}

std::string DynamicLabelDecoder::utf8_substring(const std::string &value, const size_t position, const size_t length) {
    const size_t start = utf8_byte_offset(value, position);
    const size_t end = start + utf8_byte_offset(value.substr(start), length);
    return value.substr(start, end - start);
}

size_t DynamicLabelDecoder::utf8_byte_offset(const std::string &value, size_t characters) {
    size_t offset = 0;

    while (offset < value.size()) {
        if ((static_cast<uint8_t>(value[offset]) & 0xC0) != 0x80) {
            if (characters == 0) {
                break;
            }

            characters--;
        }

        offset++;
    }

    return offset;
}

void DynamicLabelDecoder::append_utf8(std::string &output, const uint32_t codepoint) {
    if (codepoint <= 0x7F) {
        output += static_cast<char>(codepoint);
    } else if (codepoint <= 0x7FF) {
        output += static_cast<char>(0xC0 | codepoint >> 6);
        output += static_cast<char>(0x80 | (codepoint & 0x3F));
    } else {
        output += static_cast<char>(0xE0 | codepoint >> 12);
        output += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        output += static_cast<char>(0x80 | (codepoint & 0x3F));
    }
}
