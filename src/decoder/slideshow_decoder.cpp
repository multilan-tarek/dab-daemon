#include "slideshow_decoder.h"
#include <algorithm>
#include <chrono>

namespace {
constexpr size_t MAX_OBJECT_SIZE = 4 * 1024 * 1024;
constexpr size_t MAX_OBJECTS = 64;
constexpr size_t MAX_SEGMENTS = 2048;
constexpr size_t MAX_CACHED_SERVICES = 16;
constexpr size_t MAX_CACHE_SIZE = 8 * 1024 * 1024;
constexpr auto CACHE_LIFETIME = std::chrono::minutes(5);
constexpr uint8_t CONTENT_TYPE_IMAGE = 2;
constexpr uint8_t CONTENT_TYPE_MOT_TRANSPORT = 5;
constexpr uint16_t CONTENT_SUBTYPE_JFIF = 1;
constexpr uint16_t CONTENT_SUBTYPE_PNG = 3;
constexpr uint16_t CONTENT_SUBTYPE_HEADER_UPDATE = 0;
}

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

        if (result.size() + segment.size() > MAX_OBJECT_SIZE) {
            return {};
        }

        result.insert(result.end(), segment.begin(), segment.end());
    }

    return result;
}

SlideShowDecoder::SlideShowDecoder(const SlideCallback &slide_callback, const SlideCallback &display_callback, const SlideRemovedCallback &removed_callback) {
    this->slide_callback = slide_callback;
    this->display_callback = display_callback;
    this->removed_callback = removed_callback;
}

void SlideShowDecoder::reset() {
    data_group_length = 0;
    data_group.clear();
    objects.clear();
    slides.clear();
    current_time = 0;
    current_time_valid = false;
    service_key = 0;
#if DAB_ENABLE_DIAGNOSTICS
    statistics = {};
    reception_started = std::chrono::steady_clock::now();
#endif
}

void SlideShowDecoder::select_service(const uint32_t frequency, const uint16_t ensemble_id, const uint16_t service_id) {
    reset();
    service_key = static_cast<uint64_t>(frequency) << 32 | static_cast<uint64_t>(ensemble_id) << 16 | service_id;

    if (reference_time_valid) {
        current_time = reference_time + std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - reference_time_received).count();
        current_time_valid = true;
    }

    prune_cache();

    const auto cached = cached_slides.find(service_key);

    if (cached == cached_slides.end()) {
        return;
    }

    const Slide slide = cached->second.slide;
    StoredSlide &stored_slide = slides[slide.content_name];
    stored_slide.slide = slide;

    if (slide_callback) {
        slide_callback(stored_slide.slide);
    }

    display_slide(stored_slide, true);
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

void SlideShowDecoder::update_time(const int64_t timestamp) {
    const bool had_time = current_time_valid;
    current_time = timestamp;
    current_time_valid = true;
    reference_time = timestamp;
    reference_time_valid = true;
    reference_time_received = std::chrono::steady_clock::now();

    prune_cache();
    expire_slides();

    for (auto &[content_name, stored_slide]: slides) {
        if (!stored_slide.trigger_pending || stored_slide.trigger_consumed || stored_slide.slide.trigger_now || !stored_slide.slide.trigger_time_present) {
            continue;
        }

        if (!had_time && stored_slide.slide.trigger_time < current_time) {
            stored_slide.trigger_pending = false;
            continue;
        }

        if (stored_slide.slide.trigger_time <= current_time) {
            display_slide(stored_slide);
        }
    }
}

void SlideShowDecoder::process_group(const std::vector<uint8_t> &group) {
#if DAB_ENABLE_DIAGNOSTICS
    statistics.groups_received++;
#endif

    if (group.size() < 4) {
        return;
    }

    const bool extension_present = (group[0] & 0x80) != 0;
    const bool crc_present = (group[0] & 0x40) != 0;
    const bool segment_present = (group[0] & 0x20) != 0;
    const bool user_access_present = (group[0] & 0x10) != 0;
    const uint8_t type = group[0] & 0x0F;

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
#if DAB_ENABLE_DIAGNOSTICS
            statistics.group_crc_errors++;
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
    statistics.groups_accepted++;
    statistics.segments++;
#endif

    if (!objects.contains(transport_id) && objects.size() >= MAX_OBJECTS) {
        objects.erase(objects.begin());
    }

    Object &object = objects[transport_id];

    // TransportId identifies the object version. The data-group continuity index
    // changes between segments and carousel passes, so it must not clear them.
    if (object.emitted || (type == 3 && object.header_complete)) {
        return;
    }

    Segments &segments = type == 3 ? object.header_segments : object.body_segments;
    segments.values[segment_number] = std::vector<uint8_t>(group.begin() + static_cast<std::ptrdiff_t>(offset), group.begin() + static_cast<std::ptrdiff_t>(offset + segment_size));

    if (last) {
        segments.last = segment_number;
    }

#if DAB_ENABLE_DIAGNOSTICS
    if (type == 4) {
        statistics.transport_id = transport_id;
        statistics.received_segments = static_cast<uint32_t>(segments.values.size());
        statistics.total_segments_known = segments.last >= 0;
        statistics.total_segments = statistics.total_segments_known ? static_cast<uint32_t>(segments.last + 1) : 0;
    }
#endif

    update_object(transport_id);
}

void SlideShowDecoder::update_object(const uint16_t transport_id) {
    Object &object = objects[transport_id];

    if (!object.header_complete && object.header_segments.complete()) {
        const std::vector<uint8_t> header = object.header_segments.get_data();

        if (!parse_header(object, header)) {
            return;
        }
    }

    if (!object.header_complete || object.emitted) {
        return;
    }

    if (object.content_type == CONTENT_TYPE_MOT_TRANSPORT && object.content_subtype == CONTENT_SUBTYPE_HEADER_UPDATE && object.body_size == 0) {
        object.emitted = true;
        apply_header_update(object.parameters);
        return;
    }

    if (object.content_type != CONTENT_TYPE_IMAGE || (object.content_subtype != CONTENT_SUBTYPE_JFIF && object.content_subtype != CONTENT_SUBTYPE_PNG) || !object.body_segments.complete()) {
        return;
    }

    const std::vector<uint8_t> body = object.body_segments.get_data();

    if (body.size() != object.body_size || object.parameters.content_name.empty()) {
        return;
    }

    const std::string extension = detect_extension(body);

    if (extension.empty()) {
        return;
    }

    Slide slide;
    slide.transport_id = transport_id;
    slide.content_name = object.parameters.content_name;
    slide.data = body;
    slide.extension = extension;
    slide.trigger_time_present = object.parameters.trigger_time_present;
    slide.trigger_now = object.parameters.trigger_now;
    slide.trigger_time = object.parameters.trigger_time;
    slide.expire_time_present = object.parameters.expire_time_present;
    slide.expire_time = object.parameters.expire_time;
    slide.category_id = object.parameters.category_id;
    slide.slide_id = object.parameters.slide_id;
    slide.category_title = object.parameters.category_title;
    slide.click_through_url = object.parameters.click_through_url;
    slide.alternative_location_url = object.parameters.alternative_location_url;
    slide.alert = object.parameters.alert;
    object.emitted = true;
#if DAB_ENABLE_DIAGNOSTICS
    statistics.objects++;
#endif
    store_slide(slide);
}

bool SlideShowDecoder::parse_header(Object &object, const std::vector<uint8_t> &header) {
    if (header.size() < 7) {
        return false;
    }

    object.body_size = static_cast<uint32_t>(header[0]) << 20 | static_cast<uint32_t>(header[1]) << 12 | static_cast<uint32_t>(header[2]) << 4 | header[3] >> 4;
    object.header_size = static_cast<uint16_t>((header[3] & 0x0F) << 9 | header[4] << 1 | header[5] >> 7);
    object.content_type = header[5] >> 1 & 0x3F;
    object.content_subtype = static_cast<uint16_t>((header[5] & 0x01) << 8 | header[6]);

    if (object.header_size < 7 || object.header_size > header.size() || object.body_size > MAX_OBJECT_SIZE) {
        return false;
    }

    HeaderParameters parameters;
    size_t offset = 7;

    while (offset < object.header_size) {
        uint8_t parameter_id = 0;
        std::vector<uint8_t> parameter_data;

        if (!read_parameter(header, offset, parameter_id, parameter_data)) {
            return false;
        }

        if (parameter_id == 0x0C && !parameter_data.empty()) {
            parameters.content_name.assign(parameter_data.begin() + 1, parameter_data.end());
        } else if (parameter_id == 0x05) {
            parameters.trigger_time_present = decode_time(parameter_data, parameters.trigger_now, parameters.trigger_time);
        } else if (parameter_id == 0x04) {
            bool expire_now = false;
            parameters.expire_time_present = decode_time(parameter_data, expire_now, parameters.expire_time) && !expire_now;
        } else if (parameter_id == 0x25 && parameter_data.size() == 2) {
            parameters.category_present = true;
            parameters.category_id = parameter_data[0];
            parameters.slide_id = parameter_data[1];
        } else if (parameter_id == 0x26 && parameter_data.size() <= 128) {
            parameters.category_title_present = true;
            parameters.category_title.assign(parameter_data.begin(), parameter_data.end());
        } else if (parameter_id == 0x27 && parameter_data.size() <= 512) {
            parameters.click_through_url.assign(parameter_data.begin(), parameter_data.end());
        } else if (parameter_id == 0x28 && parameter_data.size() <= 512) {
            parameters.alternative_location_url.assign(parameter_data.begin(), parameter_data.end());
        } else if (parameter_id == 0x29 && parameter_data.size() == 1) {
            parameters.alert = parameter_data[0];
        }
    }

    object.parameters = std::move(parameters);
    object.header_complete = true;
#if DAB_ENABLE_DIAGNOSTICS
    statistics.header_parses++;
#endif
    return true;
}

void SlideShowDecoder::store_slide(const Slide &slide) {
    if (!slides.contains(slide.content_name) && slides.size() >= MAX_OBJECTS) {
        const std::string removed_content_name = slides.begin()->first;
        slides.erase(slides.begin());

        if (removed_callback) {
            removed_callback(removed_content_name);
        }
    }

    const auto existing = slides.find(slide.content_name);

    if (existing != slides.end() && existing->second.slide.expire_time_present && !slide.expire_time_present) {
        Slide stored_slide = slide;
        stored_slide.expire_time_present = true;
        stored_slide.expire_time = existing->second.slide.expire_time;
        existing->second = {stored_slide};
    } else {
        slides[slide.content_name] = {slide};
    }

    StoredSlide &stored_slide = slides[slide.content_name];
    apply_category(slide.content_name, true, stored_slide.slide.category_id, stored_slide.slide.slide_id, stored_slide.slide.category_title, !stored_slide.slide.category_title.empty());

    if (slide_callback) {
        slide_callback(stored_slide.slide);
    }

    arm_trigger(stored_slide, false);
    expire_slides();
}

void SlideShowDecoder::apply_header_update(const HeaderParameters &parameters) {
    if (parameters.content_name.empty()) {
        return;
    }

    const auto iterator = slides.find(parameters.content_name);

    if (iterator == slides.end()) {
        return;
    }

    StoredSlide &stored_slide = iterator->second;

    if (parameters.category_present) {
        apply_category(parameters.content_name, true, parameters.category_id, parameters.slide_id, {}, false);
    }

    if (parameters.expire_time_present) {
        stored_slide.slide.expire_time_present = true;
        stored_slide.slide.expire_time = parameters.expire_time;
    }

    if (parameters.trigger_time_present) {
        stored_slide.slide.trigger_time_present = true;
        stored_slide.slide.trigger_now = parameters.trigger_now;
        stored_slide.slide.trigger_time = parameters.trigger_time;
        stored_slide.trigger_consumed = false;
        arm_trigger(stored_slide, true);
    }

    const auto cached = cached_slides.find(service_key);

    if (cached != cached_slides.end() && cached->second.slide.content_name == parameters.content_name) {
        cached->second.slide = stored_slide.slide;
    }

    if (slide_callback) {
        Slide updated_slide = stored_slide.slide;
        updated_slide.metadata_update = true;
        slide_callback(updated_slide);
    }

    expire_slides();
}

void SlideShowDecoder::apply_category(const std::string &content_name, const bool category_present, const uint8_t category_id, const uint8_t slide_id, const std::string &category_title, const bool category_title_present) {
    if (!category_present) {
        return;
    }

    if (category_id != 0 && slide_id != 0) {
        for (auto &[other_content_name, other_slide]: slides) {
            if (other_content_name == content_name || other_slide.slide.category_id != category_id || other_slide.slide.slide_id != slide_id) {
                continue;
            }

            other_slide.slide.category_id = 0;
            other_slide.slide.slide_id = 0;

            if (slide_callback) {
                Slide updated_slide = other_slide.slide;
                updated_slide.metadata_update = true;
                slide_callback(updated_slide);
            }
        }
    }

    const auto iterator = slides.find(content_name);

    if (iterator == slides.end()) {
        return;
    }

    iterator->second.slide.category_id = category_id;
    iterator->second.slide.slide_id = slide_id;

    if (category_title_present && category_id != 0) {
        for (auto &[other_content_name, other_slide]: slides) {
            if (other_slide.slide.category_id != category_id) {
                continue;
            }

            other_slide.slide.category_title = category_title;

            if (other_content_name != content_name && slide_callback) {
                Slide updated_slide = other_slide.slide;
                updated_slide.metadata_update = true;
                slide_callback(updated_slide);
            }
        }
    }
}

void SlideShowDecoder::arm_trigger(StoredSlide &stored_slide, const bool from_update) {
    stored_slide.trigger_pending = false;

    if (stored_slide.trigger_consumed) {
        return;
    }

    if (!stored_slide.slide.trigger_time_present || stored_slide.slide.trigger_now) {
        display_slide(stored_slide);
        return;
    }

    if (!current_time_valid) {
        stored_slide.trigger_pending = true;
        return;
    }

    if (stored_slide.slide.trigger_time == current_time || (from_update && stored_slide.slide.trigger_time <= current_time)) {
        display_slide(stored_slide);
    } else if (stored_slide.slide.trigger_time > current_time) {
        stored_slide.trigger_pending = true;
    }
}

void SlideShowDecoder::display_slide(StoredSlide &stored_slide, const bool from_cache) {
    if (current_time_valid && stored_slide.slide.expire_time_present && stored_slide.slide.expire_time <= current_time) {
        return;
    }

    stored_slide.trigger_pending = false;
    stored_slide.trigger_consumed = true;

#if DAB_ENABLE_DIAGNOSTICS
    if (!statistics.first_image_received) {
        statistics.first_image_received = true;
        statistics.first_image_ms = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - reception_started).count());
        statistics.cache_hit = from_cache;
    }
#endif

    if (!from_cache) {
        cache_slide(stored_slide.slide);
    }

    if (display_callback) {
        display_callback(stored_slide.slide);
    }
}

void SlideShowDecoder::expire_slides() {
    if (!current_time_valid) {
        return;
    }

    for (auto iterator = slides.begin(); iterator != slides.end();) {
        if (!iterator->second.slide.expire_time_present || iterator->second.slide.expire_time > current_time) {
            ++iterator;
            continue;
        }

        const std::string content_name = iterator->first;
        iterator = slides.erase(iterator);

        const auto cached = cached_slides.find(service_key);

        if (cached != cached_slides.end() && cached->second.slide.content_name == content_name) {
            cached_slides.erase(cached);
        }

        if (removed_callback) {
            removed_callback(content_name);
        }
    }
}

void SlideShowDecoder::cache_slide(const Slide &slide) {
    if (service_key == 0) {
        return;
    }

    cached_slides[service_key] = {slide, std::chrono::steady_clock::now()};
    prune_cache();
}

void SlideShowDecoder::prune_cache() {
    const auto now = std::chrono::steady_clock::now();
    const int64_t timestamp = reference_time_valid ? reference_time + std::chrono::duration_cast<std::chrono::seconds>(now - reference_time_received).count() : 0;
    size_t cache_size = 0;

    for (auto iterator = cached_slides.begin(); iterator != cached_slides.end();) {
        const auto &cached = iterator->second;

        if (now - cached.received >= CACHE_LIFETIME || (cached.slide.expire_time_present && (!reference_time_valid || cached.slide.expire_time <= timestamp))) {
            iterator = cached_slides.erase(iterator);
            continue;
        }

        cache_size += cached.slide.data.size();
        iterator++;
    }

    while (cached_slides.size() > MAX_CACHED_SERVICES || cache_size > MAX_CACHE_SIZE) {
        const auto oldest = std::min_element(cached_slides.begin(), cached_slides.end(), [](const auto &left, const auto &right) {
            return left.second.received < right.second.received;
        });

        cache_size -= oldest->second.slide.data.size();
        cached_slides.erase(oldest);
    }
}

#if DAB_ENABLE_DIAGNOSTICS
SlideShowDecoder::Statistics SlideShowDecoder::get_statistics() const {
    return statistics;
}
#endif

bool SlideShowDecoder::read_parameter(const std::vector<uint8_t> &data, size_t &offset, uint8_t &parameter_id, std::vector<uint8_t> &parameter_data) {
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

bool SlideShowDecoder::decode_time(const std::vector<uint8_t> &data, bool &now, int64_t &timestamp) {
    if (data.size() != 4 && data.size() != 6) {
        return false;
    }

    const bool validity = (data[0] & 0x80) != 0;
    now = !validity;
    timestamp = 0;

    if (!validity) {
        return true;
    }

    const uint32_t mjd = static_cast<uint32_t>(data[0] & 0x7F) << 10 | static_cast<uint32_t>(data[1]) << 2 | data[2] >> 6;
    const bool utc_long = (data[2] & 0x08) != 0;

    if (utc_long != (data.size() == 6)) {
        return false;
    }

    const uint8_t hour = static_cast<uint8_t>((data[2] & 0x07) << 2 | data[3] >> 6);
    const uint8_t minute = data[3] & 0x3F;
    uint8_t second = 0;

    if (data.size() == 6) {
        second = data[4] >> 2;
    }

    if (hour >= 24 || minute >= 60 || second >= 60) {
        return false;
    }

    using namespace std::chrono;
    const sys_seconds utc = sys_days{1970y / January / 1} + days{static_cast<int64_t>(mjd) - 40587} + hours{hour} + minutes{minute} + seconds{second};
    timestamp = utc.time_since_epoch().count();
    return true;
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

    return {};
}
