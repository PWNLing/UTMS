set(PROJECT_MEDIA_TARGET "${PROJECT_NAME}_media")
set(PROJECT_MEDIA_STATE_TARGET "${PROJECT_NAME}_media_state")

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

add_library(${PROJECT_MEDIA_TARGET} STATIC
    RtspController.cpp
    RtspController.h
    RtspDecoderWorker.cpp
    RtspDecoderWorker.h
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

foreach(ffmpeg_library IN LISTS FFMPEG_REQUIRED_LIBRARIES)
    if(NOT EXISTS "${ffmpeg_library}")
        message(FATAL_ERROR "Required FFmpeg library is missing: ${ffmpeg_library}")
    endif()
endforeach()

target_include_directories(${PROJECT_MEDIA_TARGET}
    SYSTEM PRIVATE
        "${FFMPEG_ROOT}/include"
)

target_link_libraries(${PROJECT_MEDIA_TARGET}
    PUBLIC
        ${PROJECT_NAMESPACE}::media_state
        Qt${QT_VERSION_MAJOR}::Gui
    PRIVATE
        ${FFMPEG_REQUIRED_LIBRARIES}
)

project_apply_compiler_options(${PROJECT_MEDIA_TARGET})
