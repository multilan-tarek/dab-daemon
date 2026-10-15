#ifndef SLIDESHOW_FILE_H
#define SLIDESHOW_FILE_H

#include <cstdint>
#include <string>
#include <vector>
#include "logger.h"

class SlideShowFile {
public:
    explicit SlideShowFile(int tuner_id);

    ~SlideShowFile();

    std::string write(uint16_t transport_id, const std::vector<uint8_t> &data, const std::string &extension);

    void remove(const std::string &path) const;

    void clear() const;

private:
    std::string directory;
    uint64_t sequence = 0;
    Logger logger{"SLIDESHOW"};
};

#endif //SLIDESHOW_FILE_H
