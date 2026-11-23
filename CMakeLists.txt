cmake_minimum_required(VERSION 3.14)
project(memory_manager LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(BUILD_GUI   "Build the Raylib GUI application" ON)
option(BUILD_TESTS "Build the unit tests"             ON)

# ---------------------------------------------------------------------------
# Core allocator library (no Raylib dependency)
# ---------------------------------------------------------------------------
add_library(memcore STATIC src/memory_manager.c)
target_include_directories(memcore PUBLIC src)

if(MSVC)
    target_compile_options(memcore PRIVATE /W4)
    target_compile_definitions(memcore PUBLIC _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(memcore PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------------------
# Raylib GUI
# ---------------------------------------------------------------------------
if(BUILD_GUI)
    # Use an installed Raylib if there is one, otherwise download and build it.
    find_package(raylib 5.0 QUIET)
    if(NOT raylib_FOUND)
        include(FetchContent)
        set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(BUILD_GAMES    OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(
                raylib
                GIT_REPOSITORY https://github.com/raysan5/raylib.git
                GIT_TAG        5.5
                GIT_SHALLOW    TRUE
        )
        FetchContent_MakeAvailable(raylib)
    endif()

    # Change the executable name here only (must match your IDE run configuration)
    set(APP_NAME Memory_Management_System__Free_List_)

    add_executable(${APP_NAME} src/main.c)
    target_link_libraries(${APP_NAME} PRIVATE memcore raylib)

    # Fonts: found next to the executable, in the working directory, or here
    target_compile_definitions(${APP_NAME} PRIVATE ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
    add_custom_command(TARGET ${APP_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:${APP_NAME}>/assets")

    if(MSVC)
        target_compile_options(${APP_NAME} PRIVATE /W4)
    else()
        target_compile_options(${APP_NAME} PRIVATE -Wall -Wextra)
    endif()
endif()

# ---------------------------------------------------------------------------
# Tests (run with: ctest --output-on-failure)
# ---------------------------------------------------------------------------
if(BUILD_TESTS)
    enable_testing()
    add_executable(test_memory tests/test_memory.c)
    target_link_libraries(test_memory PRIVATE memcore)
    add_test(NAME memory_manager_tests COMMAND test_memory)
endif()