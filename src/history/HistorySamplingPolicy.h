#pragma once

#include <optional>

#include "history/HistoryTypes.h"

namespace utms {

class HistorySamplingPolicy {
public:
    std::optional<int> intervalMs(HistorySamplingRate sampling_rate) const;
};

} // namespace utms
