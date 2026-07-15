#pragma once

#include <optional>

#include <QSize>
#include <QString>
#include <QStringList>

namespace utms {

struct YoloModelConfig {
    QString model_path;
    QStringList class_names;
    QSize input_size;
    float confidence_threshold = 0.0F;
    float nms_threshold = 0.0F;
    bool letterbox = true;
    bool swap_rb = true;
    bool normalize = true;
    float normalize_scale = 1.0F;

    static std::optional<YoloModelConfig> read(const QString &model_directory, QString *error = nullptr);
};

} // namespace utms
