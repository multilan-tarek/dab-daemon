#ifndef SPI_GROUP_HELPER_H
#define SPI_GROUP_HELPER_H

#include "../decoder/spi_decoder.h"

class SPIGroupHelper {
public:
    static bool matches(const std::string &id, uint32_t short_id, const std::string &other_id, uint32_t other_short_id);

    static std::vector<SPIDecoder::Programme> get_programmes(const std::vector<SPIDecoder::Programme> &programmes, const std::vector<SPIDecoder::ProgrammeGroup> &groups, const std::string &id, uint32_t short_id);
};

#endif //SPI_GROUP_HELPER_H
