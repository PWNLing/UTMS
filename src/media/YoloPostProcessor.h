#pragma once

#include <cstdint>
#include <vector>

#include <QSize>
#include <QVector>

#include "media/VideoDetection.h"
#include "media/YoloModelConfig.h"

namespace utms {

class YoloPostProcessor {
public:
    // 同时支持 YOLO26 原始输出与端到端 [x1, y1, x2, y2, confidence, class] 输出。
    static QVector<VideoDetection> process(const std::vector<float> &output, const std::vector<int64_t> &shape,
                                            const YoloModelConfig &config, const QSize &source_size);
};

} // namespace utms
