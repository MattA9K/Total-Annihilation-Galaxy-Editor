cmake_minimum_required(VERSION 3.24)

project(GalaxyEditor VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(ZLIB REQUIRED)
find_package(OpenGL REQUIRED)

include(FetchContent)
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_SHALLOW TRUE
)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# ImGui is not a CMake project. Download the tag once into the build tree.
set(IMGUI_SRC "${CMAKE_BINARY_DIR}/_deps/imgui-src")
if(NOT EXISTS "${IMGUI_SRC}/imgui.h")
    set(IMGUI_ARCHIVE "${CMAKE_BINARY_DIR}/_deps/imgui-v1.91.8.tar.gz")
    file(DOWNLOAD
        https://github.com/ocornut/imgui/archive/refs/tags/v1.91.8.tar.gz
        "${IMGUI_ARCHIVE}"
        SHOW_PROGRESS
        TLS_VERIFY ON
        STATUS IMGUI_DOWNLOAD_STATUS
    )
    list(GET IMGUI_DOWNLOAD_STATUS 0 IMGUI_DOWNLOAD_CODE)
    if(NOT IMGUI_DOWNLOAD_CODE EQUAL 0)
        list(GET IMGUI_DOWNLOAD_STATUS 1 IMGUI_DOWNLOAD_MESSAGE)
        message(FATAL_ERROR "Failed to download Dear ImGui: ${IMGUI_DOWNLOAD_MESSAGE}")
    endif()
    file(ARCHIVE_EXTRACT INPUT "${IMGUI_ARCHIVE}" DESTINATION "${CMAKE_BINARY_DIR}/_deps")
    file(RENAME "${CMAKE_BINARY_DIR}/_deps/imgui-1.91.8" "${IMGUI_SRC}")
endif()

add_library(imgui STATIC
    "${IMGUI_SRC}/imgui.cpp"
    "${IMGUI_SRC}/imgui_draw.cpp"
    "${IMGUI_SRC}/imgui_tables.cpp"
    "${IMGUI_SRC}/imgui_widgets.cpp"
    "${IMGUI_SRC}/backends/imgui_impl_glfw.cpp"
    "${IMGUI_SRC}/backends/imgui_impl_opengl3.cpp"
)
target_include_directories(imgui PUBLIC "${IMGUI_SRC}" "${IMGUI_SRC}/backends")
target_link_libraries(imgui PUBLIC glfw OpenGL::GL)

add_library(galaxy_format STATIC
    src/format/HpiArchive.cpp
    src/format/MapFile.cpp
    src/format/SctSection.cpp
    src/format/GafImage.cpp
    src/core/TilesetLibrary.cpp
    src/core/MapGenerator.cpp
    src/core/EditorSettings.cpp
)
target_include_directories(galaxy_format PUBLIC src)
target_link_libraries(galaxy_format PUBLIC ZLIB::ZLIB)
target_compile_definitions(galaxy_format PUBLIC GALAXY_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_compile_options(galaxy_format PRIVATE -Wall -Wextra -Wpedantic)

add_executable(galaxy_tests src/tests/format_test.cpp)
target_link_libraries(galaxy_tests PRIVATE galaxy_format)
target_compile_definitions(galaxy_tests PRIVATE
    GALAXY_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    GALAXY_EXAMPLE_MAPS="${CMAKE_SOURCE_DIR}/../ExampleMaps"
)
target_compile_options(galaxy_tests PRIVATE -Wall -Wextra -Wpedantic)

add_executable(GalaxyEditor src/main.cpp src/ui/EditorApp.cpp src/ui/Theme.cpp)
target_link_libraries(GalaxyEditor PRIVATE galaxy_format imgui)
target_compile_options(GalaxyEditor PRIVATE -Wall -Wextra -Wpedantic)

if(APPLE)
    target_sources(GalaxyEditor PRIVATE src/platform/FileDialog_mac.mm)
    target_link_libraries(GalaxyEditor PRIVATE "-framework Cocoa" "-framework UniformTypeIdentifiers")
elseif(WIN32)
    target_sources(GalaxyEditor PRIVATE src/platform/FileDialog_win.cpp)
    target_link_libraries(GalaxyEditor PRIVATE comdlg32)
else()
    target_sources(GalaxyEditor PRIVATE src/platform/FileDialog_posix.cpp)
endif()

enable_testing()
add_test(NAME format COMMAND galaxy_tests)
