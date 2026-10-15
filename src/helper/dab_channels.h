#ifndef DAB_CHANNELS_H
#define DAB_CHANNELS_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

struct DABChannel {
    std::string_view name;
    uint32_t frequency;
};

constexpr std::array DAB_CHANNELS{
    DABChannel{"5A", 174928},
    DABChannel{"5B", 176640},
    DABChannel{"5C", 178352},
    DABChannel{"5D", 180064},
    DABChannel{"6A", 181936},
    DABChannel{"6B", 183648},
    DABChannel{"6C", 185360},
    DABChannel{"6D", 187072},
    DABChannel{"7A", 188928},
    DABChannel{"7B", 190640},
    DABChannel{"7C", 192352},
    DABChannel{"7D", 194064},
    DABChannel{"8A", 195936},
    DABChannel{"8B", 197648},
    DABChannel{"8C", 199360},
    DABChannel{"8D", 201072},
    DABChannel{"9A", 202928},
    DABChannel{"9B", 204640},
    DABChannel{"9C", 206352},
    DABChannel{"9D", 208064},
    DABChannel{"10A", 209936},
    DABChannel{"10N", 210096},
    DABChannel{"10B", 211648},
    DABChannel{"10C", 213360},
    DABChannel{"10D", 215072},
    DABChannel{"11A", 216928},
    DABChannel{"11N", 217088},
    DABChannel{"11B", 218640},
    DABChannel{"11C", 220352},
    DABChannel{"11D", 222064},
    DABChannel{"12A", 223936},
    DABChannel{"12N", 224096},
    DABChannel{"12B", 225648},
    DABChannel{"12C", 227360},
    DABChannel{"12D", 229072},
    DABChannel{"13A", 230784},
    DABChannel{"13B", 232496},
    DABChannel{"13C", 234208},
    DABChannel{"13D", 235776},
    DABChannel{"13E", 237488},
    DABChannel{"13F", 239200}
};

inline std::optional<size_t> get_dab_channel_index(const uint32_t frequency) {
    for (size_t index = 0; index < DAB_CHANNELS.size(); index++) {
        if (DAB_CHANNELS[index].frequency == frequency) {
            return index;
        }
    }

    return std::nullopt;
}

inline const DABChannel *get_dab_channel(const std::string_view name) {
    for (const auto &channel: DAB_CHANNELS) {
        if (channel.name == name) {
            return &channel;
        }
    }

    return nullptr;
}

inline const DABChannel *get_dab_channel(const uint32_t frequency) {
    const auto index = get_dab_channel_index(frequency);

    if (!index.has_value()) {
        return nullptr;
    }

    return &DAB_CHANNELS[*index];
}

#endif //DAB_CHANNELS_H
