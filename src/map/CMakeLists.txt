set(PROJECT_MAP_TARGET "${PROJECT_NAME}_map")

add_library(${PROJECT_MAP_TARGET} STATIC
    AmapConfig.cpp
    AmapConfig.h
    OnlineMapState.cpp
    OnlineMapState.h
    RealtimeTrajectoryModel.cpp
    RealtimeTrajectoryModel.h
    WebMercator.cpp
    WebMercator.h
)

add_library(${PROJECT_NAMESPACE}::map ALIAS ${PROJECT_MAP_TARGET})

target_include_directories(${PROJECT_MAP_TARGET}
    PUBLIC
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(${PROJECT_MAP_TARGET}
    PUBLIC
        ${PROJECT_NAMESPACE}::core
        Qt${QT_VERSION_MAJOR}::Core
)

project_apply_compiler_options(${PROJECT_MAP_TARGET})
