#include "ensemble_cache.h"
#include "dab_channels.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <mutex>

namespace {
constexpr uintmax_t MAX_CACHE_BYTES = 4 * 1024 * 1024;
constexpr size_t MAX_SERVICES = 1024;
constexpr size_t MAX_STRINGS = 64;

template<typename T>
bool read_number(std::istream &input, T &value) {
    uint64_t number = 0;
    if (!(input >> number) || number > std::numeric_limits<T>::max()) {
        return false;
    }
    value = static_cast<T>(number);
    return true;
}

bool read_strings(std::istream &input, std::vector<std::string> &values) {
    size_t count = 0;
    if (!(input >> count) || count > MAX_STRINGS) {
        return false;
    }
    for (size_t index = 0; index < count; index++) {
        std::string value;
        if (!(input >> std::quoted(value))) {
            return false;
        }
        values.push_back(std::move(value));
    }
    return true;
}

void write_strings(std::ostream &output, const std::vector<std::string> &values) {
    output << values.size();
    for (const auto &value: values) {
        output << ' ' << std::quoted(value);
    }
    output << '\n';
}

std::vector<EnsembleCache::Ensemble> read_cache(const std::filesystem::path &path) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size > MAX_CACHE_BYTES) {
        return {};
    }
    std::ifstream input(path);
    std::string magic;
    size_t count = 0;
    if (!(input >> magic >> count) || magic != "DAB-ENSEMBLES-1" || count > DAB_CHANNELS.size()) {
        return {};
    }
    std::vector<EnsembleCache::Ensemble> result;
    for (size_t index = 0; index < count; index++) {
        EnsembleCache::Ensemble ensemble;
        size_t service_count = 0;
        if (!read_number(input, ensemble.frequency) || !read_number(input, ensemble.ensemble_id) || !read_number(input, ensemble.ecc) ||
            !(input >> std::quoted(ensemble.channel) >> std::quoted(ensemble.label) >> std::quoted(ensemble.short_label) >> service_count) ||
            ensemble.ensemble_id == 0 || !get_dab_channel_index(ensemble.frequency) || service_count > MAX_SERVICES) {
            return {};
        }
        if (std::ranges::any_of(result, [&](const auto &entry) { return entry.frequency == ensemble.frequency; })) {
            return {};
        }
        for (size_t service_index = 0; service_index < service_count; service_index++) {
            FICDecoder::Service service;
            if (!read_number(input, service.id) || !read_number(input, service.ecc) || !read_number(input, service.subchannel.id) ||
                !read_number(input, service.subchannel.start) || !read_number(input, service.subchannel.size) || !read_number(input, service.subchannel.bitrate) ||
                !read_number(input, service.dab_plus) || !(input >> std::quoted(service.label) >> std::quoted(service.short_label) >>
                std::quoted(service.subchannel.protection) >> std::quoted(service.language) >> std::quoted(service.pty)) ||
                !read_strings(input, service.announcement_support) || !read_strings(input, service.announcements) ||
                service.id == 0 || service.label.empty() || service.subchannel.id >= 64 || service.subchannel.bitrate == 0) {
                return {};
            }
            if (std::ranges::any_of(ensemble.services, [&](const auto &entry) { return entry.id == service.id; })) {
                return {};
            }
            ensemble.services.push_back(std::move(service));
        }
        result.push_back(std::move(ensemble));
    }
    input >> std::ws;
    return input.eof() ? result : std::vector<EnsembleCache::Ensemble>{};
}

bool write_cache(const std::filesystem::path &path, const std::vector<EnsembleCache::Ensemble> &ensembles) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        return false;
    }
    const auto temporary = path.string() + ".tmp";
    std::ofstream output(temporary, std::ios::trunc);
    output << "DAB-ENSEMBLES-1 " << ensembles.size() << '\n';
    for (const auto &ensemble: ensembles) {
        output << ensemble.frequency << ' ' << ensemble.ensemble_id << ' ' << unsigned(ensemble.ecc) << ' ' <<
            std::quoted(ensemble.channel) << ' ' << std::quoted(ensemble.label) << ' ' << std::quoted(ensemble.short_label) << ' ' << ensemble.services.size() << '\n';
        for (const auto &service: ensemble.services) {
            output << service.id << ' ' << unsigned(service.ecc) << ' ' << unsigned(service.subchannel.id) << ' ' << service.subchannel.start << ' ' <<
                service.subchannel.size << ' ' << service.subchannel.bitrate << ' ' << service.dab_plus << ' ' << std::quoted(service.label) << ' ' <<
                std::quoted(service.short_label) << ' ' << std::quoted(service.subchannel.protection) << ' ' << std::quoted(service.language) << ' ' << std::quoted(service.pty) << '\n';
            write_strings(output, service.announcement_support);
            write_strings(output, service.announcements);
        }
    }
    output.close();
    if (output) {
        std::filesystem::rename(temporary, path, error);
        if (!error) {
            return true;
        }
    }
    std::filesystem::remove(temporary, error);
    return false;
}
}

struct EnsembleCache::State {
    std::mutex mutex;
    std::filesystem::path path;
    std::vector<Ensemble> ensembles;
    uint64_t revision = 0;
};

EnsembleCache::EnsembleCache(const int tuner_id, const std::string &directory) {
    logger.set_id(tuner_id);
    std::string cache_directory = directory;
    if (cache_directory.empty()) {
        const char *cache_home = std::getenv("XDG_CACHE_HOME");
        const char *user_home = std::getenv("HOME");
        if (cache_home && std::filesystem::path(cache_home).is_absolute()) {
            cache_directory = (std::filesystem::path(cache_home) / "dab-daemon").string();
        } else if (user_home && std::filesystem::path(user_home).is_absolute()) {
            cache_directory = (std::filesystem::path(user_home) / ".cache/dab-daemon").string();
        }
    }
    static std::mutex registry_mutex;
    static std::map<std::string, std::weak_ptr<State>> registry;
    const std::lock_guard lock(registry_mutex);
    state = registry[cache_directory].lock();
    if (!state) {
        state = std::make_shared<State>();
        if (!cache_directory.empty()) {
            state->path = std::filesystem::path(cache_directory) / "ensembles.cache";
            state->ensembles = read_cache(state->path);
            std::ranges::sort(state->ensembles, {}, &Ensemble::frequency);
        }
        registry[cache_directory] = state;
    }
}

std::vector<EnsembleCache::Ensemble> EnsembleCache::get_ensembles() const {
    const std::lock_guard lock(state->mutex);
    return state->ensembles;
}

uint64_t EnsembleCache::get_revision() const {
    const std::lock_guard lock(state->mutex);
    return state->revision;
}

bool EnsembleCache::update(Ensemble ensemble, const bool services_ready) {
    if (ensemble.ensemble_id == 0 || !get_dab_channel_index(ensemble.frequency) || ensemble.services.size() > MAX_SERVICES) {
        return false;
    }
    if (services_ready && (ensemble.ecc == 0 || std::ranges::any_of(ensemble.services, [](const FICDecoder::Service &service) {
        return service.id == 0 || service.label.empty() || service.subchannel.id >= 64 || service.subchannel.bitrate == 0;
    }))) {
        return false;
    }
    if (!services_ready) {
        ensemble.services.clear();
    }
    const std::lock_guard lock(state->mutex);
    auto existing = std::ranges::find(state->ensembles, ensemble.frequency, &Ensemble::frequency);
    if (existing != state->ensembles.end()) {
        const bool same_identity = existing->ensemble_id == ensemble.ensemble_id && (existing->ecc == ensemble.ecc || ensemble.ecc == 0);
        if (same_identity) {
            if (ensemble.ecc == 0) {
                ensemble.ecc = existing->ecc;
            }
            if (ensemble.label.empty()) {
                ensemble.label = existing->label;
                ensemble.short_label = existing->short_label;
            }
            if (!services_ready) {
                ensemble.services = existing->services;
            }
        }
        if (*existing == ensemble) {
            return false;
        }
        *existing = std::move(ensemble);
    } else {
        state->ensembles.push_back(std::move(ensemble));
        std::ranges::sort(state->ensembles, {}, &Ensemble::frequency);
    }
    state->revision++;
    if (!state->path.empty() && !write_cache(state->path, state->ensembles)) {
        logger.warning("Could not save ensemble database");
    }
    return true;
}
