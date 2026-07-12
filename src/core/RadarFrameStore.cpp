#include "core/RadarFrameStore.h"

#include <QSet>

namespace utms {

RadarFrame RadarFrameStore::replace(const RadarFrame &frame)
{
    RadarFrame next_frame = frame;
    QSet<qint64> current_track_ids;
    current_track_ids.reserve(next_frame.tracks.size());

    for (TrackData &track : next_frame.tracks) {
        current_track_ids.insert(track.track_id);
        const auto first_seen = first_seen_by_track_id_.constFind(track.track_id);
        if (first_seen != first_seen_by_track_id_.cend()) {
            track.first_seen_at = first_seen.value();
        } else {
            track.first_seen_at = next_frame.received_at;
            first_seen_by_track_id_.insert(track.track_id, track.first_seen_at);
        }
    }

    for (auto iterator = first_seen_by_track_id_.begin(); iterator != first_seen_by_track_id_.end();) {
        if (!current_track_ids.contains(iterator.key())) {
            iterator = first_seen_by_track_id_.erase(iterator);
        } else {
            ++iterator;
        }
    }

    return next_frame;
}

void RadarFrameStore::clear()
{
    first_seen_by_track_id_.clear();
}

}  // namespace utms
