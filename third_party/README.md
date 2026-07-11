下面按 **Windows 11 + Qt 6 + CMake + MSVC 2022 x64** 来配置。这套组合最省事。ONNX Runtime 官方 Windows 构建以 Visual Studio 2022 为默认生成器，当前也不再支持 32 位 Windows 构建，因此建议 Qt Creator 选择 **MSVC 2022 64-bit Kit**，不要优先使用 MinGW。([ONNX Runtime](https://onnxruntime.ai/docs/build/inferencing.html?utm_source=chatgpt.com))

## 一、先弄清三者分别是什么

你的运行链路一般是：

```text
摄像头 / 视频 / RTSP
        │
        ▼
FFmpeg 解封装、解码
        │
        ▼
RGB 图像 / 原始像素
        │
        ▼
缩放、LetterBox、归一化
        │
        ▼
ONNX Runtime 加载 YOLO 的 .onnx 模型
        │
        ▼
YOLO 后处理：置信度过滤、坐标还原、NMS
        │
        ▼
Qt 绘制检测框并显示
```

其中：

- **FFmpeg**：负责读取摄像头、视频、RTSP，解码 H.264/H.265 等。
- **ONNX Runtime**：负责执行神经网络模型推理。
- **YOLO**：不是一个必须放进 Qt 项目的 C++库。部署时主要需要：
  - `best.onnx`
  - 类别名称文件
  - 你自己编写的预处理和后处理代码

因此严格来说，`third_party` 里放 FFmpeg 和 ONNX Runtime；YOLO 模型建议放到 `models` 或 `assets/models`。

------

# 二、推荐项目目录

```text
MyQtProject/
├── CMakeLists.txt
│
├── src/
│   ├── main.cpp
│   ├── media/
│   │   ├── FfmpegDecoder.h
│   │   └── FfmpegDecoder.cpp
│   └── inference/
│       ├── YoloDetector.h
│       └── YoloDetector.cpp
│
├── third_party/
│   ├── ffmpeg/
│   │   ├── include/
│   │   │   ├── libavcodec/
│   │   │   ├── libavformat/
│   │   │   ├── libavutil/
│   │   │   ├── libswscale/
│   │   │   ├── libswresample/
│   │   │   ├── libavdevice/       # 使用摄像头时需要
│   │   │   └── libavfilter/       # 使用滤镜时需要
│   │   │
│   │   ├── lib/
│   │   │   ├── avcodec.lib
│   │   │   ├── avformat.lib
│   │   │   ├── avutil.lib
│   │   │   ├── swscale.lib
│   │   │   ├── swresample.lib
│   │   │   ├── avdevice.lib
│   │   │   └── avfilter.lib
│   │   │
│   │   ├── bin/
│   │   │   ├── avcodec-*.dll
│   │   │   ├── avformat-*.dll
│   │   │   ├── avutil-*.dll
│   │   │   ├── swscale-*.dll
│   │   │   ├── swresample-*.dll
│   │   │   └── 其他依赖的 DLL
│   │   │
│   │   ├── LICENSE.txt
│   │   └── VERSION.txt
│   │
│   └── onnxruntime/
│       ├── include/
│       │   ├── onnxruntime_c_api.h
│       │   ├── onnxruntime_cxx_api.h
│       │   ├── onnxruntime_cxx_inline.h
│       │   └── 其他官方头文件
│       │
│       ├── lib/
│       │   └── onnxruntime.lib
│       │
│       ├── bin/
│       │   └── onnxruntime.dll
│       │
│       ├── LICENSE
│       └── VERSION.txt
│
├── models/
│   └── yolo/
│       ├── best.onnx
│       ├── classes.txt
│       └── model.json
│
└── resources/
```

------

# 三、下载安装 FFmpeg

## 1. 应该下载哪个包

FFmpeg 官方只直接提供源代码，同时在下载页面列出了 Windows 预编译版本来源，例如 gyan.dev 和 BtbN。([FFmpeg](https://www.ffmpeg.org/download.html))

对于 Qt C++ 调用 FFmpeg API，你不能只下载普通的 `ffmpeg.exe` 包，因为你需要：

```text
include    头文件
lib        链接时使用的 .lib
bin        运行时使用的 .dll
```

建议下载：

```text
ffmpeg-release-full-shared.7z
```

重点是名字中的：

```text
shared
```

Gyan 的 `full-shared` 包包含开发文件，当前发布页显示的稳定版本为 FFmpeg 8.1.2。([Gyan](https://www.gyan.dev/ffmpeg/builds/))

不要下载下面这种仅包含命令行程序的包：

```text
ffmpeg-release-essentials.zip
ffmpeg-release-full.7z
```

这些通常适合直接使用 `ffmpeg.exe`，不一定包含你需要的开发头文件和导入库。

------

## 2. 解压后保留什么

【注】删除 bin 目录下的所有 exe，还可以删除编码器预设目录 presets 

解压后通常能看到：

```text
ffmpeg-8.x-full_build-shared/
├── bin/
├── include/
├── lib/
├── presets/
├── LICENSE
└── README.txt
```

将它们整理成：

```text
third_party/ffmpeg/
├── include/
├── lib/
├── bin/
├── LICENSE.txt
└── VERSION.txt
```

建议完整保留：

```text
include/
lib/
bin/中的所有 DLL
LICENSE
README
```

因为 FFmpeg 的某些 DLL 之间还有依赖关系。最稳妥的办法不是只挑五个 DLL，而是暂时保留 `bin` 中所有 `.dll`。

------

## 3. 哪些 FFmpeg 库是必要的

### 视频文件、RTSP、网络视频

```text
avformat
avcodec
avutil
swscale
```

作用分别是：

| 库         | 作用                            |
| ---------- | ------------------------------- |
| `avformat` | 打开 MP4、RTSP、网络流，解封装  |
| `avcodec`  | H.264、H.265、MJPEG 等解码      |
| `avutil`   | FFmpeg 公共工具、内存、图像格式 |
| `swscale`  | YUV、RGB 转换和图像缩放         |

### 还需要音频

增加：

```text
swresample
```

### FFmpeg 直接采集 Windows 摄像头

增加：

```text
avdevice
```

### 使用 FFmpeg 滤镜

增加：

```text
avfilter
```

所以你的上位机推荐先链接：

```text
avformat
avcodec
avutil
swscale
swresample
avdevice
```

------

## 4. 是否需要 ffmpeg.exe

如果你的代码直接调用：

```cpp
avformat_open_input();
avcodec_send_packet();
avcodec_receive_frame();
```

那么一般不需要：

```text
ffmpeg.exe
ffprobe.exe
ffplay.exe
```

只有在代码中通过 `QProcess` 执行 FFmpeg 命令时，才需要把 `ffmpeg.exe` 放在程序旁边。

------

# 四、下载安装 ONNX Runtime

## 1. 建议先使用 CPU 版

初次开发建议先用 CPU 版，等推理流程完全正确后，再切换 CUDA GPU 版。

ONNX Runtime 官方 C++ 文档说明，每个正式版本的 GitHub Release 都提供 `.zip` 和 `.tgz` 预编译包。当前发布页显示正式版本为 1.27.1。([ONNX Runtime](https://onnxruntime.ai/docs/get-started/with-cpp.html))

找到这一行：

> .zip and .tgz files are also included as assets in each [Github release](https://github.com/microsoft/onnxruntime/releases).

点击跳转到官方 GitHub Releases 中选择文件名类似：

```text
onnxruntime-win-x64-1.27.1.zip
```

注意选择：

```text
win
x64
```

不要选：

```text
win-arm64
linux
osx
```

------

## 2. 解压后保留什么

【注】建议直接拷贝过去，保留所有文件

官方压缩包大致是：

```text
onnxruntime-win-x64-x.x.x/
├── include/
├── lib/
├── LICENSE
├── Privacy.md
└── README.md
```

其中 `lib` 目录通常同时包含：

```text
onnxruntime.lib
onnxruntime.dll
```

你可以重新整理为：

```text
third_party/onnxruntime/
├── include/
├── lib/
│   └── onnxruntime.lib
├── bin/
│   └── onnxruntime.dll
├── LICENSE
└── VERSION.txt
```

也就是：

```text
原目录/include/*       → third_party/onnxruntime/include/
原目录/lib/*.lib       → third_party/onnxruntime/lib/
原目录/lib/*.dll       → third_party/onnxruntime/bin/
```

### 不要只复制两个头文件

虽然最常使用的是：

```cpp
onnxruntime_cxx_api.h
onnxruntime_c_api.h
```

但它们还会包含其他头文件，所以应当把官方的整个 `include` 目录复制进来。

------

# 五、GPU 版 ONNX Runtime 怎么处理

如果电脑有 NVIDIA 显卡，可以下载名称类似：

```text
onnxruntime-win-x64-gpu-x.x.x.zip
```

GPU 版除了：

```text
onnxruntime.dll
```

一般还会包含：

```text
onnxruntime_providers_shared.dll
onnxruntime_providers_cuda.dll
```

这些都要放到最终 `.exe` 所在目录。

项目一般仍然只链接：

```text
onnxruntime.lib
```

CUDA Provider DLL 会在运行时加载。

## CUDA 版本必须匹配

ONNX Runtime 的 GPU 包与 CUDA、cuDNN 有明确兼容关系。官方文档强调：

- CUDA 12 构建对应 CUDA 12.x。
- cuDNN 8 和 cuDNN 9 不能混用。
- 从 ONNX Runtime 1.19 开始，默认 GPU 包转向 CUDA 12.x。
- ONNX Runtime 1.25 已将最低 CUDA 版本提高到 12.0。([ONNX Runtime](https://onnxruntime.ai/docs/execution-providers/CUDA-ExecutionProvider.html))

因此不要随便组合：

```text
ONNX Runtime CUDA 12
+
CUDA 11.8
+
cuDNN 8
```

这种组合很容易出现：

```text
LoadLibrary failed
onnxruntime_providers_cuda.dll could not be loaded
CUDAExecutionProvider is not available
```

第一阶段建议：

```text
onnxruntime-win-x64 CPU 版
```

等 CPU 推理成功后再升级 GPU。

------

# 六、YOLO 模型怎么下载或导出

## 情况一：你有自己训练的 best.pt

安装 Python 环境：

```powershell
python -m venv .venv
.venv\Scripts\activate

python -m pip install --upgrade pip
pip install ultralytics onnx
```

导出：

```python
from ultralytics import YOLO

model = YOLO("best.pt")

model.export(
    format="onnx",
    imgsz=640,
    dynamic=False
)
```

运行后通常得到：

```text
best.onnx
```

Ultralytics 官方支持通过 `model.export(format="onnx")` 将训练后的 YOLO 模型导出为 ONNX。([Ultralytics Docs](https://docs.ultralytics.com/modes/export))

将其放入：

```text
models/yolo/best.onnx
```

------

## 情况二：你还没有训练模型

可以先使用官方预训练模型，例如：

```python
from ultralytics import YOLO

model = YOLO("yolo11n.pt")
model.export(format="onnx", imgsz=640, dynamic=False)
```

实际模型名称应以你使用的 YOLO 系列为准，例如：

```text
YOLOv5
YOLOv8
YOLO11
YOLO26
```

这些模型的输出张量格式可能不同。因此不能随便找一份“YOLO后处理代码”就直接用于所有版本。

------

# 七、YOLO 目录到底放哪些文件

推荐：

```text
models/yolo/
├── best.onnx
├── classes.txt
└── model.json
```

## classes.txt

一行一个类别：

```text
person
car
truck
bus
bicycle
```

类别顺序必须与训练时的类别编号一致：

```text
类别 0 → person
类别 1 → car
类别 2 → truck
```

## model.json

可以保存模型相关参数：

```json
{
    "model": "best.onnx",
    "input_width": 640,
    "input_height": 640,
    "confidence_threshold": 0.25,
    "nms_threshold": 0.45,
    "letterbox": true,
    "rgb": true,
    "normalize_scale": 0.00392156862745098
}
```

这样以后修改模型尺寸、置信度阈值时，不需要重新编译程序。

## 部署时不需要放

通常不需要：

```text
best.pt
训练数据集
train/images
train/labels
runs/
weights/
ultralytics Python源码
PyTorch
Python虚拟环境
```

Qt C++程序部署时主要需要：

```text
best.onnx
classes.txt
model.json
onnxruntime.dll
```

------

# 八、CMakeLists.txt 配置

假设可执行程序目标名为：

```cmake
MyQtProject
```

可以这样配置：

```cmake
cmake_minimum_required(VERSION 3.21)

project(MyQtProject LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
)

qt_standard_project_setup()

set(APP_TARGET MyQtProject)

qt_add_executable(${APP_TARGET}
    src/main.cpp

    src/media/FfmpegDecoder.h
    src/media/FfmpegDecoder.cpp

    src/inference/YoloDetector.h
    src/inference/YoloDetector.cpp
)

target_link_libraries(${APP_TARGET}
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
)

# =========================================================
# 第三方库目录
# =========================================================

set(THIRD_PARTY_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party"
)

set(FFMPEG_ROOT
    "${THIRD_PARTY_DIR}/ffmpeg"
)

set(ONNXRUNTIME_ROOT
    "${THIRD_PARTY_DIR}/onnxruntime"
)

# =========================================================
# 头文件
# =========================================================

target_include_directories(${APP_TARGET}
    PRIVATE
        "${FFMPEG_ROOT}/include"
        "${ONNXRUNTIME_ROOT}/include"
)

# =========================================================
# FFmpeg
# Windows + MSVC
# =========================================================

target_link_libraries(${APP_TARGET}
    PRIVATE
        "${FFMPEG_ROOT}/lib/avformat.lib"
        "${FFMPEG_ROOT}/lib/avcodec.lib"
        "${FFMPEG_ROOT}/lib/avutil.lib"
        "${FFMPEG_ROOT}/lib/swscale.lib"
        "${FFMPEG_ROOT}/lib/swresample.lib"
        "${FFMPEG_ROOT}/lib/avdevice.lib"
)

# 使用 FFmpeg 滤镜时再添加
if(EXISTS "${FFMPEG_ROOT}/lib/avfilter.lib")
    target_link_libraries(${APP_TARGET}
        PRIVATE
            "${FFMPEG_ROOT}/lib/avfilter.lib"
    )
endif()

# =========================================================
# ONNX Runtime
# =========================================================

target_link_libraries(${APP_TARGET}
    PRIVATE
        "${ONNXRUNTIME_ROOT}/lib/onnxruntime.lib"
)

# =========================================================
# 编译后复制运行时 DLL
# =========================================================

if(WIN32)
    add_custom_command(
        TARGET ${APP_TARGET}
        POST_BUILD

        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${FFMPEG_ROOT}/bin"
            "$<TARGET_FILE_DIR:${APP_TARGET}>"

        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${ONNXRUNTIME_ROOT}/bin"
            "$<TARGET_FILE_DIR:${APP_TARGET}>"

        COMMENT "Copying FFmpeg and ONNX Runtime DLL files"
    )
endif()

# =========================================================
# 复制 YOLO 模型
# =========================================================

add_custom_command(
    TARGET ${APP_TARGET}
    POST_BUILD

    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:${APP_TARGET}>/models/yolo"

    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/models/yolo/best.onnx"
        "$<TARGET_FILE_DIR:${APP_TARGET}>/models/yolo/best.onnx"

    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/models/yolo/classes.txt"
        "$<TARGET_FILE_DIR:${APP_TARGET}>/models/yolo/classes.txt"

    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/models/yolo/model.json"
        "$<TARGET_FILE_DIR:${APP_TARGET}>/models/yolo/model.json"

    COMMENT "Copying YOLO model files"
)

install(TARGETS ${APP_TARGET}
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)
```

编译后目录应该类似：

```text
build/Desktop_Qt_6_x_x_MSVC2022_64bit-Release/
├── MyQtProject.exe
├── onnxruntime.dll
├── avcodec-*.dll
├── avformat-*.dll
├── avutil-*.dll
├── swscale-*.dll
├── swresample-*.dll
├── 其他 FFmpeg DLL
└── models/
    └── yolo/
        ├── best.onnx
        ├── classes.txt
        └── model.json
```

------

# 九、代码中如何包含 FFmpeg

FFmpeg 是 C 接口，在 C++文件中建议写成：

```cpp
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}
```

否则可能出现名称修饰导致的链接问题。

简单测试：

```cpp
#include <QDebug>

extern "C" {
#include <libavutil/avutil.h>
}

void printFfmpegVersion()
{
    qDebug() << "FFmpeg version:"
             << av_version_info();
}
```

能正常打印版本，说明：

```text
头文件正确
.lib 链接正确
.dll 加载正确
```

------

# 十、代码中如何测试 ONNX Runtime

```cpp
#include <onnxruntime_cxx_api.h>

#include <QDebug>
#include <memory>

void testOnnxRuntime()
{
    static Ort::Env env(
        ORT_LOGGING_LEVEL_WARNING,
        "MyQtProject"
    );

    Ort::SessionOptions sessionOptions;

    sessionOptions.SetGraphOptimizationLevel(
        GraphOptimizationLevel::ORT_ENABLE_ALL
    );

    qDebug() << "ONNX Runtime initialized successfully";
}
```

加载模型时，在 Windows 下建议使用宽字符路径：

```cpp
#include <QCoreApplication>
#include <QDir>

Ort::Session createSession(
    Ort::Env &env,
    const Ort::SessionOptions &sessionOptions)
{
    const QString modelPath =
        QDir(QCoreApplication::applicationDirPath())
            .filePath("models/yolo/best.onnx");

#ifdef _WIN32
    const std::wstring modelPathWide =
        modelPath.toStdWString();

    return Ort::Session(
        env,
        modelPathWide.c_str(),
        sessionOptions
    );
#else
    const QByteArray modelPathUtf8 =
        modelPath.toUtf8();

    return Ort::Session(
        env,
        modelPathUtf8.constData(),
        sessionOptions
    );
#endif
}
```

------

# 十一、常见错误

## 1. 找不到 `onnxruntime_cxx_api.h`

```text
fatal error: onnxruntime_cxx_api.h: No such file
```

检查：

```text
third_party/onnxruntime/include/onnxruntime_cxx_api.h
```

以及：

```cmake
target_include_directories(...)
```

------

## 2. 找不到 `onnxruntime.lib`

```text
LNK1104: cannot open file 'onnxruntime.lib'
```

检查：

```text
third_party/onnxruntime/lib/onnxruntime.lib
```

并确认 Qt 使用的是：

```text
MSVC 2022 64-bit
```

而不是：

```text
MinGW 64-bit
```

------

## 3. 程序启动提示缺少 DLL

```text
The code execution cannot proceed because
onnxruntime.dll was not found
```

必须将：

```text
onnxruntime.dll
```

复制到：

```text
MyQtProject.exe
```

同一目录。

FFmpeg DLL 同理。

------

## 4. `avformat_open_input` 无法解析

```text
unresolved external symbol avformat_open_input
```

说明可能只包含了头文件，但是没有链接：

```cmake
avformat.lib
```

同时 `avformat` 依赖：

```text
avcodec
avutil
```

所以不能只链接一个库。

------

## 5. ONNX 模型能运行但检测框完全不对

通常不是 ONNX Runtime 安装问题，而是以下环节错误：

```text
BGR 和 RGB 顺序错误
没有除以 255
NCHW 和 NHWC 顺序错误
LetterBox 坐标没有还原
模型输出格式判断错误
类别数量错误
YOLO版本后处理不匹配
NMS实现错误
```

YOLOv5、YOLOv8、YOLO11以及端到端带 NMS 的模型，输出格式可能不同。加载模型后应先打印：

```text
输入节点名称
输入尺寸
输出节点名称
输出尺寸
输出数据类型
```

再决定后处理方式。

------

# 十二、许可证问题

用于学习和内部测试时可以先使用现成共享包。但发布商业或闭源软件时需要认真检查 FFmpeg 构建配置。

FFmpeg 官方建议，为更容易满足 LGPL 要求，应避免启用 GPL 和 nonfree 组件，并使用 DLL 动态链接。([FFmpeg](https://www.ffmpeg.org/legal.html?utm_source=chatgpt.com))

需要注意，Gyan 页面说明其 `full-shared` 构建包含 GPLv3 组件。([Gyan](https://www.gyan.dev/ffmpeg/builds/))

因此：

```text
学习、课程设计、内部演示：
可以先用 full-shared 快速开发

商业闭源发布：
不要直接默认使用 full-shared
应选择或自行编译 LGPL 兼容构建
```

------

## 最终应该下载和保存的内容

### FFmpeg

```text
下载：Windows shared development build

保存：
include/
lib/
bin/中的全部 DLL
LICENSE
版本信息
```

### ONNX Runtime

```text
下载：Windows x64 CPU zip

保存：
include/整个目录
onnxruntime.lib
onnxruntime.dll
LICENSE
版本信息
```

### YOLO

```text
部署保存：
best.onnx
classes.txt
model.json

不需要：
PyTorch
Python
Ultralytics源码
训练数据集
best.pt
```

最适合你的第一阶段组合是：

```text
Qt 6 MSVC 2022 64-bit
FFmpeg shared
ONNX Runtime CPU
YOLO 固定 640×640 ONNX 模型
CMake 自动复制 DLL 和模型
```

先把 CPU 推理完整跑通，再增加 CUDA GPU 推理。