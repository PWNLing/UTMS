set(PROJECT_MEDIA_TARGET "${PROJECT_NAME}_media")
set(PROJECT_MEDIA_STATE_TARGET "${PROJECT_NAME}_media_state")
set(PROJECT_VIDEO_INFERENCE_TARGET "${PROJECT_NAME}_video_inference")

add_library(${PROJECT_MEDIA_STATE_TARGET} STATIC
    RtspStateMachine.cpp
    RtspStateMachine.h
)

add_library(${PROJECT_NAMESPACE}::media_state ALIAS ${PROJECT_MEDIA_STATE_TARGET})

target_include_directories(${PROJECT_MEDIA_STATE_TARGET}
    PUBLIC
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(${PROJECT_MEDIA_STATE_TARGET}
    PUBLIC
        Qt${QT_VERSION_MAJOR}::Core
)

project_apply_compiler_options(${PROJECT_MEDIA_STATE_TARGET})

add_library(${PROJECT_VIDEO_INFERENCE_TARGET} STATIC
    LatestVideoFrameBuffer.cpp
    LatestVideoFrameBuffer.h
    VideoDetection.cpp
    VideoDetection.h
    YoloModelConfig.cpp
    YoloModelConfig.h
    YoloPostProcessor.cpp
    YoloPostProcessor.h
)

add_library(${PROJECT_NAMESPACE}::video_inference ALIAS ${PROJECT_VIDEO_INFERENCE_TARGET})

target_include_directories(${PROJECT_VIDEO_INFERENCE_TARGET}
    PUBLIC
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(${PROJECT_VIDEO_INFERENCE_TARGET}
    PUBLIC
        Qt${QT_VERSION_MAJOR}::Core
        Qt${QT_VERSION_MAJOR}::Gui
)

project_apply_compiler_options(${PROJECT_VIDEO_INFERENCE_TARGET})

add_library(${PROJECT_MEDIA_TARGET} STATIC
    RtspController.cpp
    RtspController.h
    RtspDecoderWorker.cpp
    RtspDecoderWorker.h
    VideoInferenceController.cpp
    VideoInferenceController.h
    YoloInferenceWorker.cpp
    YoloInferenceWorker.h
)

add_library(${PROJECT_NAMESPACE}::media ALIAS ${PROJECT_MEDIA_TARGET})

target_include_directories(${PROJECT_MEDIA_TARGET}
    PUBLIC
        ${PROJECT_SOURCE_DIR}/src
)

set(FFMPEG_ROOT "${PROJECT_SOURCE_DIR}/third_party/ffmpeg")
set(FFMPEG_REQUIRED_LIBRARIES
    "${FFMPEG_ROOT}/lib/avcodec.lib"
    "${FFMPEG_ROOT}/lib/avformat.lib"
    "${FFMPEG_ROOT}/lib/avutil.lib"
    "${FFMPEG_ROOT}/lib/swscale.lib"
)

set(ONNXRUNTIME_ROOT "${PROJECT_SOURCE_DIR}/third_party/onnxruntime")
set(ONNXRUNTIME_LIBRARY "${ONNXRUNTIME_ROOT}/lib/onnxruntime.lib")
set(ONNXRUNTIME_RUNTIME_LIBRARY "${ONNXRUNTIME_ROOT}/lib/onnxruntime.dll")

if(NOT EXISTS "${ONNXRUNTIME_LIBRARY}")
    message(FATAL_ERROR "Required ONNX Runtime library is missing: ${ONNXRUNTIME_LIBRARY}")
endif()
if(WIN32 AND NOT EXISTS "${ONNXRUNTIME_RUNTIME_LIBRARY}")
    message(FATAL_ERROR "Required ONNX Runtime DLL is missing: ${ONNXRUNTIME_RUNTIME_LIBRARY}")
endif()

foreach(ffmpeg_library IN LISTS FFMPEG_REQUIRED_LIBRARIES)
    if(NOT EXISTS "${ffmpeg_library}")
        message(FATAL_ERROR "Required FFmpeg library is missing: ${ffmpeg_library}")
    endif()
endforeach()

target_include_directories(${PROJECT_MEDIA_TARGET}
    SYSTEM PRIVATE
        "${FFMPEG_ROOT}/include"
        "${ONNXRUNTIME_ROOT}/include"
)

target_link_libraries(${PROJECT_MEDIA_TARGET}
    PUBLIC
        ${PROJECT_NAMESPACE}::media_state
        ${PROJECT_NAMESPACE}::video_inference
        Qt${QT_VERSION_MAJOR}::Gui
    PRIVATE
        ${FFMPEG_REQUIRED_LIBRARIES}
        "${ONNXRUNTIME_LIBRARY}"
)

project_apply_compiler_options(${PROJECT_MEDIA_TARGET})
