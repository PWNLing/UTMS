#pragma once

#include <optional>

#include <QString>

#include "history/HistoryTypes.h"

namespace utms {

class HistoryCsvExporter {
public:
    static std::optional<int> exportToFile(const HistoryQueryResult &result, const QString &output_path,
                                           QString *error_message = nullptr);
};

} // namespace utms
