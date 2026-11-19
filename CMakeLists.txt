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

add_executable(vellum src/main.c)
target_include_directories(vellum PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(vellum PRIVATE ${VELLUM_WARNINGS} ${VELLUM_SANITIZE_FLAGS})
target_link_libraries(vellum PRIVATE SDL3::SDL3)
target_link_options(vellum PRIVATE ${VELLUM_SANITIZE_FLAGS})
