# =========================================================
# 项目安装规则
# =========================================================
include(GNUInstallDirs)

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

# config/ 保存可在程序外部修改的运行配置，不编译进可执行程序。
if(EXISTS "${PROJECT_SOURCE_DIR}/config")
    install(DIRECTORY "${PROJECT_SOURCE_DIR}/config/"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME}/config"
    )
endif()

# models/ 保存神经网络模型等运行时数据，通常也不编译进程序。
if(EXISTS "${PROJECT_SOURCE_DIR}/models")
    install(DIRECTORY "${PROJECT_SOURCE_DIR}/models/"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME}/models"
    )
endif()
