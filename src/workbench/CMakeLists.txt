set(PROJECT_WORKBENCH_TARGET "${PROJECT_NAME}_workbench")

add_library(${PROJECT_WORKBENCH_TARGET} STATIC
    TrackFilterProxyModel.cpp
    TrackFilterProxyModel.h
    TrackTableModel.cpp
    TrackTableModel.h
    TrackTableWidget.cpp
    TrackTableWidget.h
)

add_library(${PROJECT_NAMESPACE}::workbench ALIAS ${PROJECT_WORKBENCH_TARGET})

target_include_directories(${PROJECT_WORKBENCH_TARGET}
    PUBLIC
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(${PROJECT_WORKBENCH_TARGET}
    PUBLIC
        ${PROJECT_NAMESPACE}::core
        Qt${QT_VERSION_MAJOR}::Widgets
)

project_apply_compiler_options(${PROJECT_WORKBENCH_TARGET})
