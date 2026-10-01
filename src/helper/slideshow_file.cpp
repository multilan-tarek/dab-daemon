//
// Created by tarek on 10/1/26.
//

#include "slideshow_file.h"
#include <filesystem>
#include <format>
#include <fstream>

SlideShowFile::SlideShowFile(const int tuner_id) {
    logger.set_id(tuner_id);
    path = std::format("/tmp/dab-daemon/tuner-{}/slideshow", tuner_id);
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
}

SlideShowFile::~SlideShowFile() {
    clear();
}

std::string SlideShowFile::get_path() const {
    return path;
}

void SlideShowFile::write(const std::vector<uint8_t> &data, const std::string &) {
    const std::string temporary_path = path + ".tmp";
    std::ofstream output(temporary_path, std::ios::binary | std::ios::trunc);

    if (!output) {
        logger.warning(std::format("Could not open {}", temporary_path));
        return;
    }

    output.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
    output.close();

    if (!output) {
        std::filesystem::remove(temporary_path);
        logger.warning(std::format("Could not write {}", temporary_path));
        return;
    }

    std::error_code error;
    std::filesystem::rename(temporary_path, path, error);

    if (error) {
        std::filesystem::remove(temporary_path);
        logger.warning(std::format("Could not publish {}: {}", path, error.message()));
        return;
    }

    logger.info(std::format("Updated: {}", path));
}

void SlideShowFile::clear() const {
    std::filesystem::remove(path);
    std::filesystem::remove(path + ".tmp");
}
