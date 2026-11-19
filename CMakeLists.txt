# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2026 Roland Yegon

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

# ICU provides strict UTF-8 decoding and Unicode grapheme-cluster rules. The
# pkg-config module name is `icu-uc`; ICU's CMake package was not needed here.
find_package(PkgConfig REQUIRED)
pkg_check_modules(ICU_UC REQUIRED IMPORTED_TARGET icu-uc)

pkg_check_modules(FREETYPE2 REQUIRED IMPORTED_TARGET freetype2)
pkg_check_modules(HARFBUZZ REQUIRED IMPORTED_TARGET harfbuzz)

enable_testing()

# Shared build settings. Linking this interface target applies both warnings and
# sanitizer flags to every first-party target.
add_library(vellum_build_flags INTERFACE)
target_compile_features(vellum_build_flags INTERFACE c_std_17)
target_compile_definitions(vellum_build_flags INTERFACE
    VELLUM_FONT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts"
    VELLUM_CONFIG_DIR="${CMAKE_CURRENT_SOURCE_DIR}/config")
target_compile_options(vellum_build_flags INTERFACE ${VELLUM_WARNINGS} ${VELLUM_SANITIZE_FLAGS})
target_link_options(vellum_build_flags INTERFACE ${VELLUM_SANITIZE_FLAGS})
target_link_libraries(vellum_build_flags INTERFACE SDL3::SDL3)

# Vendored yyjson is third-party code: keep Vellum's strict warnings away from it,
# but keep sanitizer instrumentation so config parsing is checked in debug builds.
add_library(yyjson_vendored STATIC third_party/yyjson/yyjson.c)
target_include_directories(yyjson_vendored PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/yyjson)
target_compile_options(yyjson_vendored PRIVATE ${VELLUM_SANITIZE_FLAGS})

add_library(vellum_core STATIC
    src/core/arena.c
    src/core/array.c
    src/core/error.c
    src/core/log.c
    src/core/queue.c
    src/core/trace.c)
target_include_directories(vellum_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(vellum_core PUBLIC vellum_build_flags)

add_library(vellum_text STATIC
    src/text/unicode.c
    src/text/buffer.c
    src/text/line_index.c
    src/text/command.c
    src/text/cursor.c
    src/text/cursor_set.c
    src/text/edit.c
    src/text/scroll.c
)
target_link_libraries(vellum_text PUBLIC vellum_core vellum_build_flags PkgConfig::ICU_UC)

add_library(vellum_graphics STATIC
    src/graphics/draw_list.c
    src/graphics/font.c
    src/graphics/shape.c
    src/graphics/text_layout.c
    src/graphics/line_cache.c
    src/graphics/renderer_sdl.c)
target_link_libraries(vellum_graphics PUBLIC
    vellum_core
    vellum_text
    vellum_build_flags
    PkgConfig::FREETYPE2
    PkgConfig::HARFBUZZ)

add_library(vellum_config STATIC
    src/config/config.c
    src/config/file_watcher.c
    src/config/json_util.c
    src/config/theme.c)
target_link_libraries(vellum_config PUBLIC
    vellum_core
    yyjson_vendored
    vellum_build_flags)

add_library(vellum_platform STATIC
    src/platform/clipboard.c
    src/platform/events.c
    src/platform/paths.c
    src/platform/signals.c
    src/platform/window.c)
target_link_libraries(vellum_platform PUBLIC vellum_core vellum_build_flags)

add_library(vellum_ui STATIC
    src/ui/widget.c
    src/ui/panels.c)
target_link_libraries(vellum_ui PUBLIC
    vellum_graphics
    vellum_config
    vellum_platform
    vellum_core
    vellum_build_flags)

add_library(vellum_app STATIC src/app.c)
target_link_libraries(vellum_app PUBLIC
    vellum_ui
    vellum_platform
    vellum_graphics
    vellum_config
    vellum_core
    vellum_build_flags)

add_executable(vellum src/main.c)
target_link_libraries(vellum PRIVATE vellum_app vellum_build_flags)

add_executable(vellum_unit_tests
    tests/unit/test_main.c
    tests/unit/test_error.c
    tests/unit/test_log.c
    tests/unit/test_arena.c
    tests/unit/test_array.c
    tests/unit/test_queue.c
    tests/unit/test_trace.c
    tests/unit/test_draw_list.c
    tests/unit/test_events.c
    tests/unit/test_unicode.c
    tests/unit/test_buffer.c
    tests/unit/test_line_index.c
    tests/unit/test_command.c
    tests/unit/test_cursor.c
    tests/unit/test_cursor_set.c
    tests/unit/test_edit.c
    tests/unit/test_clipboard.c
    tests/unit/test_scroll.c
    tests/unit/test_font.c
    tests/unit/test_shape.c
    tests/unit/test_layout.c
    tests/unit/test_line_cache.c
    tests/unit/test_widget.c
    tests/unit/test_panels.c
    tests/unit/test_config.c
    tests/unit/test_theme.c
    tests/unit/test_paths.c)
target_include_directories(vellum_unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vellum_unit_tests PRIVATE
    vellum_ui
    vellum_platform
    vellum_graphics
    vellum_config
    vellum_text
    vellum_core
    vellum_build_flags)
add_test(NAME unit COMMAND vellum_unit_tests)

add_executable(vellum_bench_draw_list
    tests/benchmarks/bench_main.c
    tests/benchmarks/bench_draw_list.c)
target_include_directories(vellum_bench_draw_list PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vellum_bench_draw_list PRIVATE vellum_graphics vellum_core vellum_build_flags)

add_executable(vellum_bench_text_frame
    tests/benchmarks/bench_text_main.c
    tests/benchmarks/bench_text_frame.c)
target_include_directories(vellum_bench_text_frame PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vellum_bench_text_frame PRIVATE
    vellum_platform
    vellum_graphics
    vellum_config
    vellum_core
    vellum_build_flags
    m)

add_executable(vellum_bench_text_buffer
    tests/benchmarks/bench_text_buffer_main.c
    tests/benchmarks/bench_text_buffer.c)
target_include_directories(vellum_bench_text_buffer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vellum_bench_text_buffer PRIVATE
    vellum_text
    vellum_core
    vellum_build_flags)

add_executable(vellum_integration_tests tests/integration/test_app_events.c)
target_link_libraries(vellum_integration_tests PRIVATE vellum_app vellum_build_flags)
add_test(NAME app_events COMMAND vellum_integration_tests)

add_executable(vellum_renderer_texture_tests tests/integration/test_renderer_textures.c)
target_link_libraries(vellum_renderer_texture_tests PRIVATE
    vellum_graphics
    vellum_core
    vellum_build_flags)
add_test(NAME renderer_textures COMMAND vellum_renderer_texture_tests)

add_executable(vellum_file_watcher_tests tests/integration/test_file_watcher.c)
target_link_libraries(vellum_file_watcher_tests PRIVATE
    vellum_config
    vellum_core
    vellum_build_flags)
add_test(NAME file_watcher COMMAND vellum_file_watcher_tests)
