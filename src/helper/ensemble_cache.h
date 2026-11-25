#ifndef ENSEMBLE_CACHE_H
#define ENSEMBLE_CACHE_H

#include <memory>
#include <string>
#include <vector>
#include "../decoder/fic_decoder.h"
#include "logger.h"

class EnsembleCache {
public:
    struct Ensemble {
        std::string channel;
        uint32_t frequency = 0;
        uint16_t ensemble_id = 0;
        uint8_t ecc = 0;
        std::string label;
        std::string short_label;
        std::vector<FICDecoder::Service> services;

        bool operator==(const Ensemble &other) const = default;
    };

    explicit EnsembleCache(int tuner_id, const std::string &directory = "");

    std::vector<Ensemble> get_ensembles() const;

    uint64_t get_revision() const;

    bool update(Ensemble ensemble, bool services_ready);

private:
    struct State;
    std::shared_ptr<State> state;
    Logger logger{"ENSEMBLES"};
};

#endif //ENSEMBLE_CACHE_H
