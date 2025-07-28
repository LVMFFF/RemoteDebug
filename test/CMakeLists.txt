

add_executable(inject_shell
    inject_shell.cpp
)
target_link_libraries(inject_shell
    PRIVATE
        dl
)

add_library(patch SHARED patch.cpp)

add_executable(compile_source_test compile_source_test.cpp)
target_include_directories(compile_source_test
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/third_party
)