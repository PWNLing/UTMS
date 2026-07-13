# =========================================================
# 项目内部模块
# =========================================================
# 核心业务模块不依赖界面；通信模块依赖核心模块；UI 位于最上层。
add_subdirectory(core)
add_subdirectory(map)
add_subdirectory(network)
add_subdirectory(workbench)
add_subdirectory(ui)

# =========================================================
# 主程序源文件
# =========================================================
# 该变量只表示最终应用程序自身的源文件。
# UI、通信、数据库等模块的源码应由各自的库目标管理，而不是全部
# 堆放到这个列表中。
set(PROJECT_APP_SOURCES
    main.cpp
)

# 如果以后创建 resources/resources.qrc，则自动将其加入主程序。
# CMAKE_AUTORCC 已在根 CMakeLists.txt 中开启。
if(EXISTS "${PROJECT_SOURCE_DIR}/resources/resources.qrc")
    list(APPEND PROJECT_APP_SOURCES
        "${PROJECT_SOURCE_DIR}/resources/resources.qrc"
    )
endif()

# =========================================================
# 创建最终可执行程序
# =========================================================
# Qt 6 使用 qt_add_executable()，它会处理部分 Qt 平台相关设置；
# Qt 5 使用标准 CMake 的 add_executable()。
#
# 这里没有使用 MANUAL_FINALIZATION，所以 Qt 6 会自动完成收尾，
# 不需要再手动调用 qt_finalize_target() 或 qt_finalize_executable()。
if(QT_VERSION_MAJOR GREATER_EQUAL 6)
    qt_add_executable(${PROJECT_APP_TARGET}
        ${PROJECT_APP_SOURCES}
    )
else()
    add_executable(${PROJECT_APP_TARGET}
        ${PROJECT_APP_SOURCES}
    )
endif()

# =========================================================
# 主程序依赖
# =========================================================
# 主程序直接链接项目自己的 UI 模块。
# Qt Widgets 是 UI 模块的 PUBLIC 依赖，会自动传播给主程序。
target_link_libraries(${PROJECT_APP_TARGET}
    PRIVATE
        ${PROJECT_NAMESPACE}::ui
)

# 为主程序应用统一警告选项。
project_apply_compiler_options(${PROJECT_APP_TARGET})

# Windows GUI 程序发布时不弹出额外的控制台窗口。
# 调试时若希望保留控制台，可暂时注释该属性。
if(WIN32)
    set_target_properties(${PROJECT_APP_TARGET}
        PROPERTIES
            WIN32_EXECUTABLE TRUE
    )
endif()

# =========================================================
# 复制运行时数据
# =========================================================
# 构建完成后，将 config/ 和 data/ 复制到可执行程序旁边，方便直接
# 从 Qt Creator 或命令行运行程序。
foreach(runtime_directory IN ITEMS config data)
    if(EXISTS "${PROJECT_SOURCE_DIR}/${runtime_directory}")
        add_custom_command(TARGET ${PROJECT_APP_TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${PROJECT_SOURCE_DIR}/${runtime_directory}"
                "$<TARGET_FILE_DIR:${PROJECT_APP_TARGET}>/${runtime_directory}"
            COMMENT "Copying ${runtime_directory} directory"
        )
    endif()
endforeach()
