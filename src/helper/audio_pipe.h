#ifndef AUDIO_PIPE_H
#define AUDIO_PIPE_H

#include <cstdint>
#include <string>
#include <vector>
#include "logger.h"

class AudioPipe {
public:
    explicit AudioPipe(int tuner_id);

    ~AudioPipe();

    std::string get_path() const;

    void write(const std::vector<uint8_t> &data);

    void discard_buffered();

    void close();

private:
    std::string path;
    int file_descriptor = -1;
    Logger logger{"AUDIO"};

    void create();
};

#endif //AUDIO_PIPE_H
