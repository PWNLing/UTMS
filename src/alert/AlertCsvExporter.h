#pragma once

#include <optional>

#include <QString>

#include "alert/AlertTypes.h"

namespace utms {

class AlertCsvExporter {
public:
    static std::optional<int> exportToFile(const AlertQueryResult &result, const QString &output_path,
                                           QString *error_message = nullptr);
};

} // namespace utms
