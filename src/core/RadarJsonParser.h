#pragma once

#include <optional>

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QStringList>

#include "core/RadarTypes.h"

namespace utms {

struct RadarParseResult {
    std::optional<RadarFrame> frame;
    QString error;
    QStringList warnings;
};

class RadarJsonParser
{
public:
    static RadarParseResult parse(const QByteArray &payload,
                                  const QDateTime &received_at = QDateTime::currentDateTime());
};

}  // namespace utms
