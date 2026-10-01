//
// Created by tarek on 10/1/26.
//

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

    std::string get_path() const;

    void write(const std::vector<uint8_t> &data, const std::string &extension);

    void clear() const;

private:
    std::string path;
    Logger logger{"SLIDESHOW"};
};

#endif //SLIDESHOW_FILE_H
