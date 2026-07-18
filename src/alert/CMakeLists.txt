set(PROJECT_ALERT_TARGET "${PROJECT_NAME}_alert")

add_library(${PROJECT_ALERT_TARGET} STATIC
    AlertEngine.cpp
    AlertEngine.h
    AlertTypes.cpp
    AlertTypes.h
    AlertWorker.cpp
    AlertWorker.h
)

add_library(${PROJECT_NAMESPACE}::alert ALIAS ${PROJECT_ALERT_TARGET})

target_include_directories(${PROJECT_ALERT_TARGET}
    PUBLIC
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(${PROJECT_ALERT_TARGET}
    PUBLIC
        ${PROJECT_NAMESPACE}::core
        Qt${QT_VERSION_MAJOR}::Core
)

project_apply_compiler_options(${PROJECT_ALERT_TARGET})
