set(PROJECT_CORE_TARGET "${PROJECT_NAME}_core")

add_library(${PROJECT_CORE_TARGET} STATIC
    GeofenceGeometry.cpp
    GeofenceGeometry.h
    GeofenceTypes.cpp
    GeofenceTypes.h
    LoginCredentialValidator.cpp
    LoginCredentialValidator.h
    Logger.cpp
    Logger.h
    RadarFrameStore.cpp
    RadarFrameStore.h
    RadarJsonParser.cpp
    RadarJsonParser.h
    RadarTypes.cpp
    RadarTypes.h
    SystemMetricsProvider.cpp
    SystemMetricsProvider.h
    SystemMetricsSampler.cpp
    SystemMetricsSampler.h
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

if(WIN32)
    target_link_libraries(${PROJECT_CORE_TARGET}
        PRIVATE
            Psapi
    )
endif()

project_apply_compiler_options(${PROJECT_CORE_TARGET})
