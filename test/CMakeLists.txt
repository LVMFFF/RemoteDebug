

add_executable(inject_shell
    inject_shell.cpp
)
target_link_libraries(inject_shell
    PRIVATE
        dl
)

add_library(patch SHARED
    patch.cpp
)