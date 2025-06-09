cmake_minimum_required(VERSION 3.14)
project(RemoteDebug VERSION 1.0.0 DESCRIPTION "Remote Debug Tool")

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)  # 保证通用性

add_compile_options(-fstack-protector-strong -D_FORTIFY_SOURCE=2)

# 设置构建类型
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# 生成编译命令数据库（用于clangd等工具）
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ====================== 主项目库 ====================== #
# 创建静态库包含所有核心功能
set(LIB_SOURCES
    src/transmit.cpp
)

add_library(hotpatcher_lib STATIC ${LIB_SOURCES})
target_compile_options(hotpatcher_lib PRIVATE 
    -Wall
    -Wextra
    -Wpedantic
    -Werror
)

# 库包含目录
target_include_directories(hotpatcher_lib PUBLIC 
    include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# 库链接
target_link_libraries(hotpatcher_lib PUBLIC 
    dl
    ${CMAKE_THREAD_LIBS_INIT}
)

# ====================== 主可执行文件 ====================== #
add_executable(hotpatcher src/main.cpp)

# 主可执行文件严格编译选项
target_compile_options(hotpatcher PRIVATE 
    -Wall
    -Wextra
    -Wpedantic
    -Werror
)

# 链接主库
target_link_libraries(hotpatcher PRIVATE hotpatcher_lib)

# 安装目标
install(TARGETS hotpatcher 
    RUNTIME DESTINATION bin
    COMPONENT runtime
)

# 安装文档
install(FILES Readme.md 
    DESTINATION share/doc/hotpatcher
    COMPONENT documentation
)

# ====================== 测试配置 ====================== #
option(ENABLE_TESTING "Enable testing" ON)

if(ENABLE_TESTING)
    # 启用测试
    enable_testing()
    
    # 查找GoogleTest
    find_package(GTest REQUIRED)
    message(STATUS "Found GTest: ${GTEST_LIBRARIES}")
    
    # 测试源文件
    set(TEST_SOURCES
        test/test_architecture.cpp
    )
    
    
    # 创建测试可执行文件
    add_executable(hotpatcher_tests ${TEST_SOURCES})
    
    # 测试目标宽松编译选项（无-Werror）
    target_compile_options(hotpatcher_tests PRIVATE 
        -Wall
        -Wextra
        -Wpedantic
        # 注意：这里没有 -Werror
    )
    
    # 测试包含目录
    target_include_directories(hotpatcher_tests PRIVATE
        include
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${GTEST_INCLUDE_DIRS}
    )
    
    # 测试链接库
    target_link_libraries(hotpatcher_tests
        PRIVATE
            hotpatcher_lib  # 链接核心库
            GTest::gtest 
            GTest::gtest_main
            ${CMAKE_THREAD_LIBS_INIT}
            dl
    )
    
    # 添加测试目标
    add_test(NAME hotpatcher_tests COMMAND hotpatcher_tests)
    
    # 覆盖率支持
    option(ENABLE_COVERAGE "Enable code coverage" OFF)
    if(ENABLE_COVERAGE)
        message(STATUS "Enabling code coverage")
        target_compile_options(hotpatcher_tests PRIVATE --coverage -fprofile-arcs -ftest-coverage)
        target_link_options(hotpatcher_tests PRIVATE --coverage)
    endif()
    
    # 添加测试构建目标
    add_custom_target(run_tests
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Running tests..."
        DEPENDS hotpatcher_tests
    )
endif()

# ====================== 实用功能 ====================== #
# 添加格式目标
find_program(CLANG_FORMAT "clang-format")
if(CLANG_FORMAT)
    add_custom_target(format
        COMMAND clang-format
        -i
        --style=file
        ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp
        COMMENT "Formatting source files..."
    )
endif()

# 添加清理目标
add_custom_target(clean_all
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}
    COMMENT "Removing build directory..."
)

# 添加运行目标
add_custom_target(run
    COMMAND ./hotpatcher
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DEPENDS hotpatcher
    COMMENT "Running hotpatcher..."
)

# 添加覆盖率报告目标
if(ENABLE_COVERAGE AND ENABLE_TESTING)
    add_custom_target(coverage_report
        COMMAND lcov --capture --directory . --output-file coverage.info
        COMMAND lcov --remove coverage.info '/usr/*' --output-file coverage.info
        COMMAND genhtml coverage.info --output-directory coverage_report
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Generating coverage report..."
        DEPENDS run_tests
    )
endif()