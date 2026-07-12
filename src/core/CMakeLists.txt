set(PROJECT_CORE_TARGET "${PROJECT_NAME}_core")

add_library(${PROJECT_CORE_TARGET} STATIC
    Logger.cpp
    Logger.h
    RadarFrameStore.cpp
    RadarFrameStore.h
    RadarJsonParser.cpp
    RadarJsonParser.h
    RadarTypes.cpp
    RadarTypes.h
)

add_library(${PROJECT_NAMESPACE}::core ALIAS ${PROJECT_CORE_TARGET})

target_include_directories(${PROJECT_CORE_TARGET}
    PUBLIC
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(${PROJECT_CORE_TARGET}
    PUBLIC
        Qt${QT_VERSION_MAJOR}::Core
)

project_apply_compiler_options(${PROJECT_CORE_TARGET})
