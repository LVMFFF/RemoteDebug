cmake_minimum_required(VERSION 3.15)
project(RemoteDebug VERSION 1.0.0 LANGUAGES C CXX)

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

# 添加测试目录（如果存在）
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test/CMakeLists.txt")
    add_subdirectory(test)
endif()

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
message(STATUS "生成器: ${CMAKE_GENERATOR}")