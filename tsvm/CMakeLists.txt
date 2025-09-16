cmake_minimum_required(VERSION 3.10)
project(tsvm C)

# 编译选项
set(CMAKE_C_FLAGS "-Wall -Wextra -Werror -std=c99")
set(CMAKE_C_FLAGS_DEBUG "-O0 -g")
set(CMAKE_C_FLAGS_RELEASE "-O3")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-unused-parameter -Wno-unused-function")

# 链接库
set(CMAKE_EXE_LINKER_FLAGS "-lm")

# 源文件
set(SOURCES
    main.c
    parser.c
    vmext.c
    jass/vm_main.c
    jass/vm_compiler.c
    jass/jass_parser.c
    jass/jass_array.c
    jass/jass_math.c
)

# 头文件路径
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/api)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/jass)

# 设置输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# 创建可执行文件
add_executable(tsvm ${SOURCES})

# 链接数学库
target_link_libraries(tsvm m)

# 自定义目标：运行程序
add_custom_target(run 
    COMMAND ${CMAKE_BINARY_DIR}/tsvm ${ARGS}
    DEPENDS tsvm
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

# 设置包含目录
target_include_directories(tsvm PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/api
    ${CMAKE_CURRENT_SOURCE_DIR}/jass
)

# 输出编译命令信息
message(STATUS "Compiler command JSON generation: ON")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C Compiler: ${CMAKE_C_COMPILER}")
message(STATUS "C Flags: ${CMAKE_C_FLAGS}")