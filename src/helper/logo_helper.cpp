#include "logo_helper.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <fstream>
#include <regex>

namespace {
constexpr uint32_t MAX_LOGO_SIZE = 2048;

std::string normalized_name(const std::string &name) {
    std::string result;

    for (const unsigned char character: name) {
        if (std::isalnum(character)) {
            result.push_back(static_cast<char>(std::tolower(character)));
        }
    }

    return result;
}
}

std::vector<uint8_t> LogoHelper::read_image(const std::filesystem::path &path) {
    std::error_code error;

    if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(path, error))) {
        return {};
    }

    const auto size = std::filesystem::file_size(path, error);

    if (error || size == 0 || size > MAX_BYTES) {
        return {};
    }

    std::vector<uint8_t> data(size);
    std::ifstream input(path, std::ios::binary);
    input.read(reinterpret_cast<char *>(data.data()), static_cast<std::streamsize>(data.size()));
    return input ? data : std::vector<uint8_t>{};
}

std::pair<uint16_t, std::string> LogoHelper::image_info(const std::vector<uint8_t> &data) {
    constexpr std::array<uint8_t, 8> png_signature{0x89, 'P', 'N', 'G', 13, 10, 26, 10};
    uint32_t width = 0;
    uint32_t height = 0;
    std::string extension;

    if (data.size() >= 33 && std::equal(png_signature.begin(), png_signature.end(), data.begin()) && data[12] == 'I' && data[13] == 'H' && data[14] == 'D' && data[15] == 'R') {
        for (size_t index = 0; index < 4; index++) {
            width = width << 8 | data[16 + index];
            height = height << 8 | data[20 + index];
        }

        extension = "png";
    } else if (data.size() >= 4 && data[0] == 0xFF && data[1] == 0xD8 && data[data.size() - 2] == 0xFF && data.back() == 0xD9) {
        size_t offset = 2;

        while (offset + 4 <= data.size() && data[offset] == 0xFF) {
            while (offset < data.size() && data[offset] == 0xFF) {
                offset++;
            }

            if (offset + 3 > data.size()) {
                break;
            }

            const uint8_t marker = data[offset++];
            const size_t length = static_cast<size_t>(data[offset] << 8 | data[offset + 1]);

            if (marker == 0xDA || length < 2 || offset + length > data.size()) {
                break;
            }

            if (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC && length >= 8) {
                height = static_cast<uint32_t>(data[offset + 3] << 8 | data[offset + 4]);
                width = static_cast<uint32_t>(data[offset + 5] << 8 | data[offset + 6]);
                extension = "jpg";
                break;
            }

            offset += length;
        }
    }

    if (width == 0 || width != height || width > MAX_LOGO_SIZE) {
        return {};
    }

    return {static_cast<uint16_t>(width), extension};
}

bool LogoHelper::matches_service(const std::string &content_name, const uint16_t service_id, const std::string &label, const std::string &short_label) {
    static const std::regex pattern("([0-9a-fA-F]{4})_(.+)_([0-9]{1,4})x([0-9]{1,4})\\.(png|jpg|jpeg)", std::regex::icase);
    const std::string name = std::filesystem::path(content_name).filename().string();
    uint16_t advertised_service = 0;

    if (name.size() < 5 || name[4] != '_') {
        return false;
    }

    const auto parsed = std::from_chars(name.data(), name.data() + 4, advertised_service, 16);

    if (parsed.ec != std::errc{} || parsed.ptr != name.data() + 4 || advertised_service != service_id) {
        return false;
    }

    std::smatch match;

    if (!std::regex_match(name, match, pattern)) {
        return false;
    }

    const auto width = std::stoul(match[3]);
    const auto height = std::stoul(match[4]);

    if (width == 0 || width != height || width > MAX_LOGO_SIZE) {
        return false;
    }

    const std::string normalized = normalized_name(match[2]);
    return !normalized.empty() && (normalized == normalized_name(label) || normalized == normalized_name(short_label));
}
