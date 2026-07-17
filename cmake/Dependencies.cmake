# =========================================================
# Qt 依赖
# =========================================================
# 在这里集中维护项目使用的 Qt 模块。
# 以后使用 Network、Sql、SerialPort、Charts 等模块时，分别将它们
# 添加到下面的列表中即可；随后还要让真正使用它们的目标进行链接。
set(PROJECT_QT_COMPONENTS
    Charts
    Concurrent
    Core
    Gui
    Network
    Sql
    WebChannel
    WebEngineWidgets
    Widgets
)

# 第一次查找用于确定本机使用 Qt 6 还是 Qt 5，并设置
# QT_VERSION_MAJOR 变量。
find_package(QT
    NAMES Qt6 Qt5
    REQUIRED
    COMPONENTS ${PROJECT_QT_COMPONENTS}
)

# 第二次查找用于加载具体版本的导入目标，例如：
# Qt6::Widgets 或 Qt5::Widgets。
find_package(Qt${QT_VERSION_MAJOR}
    REQUIRED
    COMPONENTS ${PROJECT_QT_COMPONENTS}
)

# =========================================================
# 第三方依赖
# =========================================================
# FFmpeg、ONNX Runtime 等第三方库应在真正使用它们的模块中链接，
# 不建议无条件全部链接到主程序。以后可以在这里定义 IMPORTED 目标，
# 再由 media、inference 等模块按需链接。
