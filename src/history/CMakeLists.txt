set(PROJECT_HISTORY_TARGET "${PROJECT_NAME}_history")

add_library(${PROJECT_HISTORY_TARGET} STATIC
    HistoryController.cpp
    HistoryController.h
    HistoryStore.cpp
    HistoryStore.h
)

add_library(${PROJECT_NAMESPACE}::history ALIAS ${PROJECT_HISTORY_TARGET})

target_include_directories(${PROJECT_HISTORY_TARGET}
    PUBLIC
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(${PROJECT_HISTORY_TARGET}
    PUBLIC
        Qt${QT_VERSION_MAJOR}::Core
        Qt${QT_VERSION_MAJOR}::Sql
)

project_apply_compiler_options(${PROJECT_HISTORY_TARGET})
