#include "history/HistorySamplingPolicy.h"

namespace utms {
std::optional<int> HistorySamplingPolicy::intervalMs(HistorySamplingRate sampling_rate) const {
    switch (sampling_rate) {
    case HistorySamplingRate::kEveryFrame:
        return 0;
    case HistorySamplingRate::kOneFps:
        return 1'000;
    case HistorySamplingRate::kTwoFps:
        return 500;
    case HistorySamplingRate::kFiveFps:
        return 200;
    }
    return std::nullopt;
}

} // namespace utms
