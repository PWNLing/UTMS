# =========================================================
# 单元测试
# =========================================================
# 当前工程尚未提供测试源码，因此这里保留一个通用模板。
# 在根目录配置时使用 -DPROJECT_BUILD_TESTS=ON 才会进入本目录。

# 测试目标名默认跟随项目名，例如 ExampleProject_tests。
set(PROJECT_TEST_TARGET "${PROJECT_NAME}_tests")

# 使用 Qt Test 后，可以取消下面内容的注释并添加测试源码：
#
# find_package(Qt${QT_VERSION_MAJOR}
#     REQUIRED
#     COMPONENTS Test
# )
#
# add_executable(${PROJECT_TEST_TARGET}
#     test_main.cpp
# )
#
# target_link_libraries(${PROJECT_TEST_TARGET}
#     PRIVATE
#         ${PROJECT_NAMESPACE}::ui
#         Qt${QT_VERSION_MAJOR}::Test
# )
#
# project_apply_compiler_options(${PROJECT_TEST_TARGET})
#
# add_test(
#     NAME ${PROJECT_TEST_TARGET}
#     COMMAND ${PROJECT_TEST_TARGET}
# )
