#include "media/YoloPostProcessor.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace utms {
namespace {

struct TensorLayout {
    int attribute_count = 0;
    int candidate_count = 0;
    bool attributes_first = false;
};

std::optional<TensorLayout> parseLayout(const std::vector<int64_t> &shape, size_t value_count,
                                        int expected_attribute_count)
{
    if (shape.size() != 3 || shape.at(0) != 1 || shape.at(1) <= 0 || shape.at(2) <= 0) {
        return std::nullopt;
    }
    const int64_t dimension_one = shape.at(1);
    const int64_t dimension_two = shape.at(2);
    if (dimension_one > std::numeric_limits<int>::max() || dimension_two > std::numeric_limits<int>::max() ||
        static_cast<size_t>(dimension_one * dimension_two) != value_count) {
        return std::nullopt;
    }

    if (dimension_one == expected_attribute_count || dimension_one == 6) {
        return TensorLayout{static_cast<int>(dimension_one), static_cast<int>(dimension_two), true};
    }
    if (dimension_two == expected_attribute_count || dimension_two == 6) {
        return TensorLayout{static_cast<int>(dimension_two), static_cast<int>(dimension_one), false};
    }
    if (dimension_one <= dimension_two) {
        return TensorLayout{static_cast<int>(dimension_one), static_cast<int>(dimension_two), true};
    }
    return TensorLayout{static_cast<int>(dimension_two), static_cast<int>(dimension_one), false};
}

float valueAt(const std::vector<float> &output, const TensorLayout &layout, int candidate_index, int attribute_index)
{
    if (layout.attributes_first) {
        return output.at(static_cast<size_t>(attribute_index * layout.candidate_count + candidate_index));
    }
    return output.at(static_cast<size_t>(candidate_index * layout.attribute_count + attribute_index));
}

QRectF mapToSource(const QRectF &model_box, const YoloModelConfig &config, const QSize &source_size)
{
    if (config.letterbox) {
        const qreal scale = std::min(static_cast<qreal>(config.input_size.width()) / source_size.width(),
                                     static_cast<qreal>(config.input_size.height()) / source_size.height());
        const qreal padding_x = (config.input_size.width() - source_size.width() * scale) / 2.0;
        const qreal padding_y = (config.input_size.height() - source_size.height() * scale) / 2.0;
        return QRectF((model_box.x() - padding_x) / scale, (model_box.y() - padding_y) / scale,
                      model_box.width() / scale, model_box.height() / scale)
            .intersected(QRectF(QPointF(), source_size));
    }

    const qreal scale_x = static_cast<qreal>(source_size.width()) / config.input_size.width();
    const qreal scale_y = static_cast<qreal>(source_size.height()) / config.input_size.height();
    return QRectF(model_box.x() * scale_x, model_box.y() * scale_y, model_box.width() * scale_x,
                  model_box.height() * scale_y)
        .intersected(QRectF(QPointF(), source_size));
}

qreal intersectionOverUnion(const QRectF &first, const QRectF &second)
{
    const QRectF intersection = first.intersected(second);
    if (intersection.isEmpty()) {
        return 0.0;
    }
    const qreal union_area = first.width() * first.height() + second.width() * second.height() -
                             intersection.width() * intersection.height();
    return union_area > 0.0 ? intersection.width() * intersection.height() / union_area : 0.0;
}

QVector<VideoDetection> applyNms(QVector<VideoDetection> candidates, float threshold)
{
    std::sort(candidates.begin(), candidates.end(), [](const VideoDetection &first, const VideoDetection &second) {
        return first.confidence > second.confidence;
    });

    QVector<VideoDetection> selected;
    for (const VideoDetection &candidate : candidates) {
        const bool overlaps_selected = std::any_of(selected.cbegin(), selected.cend(), [&](const VideoDetection &item) {
            return item.category == candidate.category &&
                   intersectionOverUnion(item.bounding_box, candidate.bounding_box) > threshold;
        });
        if (!overlaps_selected) {
            selected.append(candidate);
        }
    }
    return selected;
}

} // namespace

QVector<VideoDetection> YoloPostProcessor::process(const std::vector<float> &output, const std::vector<int64_t> &shape,
                                                    const YoloModelConfig &config, const QSize &source_size)
{
    if (source_size.isEmpty() || config.input_size.isEmpty()) {
        return {};
    }

    const auto layout = parseLayout(shape, output.size(), 4 + config.class_names.size());
    if (!layout.has_value() || layout->attribute_count < 6) {
        return {};
    }

    QVector<VideoDetection> candidates;
    for (int candidate_index = 0; candidate_index < layout->candidate_count; ++candidate_index) {
        float confidence = 0.0F;
        int class_index = -1;
        QRectF model_box;
        if (layout->attribute_count == 6) {
            confidence = valueAt(output, *layout, candidate_index, 4);
            class_index = static_cast<int>(std::lround(valueAt(output, *layout, candidate_index, 5)));
            const float x1 = valueAt(output, *layout, candidate_index, 0);
            const float y1 = valueAt(output, *layout, candidate_index, 1);
            const float x2 = valueAt(output, *layout, candidate_index, 2);
            const float y2 = valueAt(output, *layout, candidate_index, 3);
            model_box = QRectF(QPointF(x1, y1), QPointF(x2, y2)).normalized();
        } else {
            for (int index = 4; index < layout->attribute_count; ++index) {
                const float class_confidence = valueAt(output, *layout, candidate_index, index);
                if (class_confidence > confidence) {
                    confidence = class_confidence;
                    class_index = index - 4;
                }
            }
            const float center_x = valueAt(output, *layout, candidate_index, 0);
            const float center_y = valueAt(output, *layout, candidate_index, 1);
            const float width = valueAt(output, *layout, candidate_index, 2);
            const float height = valueAt(output, *layout, candidate_index, 3);
            model_box = QRectF(center_x - width / 2.0F, center_y - height / 2.0F, width, height);
        }

        if (!std::isfinite(confidence) || confidence < config.confidence_threshold || class_index < 0 ||
            !std::isfinite(model_box.x()) || !std::isfinite(model_box.y()) || !std::isfinite(model_box.width()) ||
            !std::isfinite(model_box.height())) {
            continue;
        }

        const QRectF source_box = mapToSource(model_box, config, source_size);
        if (source_box.isEmpty()) {
            continue;
        }

        const QString class_name =
            class_index < config.class_names.size() ? config.class_names.at(class_index) : QString();
        candidates.append({source_box, videoDetectionCategoryFromClassName(class_name), confidence});
    }
    return applyNms(std::move(candidates), config.nms_threshold);
}

} // namespace utms
