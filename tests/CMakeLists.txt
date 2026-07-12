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
