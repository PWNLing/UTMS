if(NOT DEFINED UTMS_RELEASE_ROOT OR NOT DEFINED UTMS_RELEASE_BINDIR OR
   NOT DEFINED UTMS_RELEASE_EXECUTABLE_NAME)
    message(FATAL_ERROR
        "Release validation requires UTMS_RELEASE_ROOT, UTMS_RELEASE_BINDIR, and "
        "UTMS_RELEASE_EXECUTABLE_NAME"
    )
endif()

include("${CMAKE_CURRENT_LIST_DIR}/ReleaseValidation.cmake")

utms_validate_release_layout(
    "${UTMS_RELEASE_ROOT}"
    "${UTMS_RELEASE_BINDIR}"
    "${UTMS_RELEASE_EXECUTABLE_NAME}"
    release_validation_errors
)
if(release_validation_errors)
    list(JOIN release_validation_errors "\n  - " release_validation_error_text)
    message(FATAL_ERROR "Incomplete UTMS green release:\n  - ${release_validation_error_text}")
endif()

message(STATUS "UTMS green release layout verified: ${UTMS_RELEASE_ROOT}")
