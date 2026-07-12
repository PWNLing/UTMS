#include "map/AmapConfig.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

namespace utms
{

AmapConfigResult loadAmapConfig(const QString &config_path)
{
    QFile config_file(config_path);
    if (!config_file.open(QIODevice::ReadOnly))
    {
        AmapConfigResult result;
        result.error = QCoreApplication::translate("AmapConfig", "高德地图配置文件缺失: %1").arg(config_path);
        return result;
    }

    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(config_file.readAll(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject())
    {
        AmapConfigResult result;
        result.error =
            QCoreApplication::translate("AmapConfig", "高德地图配置格式无效: %1").arg(parse_error.errorString());
        return result;
    }

    const QJsonObject object = document.object();
    const QString key = object.value(QStringLiteral("key")).toString().trimmed();
    const QString security_code = object.value(QStringLiteral("securityCode")).toString().trimmed();
    if (key.isEmpty() || security_code.isEmpty())
    {
        AmapConfigResult result;
        result.error =
            QCoreApplication::translate("AmapConfig", "高德地图 Key 或安全密钥为空，请填写 config/amap.json");
        return result;
    }

    AmapConfigResult result;
    result.config = AmapConfig{key, security_code};
    return result;
}

} // namespace utms
