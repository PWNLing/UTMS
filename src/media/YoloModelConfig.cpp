#include "media/YoloModelConfig.h"

#include <cmath>
#include <limits>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QTextStream>

namespace utms {
namespace {

void setError(QString *error, const QString &message)
{
    if (error != nullptr) {
        *error = message;
    }
}

bool readString(const QJsonObject &object, const char *key, QString &value, QString *error)
{
    const QJsonValue json_value = object.value(QLatin1String(key));
    if (!json_value.isString() || json_value.toString().trimmed().isEmpty()) {
        setError(error, QCoreApplication::translate("YoloModelConfig", "模型配置缺少有效字段 %1")
                            .arg(QString::fromLatin1(key)));
        return false;
    }
    value = json_value.toString().trimmed();
    return true;
}

bool readPositiveInt(const QJsonObject &object, const char *key, int &value, QString *error)
{
    const QJsonValue json_value = object.value(QLatin1String(key));
    if (!json_value.isDouble()) {
        setError(error, QCoreApplication::translate("YoloModelConfig", "模型配置缺少数值字段 %1")
                            .arg(QString::fromLatin1(key)));
        return false;
    }
    const double number = json_value.toDouble();
    if (!std::isfinite(number) || number <= 0.0 || number != std::floor(number) ||
        number > static_cast<double>(std::numeric_limits<int>::max())) {
        setError(error, QCoreApplication::translate("YoloModelConfig", "模型配置字段 %1 必须是正整数")
                            .arg(QString::fromLatin1(key)));
        return false;
    }
    value = static_cast<int>(number);
    return true;
}

bool readThreshold(const QJsonObject &object, const char *key, float &value, QString *error)
{
    const QJsonValue json_value = object.value(QLatin1String(key));
    if (!json_value.isDouble()) {
        setError(error, QCoreApplication::translate("YoloModelConfig", "模型配置缺少数值字段 %1")
                            .arg(QString::fromLatin1(key)));
        return false;
    }
    const double number = json_value.toDouble();
    if (!std::isfinite(number) || number < 0.0 || number > 1.0) {
        setError(error, QCoreApplication::translate("YoloModelConfig", "模型配置字段 %1 必须在 0 到 1 之间")
                            .arg(QString::fromLatin1(key)));
        return false;
    }
    value = static_cast<float>(number);
    return true;
}

bool readPositiveFloat(const QJsonObject &object, const char *key, float &value, QString *error)
{
    const QJsonValue json_value = object.value(QLatin1String(key));
    if (!json_value.isDouble()) {
        setError(error, QCoreApplication::translate("YoloModelConfig", "模型配置缺少数值字段 %1")
                            .arg(QString::fromLatin1(key)));
        return false;
    }
    const double number = json_value.toDouble();
    if (!std::isfinite(number) || number <= 0.0) {
        setError(error, QCoreApplication::translate("YoloModelConfig", "模型配置字段 %1 必须是正数")
                            .arg(QString::fromLatin1(key)));
        return false;
    }
    value = static_cast<float>(number);
    return true;
}

bool readBool(const QJsonObject &object, const char *key, bool &value, QString *error)
{
    const QJsonValue json_value = object.value(QLatin1String(key));
    if (!json_value.isBool()) {
        setError(error, QCoreApplication::translate("YoloModelConfig", "模型配置缺少布尔字段 %1")
                            .arg(QString::fromLatin1(key)));
        return false;
    }
    value = json_value.toBool();
    return true;
}

std::optional<QString> resolveModelResource(const QDir &directory, const QString &relative_path, QString *error)
{
    if (QDir::isAbsolutePath(relative_path)) {
        setError(error, QCoreApplication::translate("YoloModelConfig", "模型资源路径必须相对于模型目录"));
        return std::nullopt;
    }

    const QString absolute_path = QDir::cleanPath(directory.absoluteFilePath(relative_path));
    const QString relative_to_directory = directory.relativeFilePath(absolute_path);
    if (relative_to_directory == QStringLiteral("..") || relative_to_directory.startsWith(QStringLiteral("../"))) {
        setError(error, QCoreApplication::translate("YoloModelConfig", "模型资源路径不能离开模型目录"));
        return std::nullopt;
    }
    if (!QFileInfo::exists(absolute_path) || !QFileInfo(absolute_path).isFile()) {
        setError(error, QCoreApplication::translate("YoloModelConfig", "模型资源不存在: %1").arg(absolute_path));
        return std::nullopt;
    }
    return absolute_path;
}

} // namespace

std::optional<YoloModelConfig> YoloModelConfig::read(const QString &model_directory, QString *error)
{
    if (error != nullptr) {
        error->clear();
    }

    const QDir directory(model_directory);
    if (!directory.exists()) {
        setError(error, QCoreApplication::translate("YoloModelConfig", "模型目录不存在: %1").arg(model_directory));
        return std::nullopt;
    }

    QFile config_file(directory.filePath(QStringLiteral("model.json")));
    if (!config_file.open(QIODevice::ReadOnly)) {
        setError(error, QCoreApplication::translate("YoloModelConfig", "无法读取模型配置: %1")
                            .arg(config_file.errorString()));
        return std::nullopt;
    }

    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(config_file.readAll(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
        setError(error, QCoreApplication::translate("YoloModelConfig", "模型配置 JSON 无效: %1")
                            .arg(parse_error.errorString()));
        return std::nullopt;
    }

    const QJsonObject object = document.object();
    QString model_family;
    QString task;
    QString model_file_name;
    QString classes_file_name;
    YoloModelConfig config;
    int input_width = 0;
    int input_height = 0;
    if (!readString(object, "model_family", model_family, error) ||
        !readString(object, "task", task, error) ||
        !readString(object, "model_file", model_file_name, error) ||
        !readString(object, "classes_file", classes_file_name, error) ||
        !readPositiveInt(object, "input_width", input_width, error) ||
        !readPositiveInt(object, "input_height", input_height, error) ||
        !readThreshold(object, "confidence_threshold", config.confidence_threshold, error) ||
        !readThreshold(object, "nms_threshold", config.nms_threshold, error) ||
        !readBool(object, "letterbox", config.letterbox, error) ||
        !readBool(object, "swap_rb", config.swap_rb, error) ||
        !readBool(object, "normalize", config.normalize, error) ||
        !readPositiveFloat(object, "normalize_scale", config.normalize_scale, error)) {
        return std::nullopt;
    }

    if (model_family.compare(QStringLiteral("yolo26"), Qt::CaseInsensitive) != 0 ||
        task.compare(QStringLiteral("detect"), Qt::CaseInsensitive) != 0) {
        setError(error, QCoreApplication::translate("YoloModelConfig", "仅支持 yolo26 检测模型配置"));
        return std::nullopt;
    }

    const auto model_path = resolveModelResource(directory, model_file_name, error);
    const auto classes_path = resolveModelResource(directory, classes_file_name, error);
    if (!model_path.has_value() || !classes_path.has_value()) {
        return std::nullopt;
    }

    QFile classes_file(*classes_path);
    if (!classes_file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        setError(error, QCoreApplication::translate("YoloModelConfig", "无法读取模型类别文件: %1")
                            .arg(classes_file.errorString()));
        return std::nullopt;
    }

    QTextStream class_stream(&classes_file);
    while (!class_stream.atEnd()) {
        const QString class_name = class_stream.readLine().trimmed();
        if (!class_name.isEmpty()) {
            config.class_names.append(class_name);
        }
    }
    if (config.class_names.isEmpty()) {
        setError(error, QCoreApplication::translate("YoloModelConfig", "模型类别文件为空"));
        return std::nullopt;
    }

    config.model_path = *model_path;
    config.input_size = QSize(input_width, input_height);
    return config;
}

} // namespace utms
