function(utms_validate_release_layout release_root install_bindir executable_name output_errors)
    set(validation_errors)
    set(release_bin_directory "${release_root}/${install_bindir}")
    get_filename_component(executable_stem "${executable_name}" NAME_WE)
    get_filename_component(executable_extension "${executable_name}" LAST_EXT)
    set(performance_executable_name
        "${executable_stem}_video_performance_acceptance${executable_extension}"
    )

    set(required_release_files
        "${executable_name}"
        "${performance_executable_name}"
        "onnxruntime.dll"
        "onnxruntime_providers_shared.dll"
        "models/yolo26/yolo26n.onnx"
        "models/yolo26/classes.txt"
        "models/yolo26/model.json"
    )
    foreach(required_release_file IN LISTS required_release_files)
        if(NOT EXISTS "${release_bin_directory}/${required_release_file}")
            list(APPEND validation_errors
                "Missing required release file: ${install_bindir}/${required_release_file}"
            )
        endif()
    endforeach()

    set(required_ffmpeg_patterns
        "avcodec-*.dll"
        "avformat-*.dll"
        "avutil-*.dll"
        "swscale-*.dll"
        "swresample-*.dll"
    )
    foreach(required_ffmpeg_pattern IN LISTS required_ffmpeg_patterns)
        file(GLOB matching_ffmpeg_libraries
            LIST_DIRECTORIES FALSE
            "${release_bin_directory}/${required_ffmpeg_pattern}"
        )
        if(NOT matching_ffmpeg_libraries)
            list(APPEND validation_errors
                "Missing required FFmpeg runtime: ${install_bindir}/${required_ffmpeg_pattern}"
            )
        endif()
    endforeach()

    file(GLOB_RECURSE misplaced_model_resources
        LIST_DIRECTORIES FALSE
        "${release_root}/third_party/*.onnx"
        "${release_root}/third_party/classes.txt"
        "${release_root}/third_party/model.json"
    )
    if(misplaced_model_resources)
        list(APPEND validation_errors
            "Model resources must not be placed under the release third_party directory"
        )
    endif()

    set(${output_errors} "${validation_errors}" PARENT_SCOPE)
endfunction()
