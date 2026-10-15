#ifndef SPI_FILE_H
#define SPI_FILE_H

#include <cstdint>
#include <string>
#include <vector>
#include "logger.h"

class SPIFile {
public:
    explicit SPIFile(int tuner_id);

    ~SPIFile();

    std::string write(uint16_t transport_id, const std::vector<uint8_t> &data, const std::string &extension);

    void remove(const std::string &path) const;

    void clear() const;

private:
    std::string directory;
    Logger logger{"SPI-FILE"};
};

#endif //SPI_FILE_H
