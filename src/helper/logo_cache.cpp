#include "logo_cache.h"
#include "logo_helper.h"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>

namespace {
constexpr size_t MAX_CACHE_BYTES = 32 * 1024 * 1024;
constexpr size_t MAX_CACHE_LOGOS = 256;
constexpr auto MAX_LOGO_AGE = std::chrono::hours(24 * 30);
std::mutex cache_mutex;
}

LogoCache::LogoCache(const int tuner_id, const std::string &directory) {
    logger.set_id(tuner_id);
    this->directory = directory;

    if (this->directory.empty()) {
        const char *cache_home = std::getenv("XDG_CACHE_HOME");
        const char *user_home = std::getenv("HOME");

        if (cache_home && std::filesystem::path(cache_home).is_absolute()) {
            this->directory = (std::filesystem::path(cache_home) / "dab-daemon/logos").string();
        } else if (user_home && std::filesystem::path(user_home).is_absolute()) {
            this->directory = (std::filesystem::path(user_home) / ".cache/dab-daemon/logos").string();
        }
    }
}

std::vector<LogoCache::Logo> LogoCache::load(const uint8_t ecc, const uint16_t ensemble_id) const {
    const std::lock_guard lock(cache_mutex);
    std::vector<Logo> result;

    if (directory.empty() || ecc == 0 || ensemble_id == 0) {
        return result;
    }

    prune();
    std::error_code error;
    const auto ensemble_directory = std::filesystem::path(directory) / std::format("{:02X}-{:04X}", ecc, ensemble_id);
    std::filesystem::directory_iterator iterator(ensemble_directory, error);

    while (!error && iterator != std::filesystem::directory_iterator{}) {
        const auto path = iterator->path();
        iterator.increment(error);
        const std::string stem = path.stem().string();
        uint32_t service_id = 0;
        const auto parsed = std::from_chars(stem.data(), stem.data() + stem.size(), service_id, 16);

        if (stem.size() != 8 || parsed.ec != std::errc{} || parsed.ptr != stem.data() + stem.size()) {
            continue;
        }

        const auto [size, extension] = LogoHelper::image_info(LogoHelper::read_image(path));

        if (size != 0 && path.extension() == "." + extension) {
            const auto modified = std::filesystem::last_write_time(path, error);
            result.push_back({service_id, ecc, ensemble_id, size, path.string(), extension == "png" ? "image/png" : "image/jpeg", modified.time_since_epoch().count()});
        }
    }

    return result;
}

std::optional<LogoCache::Logo> LogoCache::store(const uint32_t service_id, const uint8_t ecc, const uint16_t ensemble_id, const std::string &path) const {
    const std::lock_guard lock(cache_mutex);

    if (directory.empty() || ecc == 0 || ensemble_id == 0 || service_id == 0) {
        return std::nullopt;
    }

    const auto data = LogoHelper::read_image(path);
    const auto [size, extension] = LogoHelper::image_info(data);

    if (size == 0) {
        return std::nullopt;
    }

    std::error_code error;
    const auto ensemble_directory = std::filesystem::path(directory) / std::format("{:02X}-{:04X}", ecc, ensemble_id);
    std::filesystem::create_directories(ensemble_directory, error);

    if (error) {
        logger.warning(std::format("Could not create logo cache: {}", error.message()));
        return std::nullopt;
    }

    const auto target = ensemble_directory / std::format("{:08X}.{}", service_id, extension);

    for (const std::string old_extension: {"png", "jpg"}) {
        const auto old_path = ensemble_directory / std::format("{:08X}.{}", service_id, old_extension);
        const auto old_data = LogoHelper::read_image(old_path);
        const auto [old_size, old_type] = LogoHelper::image_info(old_data);
        const auto modified = std::filesystem::last_write_time(old_path, error);
        const bool fresh = !error && std::filesystem::file_time_type::clock::now() - modified < MAX_LOGO_AGE;

        if (fresh && (old_size > size || (old_path == target && old_data == data))) {
            return Logo{service_id, ecc, ensemble_id, old_size, old_path.string(), old_type == "png" ? "image/png" : "image/jpeg", modified.time_since_epoch().count()};
        }
    }

    const auto temporary = target.string() + ".tmp";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
    output.close();

    if (!output) {
        std::filesystem::remove(temporary, error);
        logger.warning("Could not write station logo cache");
        return std::nullopt;
    }

    std::filesystem::rename(temporary, target, error);

    if (error) {
        std::filesystem::remove(temporary, error);
        logger.warning("Could not publish station logo cache");
        return std::nullopt;
    }

    const auto old_path = ensemble_directory / std::format("{:08X}.{}", service_id, extension == "png" ? "jpg" : "png");
    std::filesystem::remove(old_path, error);
    prune();
    logger.debug(std::format("Cached service 0x{:X}: {}", service_id, target.string()));
    const auto modified = std::filesystem::last_write_time(target, error);
    return Logo{service_id, ecc, ensemble_id, size, target.string(), extension == "png" ? "image/png" : "image/jpeg", modified.time_since_epoch().count()};
}


void LogoCache::prune() const {
    struct File {
        std::filesystem::path path;
        std::filesystem::file_time_type modified;
        uintmax_t size;
    };

    std::vector<File> files;
    uintmax_t total = 0;
    std::error_code error;
    const auto now = std::filesystem::file_time_type::clock::now();
    std::filesystem::recursive_directory_iterator iterator(directory, std::filesystem::directory_options::skip_permission_denied, error);

    while (!error && iterator != std::filesystem::recursive_directory_iterator{}) {
        const auto path = iterator->path();
        iterator.increment(error);

        if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(path, error)) || (path.extension() != ".png" && path.extension() != ".jpg")) {
            error.clear();
            continue;
        }

        const auto size = std::filesystem::file_size(path, error);
        const auto modified = std::filesystem::last_write_time(path, error);

        if (error) {
            error.clear();
            continue;
        }

        if (size > LogoHelper::MAX_BYTES || now - modified >= MAX_LOGO_AGE) {
            std::filesystem::remove(path, error);
            error.clear();
            continue;
        }

        files.push_back({path, modified, size});
        total += size;
    }

    std::ranges::sort(files, {}, &File::modified);
    size_t count = files.size();

    for (const auto &file: files) {
        if (count <= MAX_CACHE_LOGOS && total <= MAX_CACHE_BYTES) {
            break;
        }

        if (std::filesystem::remove(file.path, error)) {
            total -= file.size;
            count--;
        }
    }
}
