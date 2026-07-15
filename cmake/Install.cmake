# =========================================================
# 项目安装规则
# =========================================================
include(GNUInstallDirs)

# 将 MSVC 运行库以应用本地 DLL 形式放到可执行程序旁，确保绿色目录
# 在未预装 Visual C++ Redistributable 的目标机上也能直接启动。
if(MSVC)
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_NO_WARNINGS TRUE)
    include(InstallRequiredSystemLibraries)

    # 非标准 Visual Studio 安装目录或较新的工具集可能无法被旧版 CMake
    # 完整发现；开发者命令行提供的 VCToolsRedistDir 可补齐结果。
    if(DEFINED ENV{VCToolsRedistDir})
        file(TO_CMAKE_PATH "$ENV{VCToolsRedistDir}" msvc_redist_directory)
        file(GLOB msvc_runtime_libraries
            LIST_DIRECTORIES FALSE
            "${msvc_redist_directory}/${CMAKE_MSVC_ARCH}/Microsoft.VC*.CRT/*.dll"
        )
        list(APPEND CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS ${msvc_runtime_libraries})
    endif()

    list(REMOVE_DUPLICATES CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
    set(required_msvc_runtime_names
        concrt140.dll
        msvcp140.dll
        vcruntime140.dll
        vcruntime140_1.dll
    )
    foreach(required_runtime_name IN LISTS required_msvc_runtime_names)
        set(required_runtime_found FALSE)
        foreach(runtime_path IN LISTS CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
            get_filename_component(runtime_name "${runtime_path}" NAME)
            if(runtime_name STREQUAL required_runtime_name)
                set(required_runtime_found TRUE)
                break()
            endif()
        endforeach()
        if(NOT required_runtime_found)
            message(FATAL_ERROR
                "Unable to locate required app-local MSVC runtime '${required_runtime_name}'. "
                "Configure from an x64 Visual Studio developer command prompt."
            )
        endif()
    endforeach()

    install(PROGRAMS ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS}
        DESTINATION "${CMAKE_INSTALL_BINDIR}"
    )
endif()

# 确认主程序目标已经由 src/CMakeLists.txt 创建。
if(NOT TARGET ${PROJECT_APP_TARGET})
    message(FATAL_ERROR
        "Install.cmake: target '${PROJECT_APP_TARGET}' does not exist"
    )
endif()

# 安装最终应用程序。
# BUNDLE 用于 macOS 应用包；RUNTIME 用于 Windows/Linux 可执行程序。
install(TARGETS ${PROJECT_APP_TARGET}
    BUNDLE
        DESTINATION .
    RUNTIME
        DESTINATION ${CMAKE_INSTALL_BINDIR}
)

set(video_performance_acceptance_target "${PROJECT_NAME}_video_performance_acceptance")
if(TARGET ${video_performance_acceptance_target})
    install(TARGETS ${video_performance_acceptance_target}
        RUNTIME
            DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

# 只发布配置示例，避免将开发机上的真实高德 Key 带入绿色目录。
install(FILES "${PROJECT_SOURCE_DIR}/config/amap.example.json"
    DESTINATION "${CMAKE_INSTALL_BINDIR}/config"
)

if(WIN32 AND EXISTS "${PROJECT_SOURCE_DIR}/third_party/ffmpeg/bin")
    install(DIRECTORY "${PROJECT_SOURCE_DIR}/third_party/ffmpeg/bin/"
        DESTINATION "${CMAKE_INSTALL_BINDIR}"
        FILES_MATCHING
            PATTERN "*.dll"
    )
endif()

if(WIN32 AND EXISTS "${PROJECT_SOURCE_DIR}/third_party/onnxruntime/lib")
    install(DIRECTORY "${PROJECT_SOURCE_DIR}/third_party/onnxruntime/lib/"
        DESTINATION "${CMAKE_INSTALL_BINDIR}"
        FILES_MATCHING
            PATTERN "*.dll"
    )
endif()

if(EXISTS "${PROJECT_SOURCE_DIR}/third_party/ffmpeg/LICENSE")
    install(FILES
        "${PROJECT_SOURCE_DIR}/third_party/ffmpeg/LICENSE"
        "${PROJECT_SOURCE_DIR}/third_party/ffmpeg/README.txt"
        DESTINATION "licenses/ffmpeg"
    )
endif()

if(EXISTS "${PROJECT_SOURCE_DIR}/third_party/onnxruntime/LICENSE")
    install(FILES
        "${PROJECT_SOURCE_DIR}/third_party/onnxruntime/LICENSE"
        "${PROJECT_SOURCE_DIR}/third_party/onnxruntime/Privacy.md"
        "${PROJECT_SOURCE_DIR}/third_party/onnxruntime/ThirdPartyNotices.txt"
        DESTINATION "licenses/onnxruntime"
    )
endif()

install(DIRECTORY "${PROJECT_SOURCE_DIR}/models/yolo26/"
    DESTINATION "${CMAKE_INSTALL_BINDIR}/models/yolo26"
    FILES_MATCHING
        PATTERN "*.onnx"
        PATTERN "*.txt"
        PATTERN "*.json"
)

# 离线地图严格从可执行程序相对目录 data/map/amap 读取。若开发机已
# 放置瓦片，则一并复制；未放置时仍通过说明文件创建正确的目录结构。
if(EXISTS "${PROJECT_SOURCE_DIR}/data/map/amap")
    install(DIRECTORY "${PROJECT_SOURCE_DIR}/data/map/amap/"
        DESTINATION "${CMAKE_INSTALL_BINDIR}/data/map/amap"
        FILES_MATCHING
            PATTERN "*.png"
    )
endif()

install(FILES "${PROJECT_SOURCE_DIR}/docs/offline-tiles.md"
    DESTINATION "${CMAKE_INSTALL_BINDIR}/data/map/amap"
    RENAME README.md
)

install(FILES "${PROJECT_SOURCE_DIR}/docs/runtime-logs.md"
    DESTINATION "${CMAKE_INSTALL_BINDIR}/logs"
    RENAME README.md
)

install(FILES
    "${PROJECT_SOURCE_DIR}/README.md"
    "${PROJECT_SOURCE_DIR}/docs/acceptance-checklist.md"
    DESTINATION .
)

# Qt 6 的部署脚本在安装阶段收集 Qt DLL、插件和 WebEngine 资源；MSVC
# 运行库已在上方独立安装，使安装前缀成为可直接打包的绿色发布目录。
if(QT_VERSION_MAJOR GREATER_EQUAL 6)
    qt_generate_deploy_app_script(
        TARGET ${PROJECT_APP_TARGET}
        OUTPUT_SCRIPT qt_deploy_script
        NO_COMPILER_RUNTIME
        NO_UNSUPPORTED_PLATFORM_ERROR
    )
    install(SCRIPT ${qt_deploy_script})
endif()

# 部署脚本执行后验证视频运行库和模型资源，避免生成能够安装但无法
# 预览或推理的不完整绿色目录。
install(CODE "
    set(UTMS_RELEASE_ROOT \"\${CMAKE_INSTALL_PREFIX}\")
    set(UTMS_RELEASE_BINDIR \"${CMAKE_INSTALL_BINDIR}\")
    set(UTMS_RELEASE_EXECUTABLE_NAME \"${PROJECT_APP_TARGET}${CMAKE_EXECUTABLE_SUFFIX}\")
    include(\"${PROJECT_SOURCE_DIR}/cmake/VerifyRelease.cmake\")
")
