# =========================================================
# UI 模块
# =========================================================
# 真实目标使用普通名称，例如项目名为 ExampleProject 时得到 ExampleProject_ui。
# PROJECT_UI_TARGET 是通用变量名，换成其他项目后语义不变。
set(PROJECT_UI_TARGET "${PROJECT_NAME}_ui")

# 当前 UI 模块编译为静态库。
# 以后新增窗口、对话框或自定义控件时，继续在此列表中添加文件。
add_library(${PROJECT_UI_TARGET} STATIC
    BottomStatusBar.cpp
    BottomStatusBar.h
    MapPanel.cpp
    MapPanel.h
    MapWebBridge.cpp
    MapWebBridge.h
    OfflineMapWidget.cpp
    OfflineMapWidget.h
    OnlineMapWidget.cpp
    OnlineMapWidget.h
    StatisticsWidget.cpp
    StatisticsWidget.h
    mainwindow.cpp
    mainwindow.h
)

# 为真实目标创建带命名空间的别名。
# 项目名为 ExampleProject 时是 ExampleProject::ui；项目改名后会自动变化。
# 其他模块应优先链接这个别名，而不是直接链接真实目标名。
add_library(${PROJECT_NAMESPACE}::ui
    ALIAS
    ${PROJECT_UI_TARGET}
)

# =========================================================
# UI 头文件搜索路径
# =========================================================
# 将 src/ 作为公开头文件根目录，因此其他目标可以写：
# #include "ui/mainwindow.h"
#
# PUBLIC 表示使用 UI 模块的目标也会继承该头文件搜索路径。
target_include_directories(${PROJECT_UI_TARGET}
    PUBLIC
        ${PROJECT_SOURCE_DIR}/src
)

# =========================================================
# UI 模块依赖
# =========================================================
# MainWindow 继承 QMainWindow，因此 UI 模块依赖 Qt Widgets。
# 使用 PUBLIC 是因为 mainwindow.h 的公开接口中出现了 Qt 类型，依赖
# 需要继续传播给使用该模块的目标。
target_link_libraries(${PROJECT_UI_TARGET}
    PUBLIC
        ${PROJECT_NAMESPACE}::network
        ${PROJECT_NAMESPACE}::map
        ${PROJECT_NAMESPACE}::workbench
        Qt${QT_VERSION_MAJOR}::Charts
        Qt${QT_VERSION_MAJOR}::Concurrent
        Qt${QT_VERSION_MAJOR}::WebChannel
        Qt${QT_VERSION_MAJOR}::WebEngineWidgets
        Qt${QT_VERSION_MAJOR}::Widgets
)

# 为 UI 库应用统一警告选项。
project_apply_compiler_options(${PROJECT_UI_TARGET})
