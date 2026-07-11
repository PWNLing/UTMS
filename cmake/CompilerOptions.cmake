# =========================================================
# 统一编译选项
# =========================================================
# 用法：
# project_apply_compiler_options(target_name)
#
# 每创建一个可执行程序或库目标后调用此函数，可以避免在多个
# CMakeLists.txt 中重复编写相同的警告选项。
function(project_apply_compiler_options target_name)
    # 提前检查目标是否存在，避免目标名拼写错误时产生难懂的报错。
    if(NOT TARGET ${target_name})
        message(FATAL_ERROR
            "project_apply_compiler_options: target '${target_name}' does not exist"
        )
    endif()

    if(MSVC)
        # Microsoft Visual C++：
        # /W4          开启较严格的警告级别
        # /permissive- 提高标准 C++ 一致性
        # /utf-8       源文件和执行字符集使用 UTF-8
        target_compile_options(${target_name}
            PRIVATE
                /W4
                /permissive-
                /utf-8
        )
    else()
        # GCC / Clang：开启常用且较严格的编译警告。
        target_compile_options(${target_name}
            PRIVATE
                -Wall
                -Wextra
                -Wpedantic
        )
    endif()
endfunction()
