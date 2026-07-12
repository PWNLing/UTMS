#pragma once

#include <QHash>

#include "core/RadarTypes.h"

namespace utms {

class RadarFrameStore
{
public:
    RadarFrame replace(const RadarFrame &frame);
    void clear();

private:
    QHash<qint64, QDateTime> first_seen_by_track_id_;
};

}  // namespace utms
