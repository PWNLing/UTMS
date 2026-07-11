cmake_minimum_required(VERSION 3.16)

# =========================================================
# 项目基本信息
# =========================================================
# 更换项目时，通常只需要修改这里的项目名、版本和语言。
project(UTMS
    VERSION 0.1.0
    LANGUAGES CXX
)

# =========================================================
# 项目通用变量
# =========================================================
# 最终应用程序目标名。默认与 project() 中的项目名一致。
# 如果未来希望项目名与可执行程序名不同，只需要修改此变量。
set(PROJECT_APP_TARGET "${PROJECT_NAME}")

# 内部库目标使用的命名空间。
# 例如项目名为 ExampleProject 时，UI 模块别名为 ExampleProject::ui；
# 项目更名后，命名空间会自动跟随变化。
set(PROJECT_NAMESPACE "${PROJECT_NAME}")

# =========================================================
# C++ 编译设置
# =========================================================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 生成 compile_commands.json，方便 clangd、VS Code 等工具分析源码。
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# =========================================================
# Qt 自动代码生成
# =========================================================
# 自动调用 moc，处理包含 Q_OBJECT 的类。
set(CMAKE_AUTOMOC ON)

# 自动调用 uic，处理 Qt Designer 创建的 .ui 文件。
set(CMAKE_AUTOUIC ON)

# 自动调用 rcc，处理 Qt 资源文件 .qrc。
set(CMAKE_AUTORCC ON)

# =========================================================
# 自定义 CMake 模块
# =========================================================
# 将项目根目录下的 cmake/ 加入模块搜索路径，之后可以直接
# include(CompilerOptions)，而不必写完整文件路径。
list(APPEND CMAKE_MODULE_PATH
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake"
)

# 加载统一编译选项函数。
include(CompilerOptions)

# 查找 Qt 等项目依赖。
include(Dependencies)

# =========================================================
# 主程序和内部模块
# =========================================================
# 进入 src/，创建 UI 库和最终可执行程序。
add_subdirectory(src)

# =========================================================
# 单元测试
# =========================================================
# 默认关闭。需要测试时可使用：
# cmake -S . -B build -DPROJECT_BUILD_TESTS=ON
option(PROJECT_BUILD_TESTS "Build project tests" OFF)

if(PROJECT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# =========================================================
# 安装规则
# =========================================================
# 必须放在 add_subdirectory(src) 之后，因为安装规则引用了
# 在 src/CMakeLists.txt 中创建的 ${PROJECT_APP_TARGET} 目标。
include(Install)
