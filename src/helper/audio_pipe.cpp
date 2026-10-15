#include "audio_pipe.h"
#include <cerrno>
#include <filesystem>
#include <format>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

AudioPipe::AudioPipe(const int tuner_id) {
    logger.set_id(tuner_id);
    path = std::format("/tmp/dab-daemon/tuner-{}/audio", tuner_id);
    create();
}

AudioPipe::~AudioPipe() {
    close();
    std::filesystem::remove(path);
    std::filesystem::remove(std::filesystem::path(path).parent_path());
}

std::string AudioPipe::get_path() const {
    return path;
}

void AudioPipe::write(const std::vector<uint8_t> &data) {
    if (file_descriptor < 0) {
        file_descriptor = open(path.c_str(), O_WRONLY | O_NONBLOCK | O_CLOEXEC);

        if (file_descriptor < 0) {
            return;
        }

        logger.info(std::format("Connected: {}", path));
    }

    size_t offset = 0;

    while (offset < data.size()) {
        const ssize_t written = ::write(file_descriptor, data.data() + offset, data.size() - offset);

        if (written > 0) {
            offset += static_cast<size_t>(written);
            continue;
        }

        if (written < 0 && errno == EINTR) {
            continue;
        }

        if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return;
        }

        if (written < 0 && errno != EPIPE) {
            logger.warning(std::format("Write failed: {}", std::strerror(errno)));
        }

        close();
        return;
    }
}

void AudioPipe::close() {
    if (file_descriptor >= 0) {
        ::close(file_descriptor);
        file_descriptor = -1;
        logger.info("Disconnected");
    }
}

void AudioPipe::create() {
    const std::filesystem::path pipe_path(path);
    std::filesystem::create_directories(pipe_path.parent_path());

    if (std::filesystem::exists(pipe_path)) {
        std::filesystem::remove(pipe_path);
    }

    if (mkfifo(path.c_str(), 0666) != 0) {
        logger.error(std::format("Could not create {}: {}", path, std::strerror(errno)));
    } else {
        logger.info(std::format("Created: {}", path));
    }
}
