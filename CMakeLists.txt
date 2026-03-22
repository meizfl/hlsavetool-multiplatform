cmake_minimum_required(VERSION 3.15)
project(hlsavetool C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Sources
add_executable(hlsaves
    src/hlsaves.c
    src/oodle.c
)

target_include_directories(hlsaves PRIVATE headers)

# Warnings
if(MSVC)
    target_compile_options(hlsaves PRIVATE /W4)
else()
    target_compile_options(hlsaves PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Platform linking
if(UNIX)
    # dlopen lives in libdl on Linux, built-in on macOS
    find_library(DL_LIB dl)
    if(DL_LIB)
        target_link_libraries(hlsaves PRIVATE ${DL_LIB})
    endif()
endif()

# Install
install(TARGETS hlsaves DESTINATION bin)
