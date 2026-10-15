#ifndef LOGO_HELPER_H
#define LOGO_HELPER_H

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

class LogoHelper {
public:
    static constexpr size_t MAX_BYTES = 2 * 1024 * 1024;

    static std::vector<uint8_t> read_image(const std::filesystem::path &path);

    static std::pair<uint16_t, std::string> image_info(const std::vector<uint8_t> &data);

    static bool matches_service(const std::string &content_name, uint16_t service_id, const std::string &label, const std::string &short_label);
};

#endif //LOGO_HELPER_H
