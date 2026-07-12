set(PROJECT_NETWORK_TARGET "${PROJECT_NAME}_network")

add_library(${PROJECT_NETWORK_TARGET} STATIC
    UdpReceiver.cpp
    UdpReceiver.h
)

add_library(${PROJECT_NAMESPACE}::network ALIAS ${PROJECT_NETWORK_TARGET})

target_include_directories(${PROJECT_NETWORK_TARGET}
    PUBLIC
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(${PROJECT_NETWORK_TARGET}
    PUBLIC
        ${PROJECT_NAMESPACE}::core
        Qt${QT_VERSION_MAJOR}::Network
)

project_apply_compiler_options(${PROJECT_NETWORK_TARGET})
