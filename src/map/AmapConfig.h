#pragma once

#include <optional>

#include <QString>

namespace utms
{

struct AmapConfig
{
    QString key;
    QString security_code;
};

struct AmapConfigResult
{
    std::optional<AmapConfig> config;
    QString error;
};

AmapConfigResult loadAmapConfig(const QString &config_path);

} // namespace utms
