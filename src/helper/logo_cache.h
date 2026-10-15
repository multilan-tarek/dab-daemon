#ifndef LOGO_CACHE_H
#define LOGO_CACHE_H

#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include "logger.h"

class LogoCache {
public:
    struct Logo {
        uint32_t service_id = 0;
        uint8_t ecc = 0;
        uint16_t ensemble_id = 0;
        uint16_t size = 0;
        std::string path;
        std::string mime_type;
        int64_t modified = 0;

        bool operator==(const Logo &other) const = default;
    };

    explicit LogoCache(int tuner_id, const std::string &directory = "");

    std::vector<Logo> load(uint8_t ecc, uint16_t ensemble_id) const;

    std::optional<Logo> store(uint32_t service_id, uint8_t ecc, uint16_t ensemble_id, const std::string &path) const;

private:
    std::string directory;
    Logger logger{"LOGO-CACHE"};

    void prune() const;
};

#endif //LOGO_CACHE_H
