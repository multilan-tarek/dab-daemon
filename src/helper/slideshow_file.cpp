#include "slideshow_file.h"
#include <filesystem>
#include <format>
#include <fstream>

SlideShowFile::SlideShowFile(const int tuner_id) {
    logger.set_id(tuner_id);
    directory = std::format("/tmp/dab-daemon/tuner-{}/slideshow", tuner_id);
    std::filesystem::create_directories(directory);
}

SlideShowFile::~SlideShowFile() {
    clear();
}

std::string SlideShowFile::write(const uint16_t transport_id, const std::vector<uint8_t> &data, const std::string &extension) {
    std::filesystem::create_directories(directory);
    const std::string path = std::format("{}/{:04X}-{:08X}.{}", directory, transport_id, sequence++, extension);
    const std::string temporary_path = path + ".tmp";
    std::ofstream output(temporary_path, std::ios::binary | std::ios::trunc);

    if (!output) {
        logger.warning(std::format("Could not open {}", temporary_path));
        return {};
    }

    output.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
    output.close();

    if (!output) {
        std::filesystem::remove(temporary_path);
        logger.warning(std::format("Could not write {}", temporary_path));
        return {};
    }

    std::error_code error;
    std::filesystem::rename(temporary_path, path, error);

    if (error) {
        std::filesystem::remove(temporary_path);
        logger.warning(std::format("Could not publish {}: {}", path, error.message()));
        return {};
    }

    logger.info(std::format("Updated: {}", path));
    return path;
}

void SlideShowFile::remove(const std::string &path) const {
    if (path.empty()) {
        return;
    }

    std::error_code error;
    std::filesystem::remove(path, error);

    if (error) {
        logger.warning(std::format("Could not remove {}: {}", path, error.message()));
    }
}

void SlideShowFile::clear() const {
    std::error_code error;
    std::filesystem::remove_all(directory, error);
}
