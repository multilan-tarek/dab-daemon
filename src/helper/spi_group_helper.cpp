#include "spi_group_helper.h"
#include <algorithm>

bool SPIGroupHelper::matches(const std::string &id, const uint32_t short_id, const std::string &other_id, const uint32_t other_short_id) {
    if (!id.empty() && !other_id.empty()) {
        return id == other_id;
    }

    return short_id <= 0xFFFFFF && other_short_id <= 0xFFFFFF && short_id == other_short_id;
}

std::vector<SPIDecoder::Programme> SPIGroupHelper::get_programmes(const std::vector<SPIDecoder::Programme> &programmes, const std::vector<SPIDecoder::ProgrammeGroup> &groups, const std::string &id, const uint32_t short_id) {
    auto root = std::ranges::find_if(groups, [&](const auto &group) {
        return !id.empty() && group.id == id;
    });

    if (root == groups.end()) {
        root = std::ranges::find_if(groups, [&](const auto &group) {
            return matches(id, short_id, group.id, group.short_id);
        });
    }

    if (root == groups.end()) {
        return {};
    }

    std::vector<SPIDecoder::GroupMembership> selected{{root->id.empty() ? id : root->id, root->short_id}};
    std::vector<bool> visited(groups.size(), false);
    visited[static_cast<size_t>(root - groups.begin())] = true;

    for (size_t index = 0; index < selected.size(); index++) {
        for (size_t group_index = 0; group_index < groups.size(); group_index++) {
            if (visited[group_index]) {
                continue;
            }

            const auto &group = groups[group_index];
            const bool member = std::ranges::any_of(group.memberships, [&](const auto &membership) {
                return matches(membership.id, membership.short_id, selected[index].id, selected[index].short_id);
            });

            if (member) {
                visited[group_index] = true;
                selected.push_back({group.id, group.short_id});
            }
        }
    }

    std::vector<SPIDecoder::Programme> result;

    for (const auto &programme: programmes) {
        const bool member = std::ranges::any_of(programme.memberships, [&](const auto &membership) {
            return std::ranges::any_of(selected, [&](const auto &group) {
                return matches(membership.id, membership.short_id, group.id, group.short_id);
            });
        });

        if (member) {
            result.push_back(programme);
        }
    }

    return result;
}
