find_package(Qt${QT_VERSION_MAJOR}
    REQUIRED
    COMPONENTS Test
)

set(PROJECT_TEST_TARGET "${PROJECT_NAME}_core_tests")

add_executable(${PROJECT_TEST_TARGET}
    test_radarcore.cpp
)

target_link_libraries(${PROJECT_TEST_TARGET}
    PRIVATE
        ${PROJECT_NAMESPACE}::core
        ${PROJECT_NAMESPACE}::map
        ${PROJECT_NAMESPACE}::network
        Qt${QT_VERSION_MAJOR}::Test
)

project_apply_compiler_options(${PROJECT_TEST_TARGET})

add_test(
    NAME ${PROJECT_TEST_TARGET}
    COMMAND ${PROJECT_TEST_TARGET}
)

set(PROJECT_MAP_INTERACTION_TEST_TARGET "${PROJECT_NAME}_map_interaction_tests")

add_executable(${PROJECT_MAP_INTERACTION_TEST_TARGET}
    test_map_interaction.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/OfflineMapWidget.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/OfflineMapWidget.h
)

target_link_libraries(${PROJECT_MAP_INTERACTION_TEST_TARGET}
    PRIVATE
        ${PROJECT_NAMESPACE}::map
        Qt${QT_VERSION_MAJOR}::Concurrent
        Qt${QT_VERSION_MAJOR}::Test
        Qt${QT_VERSION_MAJOR}::Widgets
)

project_apply_compiler_options(${PROJECT_MAP_INTERACTION_TEST_TARGET})

add_test(
    NAME ${PROJECT_MAP_INTERACTION_TEST_TARGET}
    COMMAND ${PROJECT_MAP_INTERACTION_TEST_TARGET}
)

set(PROJECT_TRACK_WORKBENCH_TEST_TARGET "${PROJECT_NAME}_track_workbench_tests")

add_executable(${PROJECT_TRACK_WORKBENCH_TEST_TARGET}
    test_track_workbench.cpp
)

target_link_libraries(${PROJECT_TRACK_WORKBENCH_TEST_TARGET}
    PRIVATE
        ${PROJECT_NAMESPACE}::workbench
        Qt${QT_VERSION_MAJOR}::Test
)

project_apply_compiler_options(${PROJECT_TRACK_WORKBENCH_TEST_TARGET})

add_test(
    NAME ${PROJECT_TRACK_WORKBENCH_TEST_TARGET}
    COMMAND ${PROJECT_TRACK_WORKBENCH_TEST_TARGET}
)

if(WIN32)
    set_tests_properties(
        ${PROJECT_TEST_TARGET}
        ${PROJECT_MAP_INTERACTION_TEST_TARGET}
        ${PROJECT_TRACK_WORKBENCH_TEST_TARGET}
        PROPERTIES
        ENVIRONMENT "PATH=$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>\;$ENV{PATH}"
    )
endif()
