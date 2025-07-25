cmake_minimum_required(VERSION 3.15)
project(RemoteDebug VERSION 1.0.0 LANGUAGES C CXX)

# ======================================
# 强制指定 MinGW 工具链路径
# 解决 "no such file or directory" 错误
# ======================================

# 设置 MinGW 路径 - 与 build.sh 中相同
set(MINGW_PATH "D:/chrome/x86_64-13.2.0-release-posix-seh-ucrt-rt_v11-rev0/mingw64")

# 显式指定编译器路径
set(CMAKE_C_COMPILER "${MINGW_PATH}/bin/gcc.exe")
set(CMAKE_CXX_COMPILER "${MINGW_PATH}/bin/g++.exe")
set(CMAKE_MAKE_PROGRAM "${MINGW_PATH}/bin/mingw32-make.exe")

# 禁用编译器检测（解决 broken compiler 错误）
set(CMAKE_C_COMPILER_WORKS 1)
set(CMAKE_CXX_COMPILER_WORKS 1)

# ======================================
# 项目基本配置
# ======================================

# 设置默认构建类型
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "Build type" FORCE)
endif()

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ======================================
# 编译选项配置
# ======================================

# 全局编译警告选项
add_compile_options(
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wconversion
    -fdiagnostics-color=always
)

# 构建类型特定选项
string(TOUPPER "${CMAKE_BUILD_TYPE}" BUILD_TYPE_UPPER)
if(BUILD_TYPE_UPPER STREQUAL "DEBUG")
    message(STATUS "配置调试构建")
    add_compile_options(-O0 -g3 -Werror)
    add_definitions(-DDEBUG)
elseif(BUILD_TYPE_UPPER STREQUAL "RELEASE")
    message(STATUS "配置发布构建")
    add_compile_options(-O3 -flto -march=native)
    add_definitions(-DNDEBUG)
elseif(BUILD_TYPE_UPPER STREQUAL "RELWITHDEBINFO")
    message(STATUS "配置带调试信息的发布构建")
    add_compile_options(-O2 -g)
endif()

# ======================================
# 可执行文件配置
# ======================================

add_executable(${PROJECT_NAME}
    src/main.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# ======================================
# 安装和测试配置
# ======================================

# 安装规则
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

# 测试配置
enable_testing()
add_test(NAME BasicTest COMMAND ${PROJECT_NAME})

# ======================================
# 跨平台支持
# ======================================

if(WIN32)
    target_compile_definitions(${PROJECT_NAME} PRIVATE OS_WINDOWS)
    target_link_libraries(${PROJECT_NAME} PRIVATE ws2_32)  # Windows 网络库
elseif(UNIX)
    target_compile_definitions(${PROJECT_NAME} PRIVATE OS_LINUX)
    target_link_libraries(${PROJECT_NAME} PRIVATE pthread)
endif()

# ======================================
# 构建后信息
# ======================================

message(STATUS "项目名称: ${PROJECT_NAME}")
message(STATUS "构建类型: ${CMAKE_BUILD_TYPE}")
message(STATUS "C 编译器: ${CMAKE_C_COMPILER}")
message(STATUS "C++ 编译器: ${CMAKE_CXX_COMPILER}")
message(STATUS "Make 程序: ${CMAKE_MAKE_PROGRAM}")