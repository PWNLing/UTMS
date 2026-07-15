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

set(PROJECT_LOGIN_CREDENTIALS_TEST_TARGET "${PROJECT_NAME}_login_credentials_tests")

add_executable(${PROJECT_LOGIN_CREDENTIALS_TEST_TARGET}
    test_login_credentials.cpp
)

target_link_libraries(${PROJECT_LOGIN_CREDENTIALS_TEST_TARGET}
    PRIVATE
        ${PROJECT_NAMESPACE}::core
        Qt${QT_VERSION_MAJOR}::Test
)

project_apply_compiler_options(${PROJECT_LOGIN_CREDENTIALS_TEST_TARGET})

add_test(
    NAME ${PROJECT_LOGIN_CREDENTIALS_TEST_TARGET}
    COMMAND ${PROJECT_LOGIN_CREDENTIALS_TEST_TARGET}
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

set(PROJECT_RTSP_STATE_TEST_TARGET "${PROJECT_NAME}_rtsp_state_tests")

add_executable(${PROJECT_RTSP_STATE_TEST_TARGET}
    test_rtsp_state_machine.cpp
)

target_link_libraries(${PROJECT_RTSP_STATE_TEST_TARGET}
    PRIVATE
        ${PROJECT_NAMESPACE}::media_state
        Qt${QT_VERSION_MAJOR}::Test
)

project_apply_compiler_options(${PROJECT_RTSP_STATE_TEST_TARGET})

add_test(
    NAME ${PROJECT_RTSP_STATE_TEST_TARGET}
    COMMAND ${PROJECT_RTSP_STATE_TEST_TARGET}
)

set(PROJECT_VIDEO_INFERENCE_TEST_TARGET "${PROJECT_NAME}_video_inference_tests")

add_executable(${PROJECT_VIDEO_INFERENCE_TEST_TARGET}
    test_video_inference.cpp
)

target_link_libraries(${PROJECT_VIDEO_INFERENCE_TEST_TARGET}
    PRIVATE
        ${PROJECT_NAMESPACE}::video_inference
        Qt${QT_VERSION_MAJOR}::Gui
        Qt${QT_VERSION_MAJOR}::Test
)

project_apply_compiler_options(${PROJECT_VIDEO_INFERENCE_TEST_TARGET})

add_test(
    NAME ${PROJECT_VIDEO_INFERENCE_TEST_TARGET}
    COMMAND ${PROJECT_VIDEO_INFERENCE_TEST_TARGET}
)

set(PROJECT_YOLO_RUNTIME_TEST_TARGET "${PROJECT_NAME}_yolo_runtime_tests")

add_executable(${PROJECT_YOLO_RUNTIME_TEST_TARGET}
    test_yolo_runtime.cpp
)

target_compile_definitions(${PROJECT_YOLO_RUNTIME_TEST_TARGET}
    PRIVATE
        UTMS_TEST_MODEL_DIRECTORY="${PROJECT_SOURCE_DIR}/models/yolo26"
)

target_link_libraries(${PROJECT_YOLO_RUNTIME_TEST_TARGET}
    PRIVATE
        ${PROJECT_NAMESPACE}::media
        Qt${QT_VERSION_MAJOR}::Test
)

project_apply_compiler_options(${PROJECT_YOLO_RUNTIME_TEST_TARGET})

if(WIN32)
    add_custom_command(TARGET ${PROJECT_YOLO_RUNTIME_TEST_TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/third_party/onnxruntime/lib/onnxruntime.dll"
            "$<TARGET_FILE_DIR:${PROJECT_YOLO_RUNTIME_TEST_TARGET}>"
        COMMENT "Copying ONNX Runtime next to the runtime test"
    )
endif()

add_test(
    NAME ${PROJECT_YOLO_RUNTIME_TEST_TARGET}
    COMMAND ${PROJECT_YOLO_RUNTIME_TEST_TARGET}
)

set(PROJECT_VIDEO_PERFORMANCE_TARGET "${PROJECT_NAME}_video_performance_acceptance")

add_executable(${PROJECT_VIDEO_PERFORMANCE_TARGET}
    video_performance_acceptance.cpp
)

set_target_properties(${PROJECT_VIDEO_PERFORMANCE_TARGET}
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/acceptance"
)

target_link_libraries(${PROJECT_VIDEO_PERFORMANCE_TARGET}
    PRIVATE
        ${PROJECT_NAMESPACE}::media
        Qt${QT_VERSION_MAJOR}::Core
        Qt${QT_VERSION_MAJOR}::Gui
)

project_apply_compiler_options(${PROJECT_VIDEO_PERFORMANCE_TARGET})

if(WIN32)
    file(GLOB PROJECT_VIDEO_PERFORMANCE_RUNTIME_LIBRARIES
        LIST_DIRECTORIES FALSE
        "${PROJECT_SOURCE_DIR}/third_party/onnxruntime/lib/*.dll"
    )
    add_custom_command(TARGET ${PROJECT_VIDEO_PERFORMANCE_TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${PROJECT_SOURCE_DIR}/models/yolo26"
            "$<TARGET_FILE_DIR:${PROJECT_VIDEO_PERFORMANCE_TARGET}>/models/yolo26"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${PROJECT_SOURCE_DIR}/third_party/ffmpeg/bin"
            "$<TARGET_FILE_DIR:${PROJECT_VIDEO_PERFORMANCE_TARGET}>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${PROJECT_VIDEO_PERFORMANCE_RUNTIME_LIBRARIES}
            "$<TARGET_FILE_DIR:${PROJECT_VIDEO_PERFORMANCE_TARGET}>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt${QT_VERSION_MAJOR}::Core>"
            "$<TARGET_FILE:Qt${QT_VERSION_MAJOR}::Gui>"
            "$<TARGET_FILE_DIR:${PROJECT_VIDEO_PERFORMANCE_TARGET}>"
        COMMENT "Copying runtime libraries next to the video performance acceptance tool"
    )
endif()

add_test(
    NAME ${PROJECT_NAME}_release_validation_tests
    COMMAND ${CMAKE_COMMAND}
        -DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}
        -DTEST_WORK_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/release-validation-test
        -P ${CMAKE_CURRENT_SOURCE_DIR}/test_release_validation.cmake
)

if(WIN32)
    set_tests_properties(
        ${PROJECT_TEST_TARGET}
        ${PROJECT_LOGIN_CREDENTIALS_TEST_TARGET}
        ${PROJECT_MAP_INTERACTION_TEST_TARGET}
        ${PROJECT_RTSP_STATE_TEST_TARGET}
        ${PROJECT_TRACK_WORKBENCH_TEST_TARGET}
        ${PROJECT_VIDEO_INFERENCE_TEST_TARGET}
        ${PROJECT_YOLO_RUNTIME_TEST_TARGET}
        PROPERTIES
        ENVIRONMENT "PATH=$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>\;${PROJECT_SOURCE_DIR}/third_party/ffmpeg/bin\;${PROJECT_SOURCE_DIR}/third_party/onnxruntime/lib\;$ENV{PATH}"
    )
endif()
