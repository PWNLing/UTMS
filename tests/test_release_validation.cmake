if(NOT DEFINED PROJECT_SOURCE_DIR OR NOT DEFINED TEST_WORK_DIRECTORY)
    message(FATAL_ERROR "Release validation test requires PROJECT_SOURCE_DIR and TEST_WORK_DIRECTORY")
endif()

include("${PROJECT_SOURCE_DIR}/cmake/ReleaseValidation.cmake")

file(REMOVE_RECURSE "${TEST_WORK_DIRECTORY}")
set(release_bin_directory "${TEST_WORK_DIRECTORY}/bin")
file(MAKE_DIRECTORY
    "${release_bin_directory}/models/yolo26"
)

set(required_release_files
    "UTMS.exe"
    "UTMS_video_performance_acceptance.exe"
    "avcodec-62.dll"
    "avformat-62.dll"
    "avutil-60.dll"
    "swscale-9.dll"
    "swresample-6.dll"
    "onnxruntime.dll"
    "onnxruntime_providers_shared.dll"
    "models/yolo26/yolo26n.onnx"
    "models/yolo26/classes.txt"
    "models/yolo26/model.json"
)
foreach(required_release_file IN LISTS required_release_files)
    file(TOUCH "${release_bin_directory}/${required_release_file}")
endforeach()

utms_validate_release_layout(
    "${TEST_WORK_DIRECTORY}"
    "bin"
    "UTMS.exe"
    validation_errors
)
if(validation_errors)
    list(JOIN validation_errors "\n" validation_error_text)
    message(FATAL_ERROR "Complete release fixture was rejected:\n${validation_error_text}")
endif()

file(REMOVE "${release_bin_directory}/UTMS_video_performance_acceptance.exe")
utms_validate_release_layout(
    "${TEST_WORK_DIRECTORY}"
    "bin"
    "UTMS.exe"
    validation_errors
)
list(FIND validation_errors
    "Missing required release file: bin/UTMS_video_performance_acceptance.exe"
    missing_performance_tool_error_index
)
if(missing_performance_tool_error_index EQUAL -1)
    message(FATAL_ERROR "Missing performance acceptance tool was not reported: ${validation_errors}")
endif()

file(TOUCH "${release_bin_directory}/UTMS_video_performance_acceptance.exe")
file(REMOVE "${release_bin_directory}/onnxruntime.dll")
utms_validate_release_layout(
    "${TEST_WORK_DIRECTORY}"
    "bin"
    "UTMS.exe"
    validation_errors
)
list(FIND validation_errors "Missing required release file: bin/onnxruntime.dll" missing_runtime_error_index)
if(missing_runtime_error_index EQUAL -1)
    message(FATAL_ERROR "Missing ONNX Runtime DLL was not reported: ${validation_errors}")
endif()

file(TOUCH "${release_bin_directory}/onnxruntime.dll")
file(MAKE_DIRECTORY "${TEST_WORK_DIRECTORY}/third_party/models")
file(TOUCH "${TEST_WORK_DIRECTORY}/third_party/models/yolo26n.onnx")
utms_validate_release_layout(
    "${TEST_WORK_DIRECTORY}"
    "bin"
    "UTMS.exe"
    validation_errors
)
list(FIND validation_errors
    "Model resources must not be placed under the release third_party directory"
    misplaced_model_error_index
)
if(misplaced_model_error_index EQUAL -1)
    message(FATAL_ERROR "Misplaced model resource was not reported: ${validation_errors}")
endif()

file(REMOVE_RECURSE "${TEST_WORK_DIRECTORY}")
