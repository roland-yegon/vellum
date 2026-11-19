cmake_minimum_required(VERSION 3.25)

project(
    vellum
    VERSION 0.1.0
    DESCRIPTION "Native code editor built with SDL3"
    LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(VELLUM_SANITIZERS "Build with AddressSanitizer and UBSan" OFF)
option(VELLUM_WERROR "Treat compiler warnings as errors" OFF)

set(VELLUM_WARNINGS
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wshadow
    -Wstrict-prototypes
    -Wmissing-prototypes
    -Wformat=2
    -Wnull-dereference
    -Wwrite-strings)

if(VELLUM_WERROR)
    list(APPEND VELLUM_WARNINGS -Werror)
endif()

if(VELLUM_SANITIZERS)
    set(VELLUM_SANITIZE_FLAGS -fsanitize=address,undefined -fno-omit-frame-pointer)
else()
    set(VELLUM_SANITIZE_FLAGS)
endif()

# The project's primary platform layer. Version 3.2.0 is the first release with
# SDL_RenderGeometry(), which the renderer backend is built around.
find_package(SDL3 3.2.0 CONFIG REQUIRED)

enable_testing()

# Shared build settings. Linking this interface target applies both warnings and
# sanitizer flags to every first-party target.
add_library(vellum_build_flags INTERFACE)
target_compile_features(vellum_build_flags INTERFACE c_std_17)
target_compile_options(vellum_build_flags INTERFACE ${VELLUM_WARNINGS} ${VELLUM_SANITIZE_FLAGS})
target_link_options(vellum_build_flags INTERFACE ${VELLUM_SANITIZE_FLAGS})
target_link_libraries(vellum_build_flags INTERFACE SDL3::SDL3)

add_library(vellum_core STATIC
    src/core/arena.c
    src/core/array.c
    src/core/error.c
    src/core/log.c
    src/core/queue.c
    src/core/trace.c)
target_include_directories(vellum_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(vellum_core PUBLIC vellum_build_flags)

add_library(vellum_graphics STATIC
    src/graphics/draw_list.c
    src/graphics/renderer_sdl.c)
target_link_libraries(vellum_graphics PUBLIC vellum_core vellum_build_flags)

add_executable(vellum src/main.c)
target_link_libraries(vellum PRIVATE vellum_graphics vellum_core vellum_build_flags)

add_executable(vellum_unit_tests
    tests/unit/test_main.c
    tests/unit/test_error.c
    tests/unit/test_log.c
    tests/unit/test_arena.c
    tests/unit/test_array.c
    tests/unit/test_queue.c
    tests/unit/test_trace.c
    tests/unit/test_draw_list.c)
target_include_directories(vellum_unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vellum_unit_tests PRIVATE vellum_graphics vellum_core vellum_build_flags)
add_test(NAME unit COMMAND vellum_unit_tests)

add_executable(vellum_bench_draw_list
    tests/benchmarks/bench_main.c
    tests/benchmarks/bench_draw_list.c)
target_include_directories(vellum_bench_draw_list PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vellum_bench_draw_list PRIVATE vellum_graphics vellum_core vellum_build_flags)
