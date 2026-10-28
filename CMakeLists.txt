cmake_minimum_required(VERSION 3.24)

# The release number lives in the VERSION file, one line such as 1.0.0. The
# project, the window title and the About box all take it from there.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" GALAXY_VERSION LIMIT_COUNT 1)
string(STRIP "${GALAXY_VERSION}" GALAXY_VERSION)
if(NOT GALAXY_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "VERSION must hold one number like 1.0.0, not \"${GALAXY_VERSION}\"")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")

project(GalaxyEditor VERSION ${GALAXY_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Windows has no system zlib, so a cross build downloads and builds its own.
find_package(ZLIB QUIET)
if(NOT ZLIB_FOUND)
    if(NOT WIN32)
        message(FATAL_ERROR "zlib was not found. Install its development package.")
    endif()
    include(FetchContent)
    FetchContent_Declare(
        zlib
        GIT_REPOSITORY https://github.com/madler/zlib.git
        GIT_TAG v1.3.1
        GIT_SHALLOW TRUE
    )
    set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(zlib)
    add_library(ZLIB::ZLIB ALIAS zlibstatic)
    target_include_directories(zlibstatic INTERFACE "${zlib_SOURCE_DIR}" "${zlib_BINARY_DIR}")
endif()
find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)

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

# Only the version text lives in this generated file, so changing VERSION
# recompiles one small file.
set(VERSION_CPP "${CMAKE_BINARY_DIR}/generated/Version.cpp")
file(WRITE "${VERSION_CPP}.new"
    "#include \"core/Version.hpp\"\n\nnamespace galaxy {\n\n"
    "const char* versionString() {\n    return \"${GALAXY_VERSION}\";\n}\n\n"
    "}  // namespace galaxy\n")
execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${VERSION_CPP}.new" "${VERSION_CPP}")

add_library(galaxy_format STATIC
    src/format/HpiArchive.cpp
    src/format/MapFile.cpp
    src/format/SctSection.cpp
    src/format/GafImage.cpp
    src/core/TilesetLibrary.cpp
    src/core/ArchiveDocument.cpp
    src/core/FavoritesArchive.cpp
    src/core/MapGenerator.cpp
    src/core/EditorSettings.cpp
    src/core/OrbitCamera.cpp
    src/core/Version.cpp
    "${VERSION_CPP}"
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

# Pictures the editor needs before it reads a single file (the splash and the
# window icon) are compiled into the executable. Reconfigure after replacing one.
set(EMBED_CPP "${CMAKE_BINARY_DIR}/generated/EmbeddedImages.cpp")
set(EMBED_BODY "#include \"ui/EmbeddedData.hpp\"\n\nnamespace galaxy {\n\n")
function(embed_image SYMBOL FILE)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${FILE}")
    if(EXISTS "${FILE}")
        file(READ "${FILE}" HEX HEX)
        string(LENGTH "${HEX}" HEX_LENGTH)
        math(EXPR SIZE "${HEX_LENGTH} / 2")
        string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," BYTES "${HEX}")
    else()
        message(WARNING "${FILE} is missing. The editor will build without it.")
        set(BYTES "0x00,")
        set(SIZE 0)
    endif()
    set(EMBED_BODY "${EMBED_BODY}const unsigned char k${SYMBOL}Data[] = {${BYTES}};\nconst unsigned long k${SYMBOL}Size = ${SIZE}UL;\n\n" PARENT_SCOPE)
endfunction()

embed_image(SplashImage "${CMAKE_SOURCE_DIR}/src/SplashScreen.png")
embed_image(WindowIcon256 "${CMAKE_SOURCE_DIR}/assets/icons/window-256.png")
embed_image(WindowIcon64 "${CMAKE_SOURCE_DIR}/assets/icons/window-64.png")
embed_image(WindowIcon32 "${CMAKE_SOURCE_DIR}/assets/icons/window-32.png")
embed_image(DockIcon "${CMAKE_SOURCE_DIR}/assets/icons/dock-512.png")
file(WRITE "${EMBED_CPP}.new" "${EMBED_BODY}}  // namespace galaxy\n")
execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${EMBED_CPP}.new" "${EMBED_CPP}")

# stb_image is third-party code, so it is built without the project warnings.
set_source_files_properties(src/ui/StbImage.cpp "${EMBED_CPP}" PROPERTIES COMPILE_OPTIONS "-w")

add_executable(GalaxyEditor
    src/main.cpp
    src/ui/EditorApp.cpp
    src/ui/HpiEditor.cpp
    src/ui/Theme.cpp
    src/ui/ToolbarIcons.cpp
    src/ui/GlassBlur.cpp
    src/ui/Terrain3D.cpp
    src/ui/Splash.cpp
    src/ui/StbImage.cpp
    "${EMBED_CPP}")
target_link_libraries(GalaxyEditor PRIVATE galaxy_format imgui Threads::Threads)
target_compile_options(GalaxyEditor PRIVATE -Wall -Wextra -Wpedantic)

if(APPLE)
    target_sources(GalaxyEditor PRIVATE src/platform/FileDialog_mac.mm src/platform/AppIcon_mac.mm src/platform/SystemAppearance_mac.mm)
    target_link_libraries(GalaxyEditor PRIVATE "-framework Cocoa" "-framework UniformTypeIdentifiers")
    # The build produces GalaxyEditor.app. Its version and name come from the
    # generated Info.plist, and the Finder takes the icon from the bundle.
    set(MAC_PLIST "${CMAKE_BINARY_DIR}/generated/Info.plist")
    configure_file(assets/macos/Info.plist.in "${MAC_PLIST}" @ONLY)
    set(MAC_ICON "${CMAKE_SOURCE_DIR}/assets/icons/AppIcon.icns")
    set_target_properties(GalaxyEditor PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${MAC_PLIST}")
    # Relink, and so run the steps below again, when the icon or the plist changes.
    set_property(TARGET GalaxyEditor APPEND PROPERTY LINK_DEPENDS "${MAC_ICON}" "${MAC_PLIST}")
    # Put the icon in the bundle, then sign the whole bundle. The linker only signs
    # the bare executable. Signing last gives the signature the bundle identifier and
    # makes it cover the Info.plist and the icon.
    add_custom_command(TARGET GalaxyEditor POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_BUNDLE_CONTENT_DIR:GalaxyEditor>/Resources"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${MAC_ICON}" "$<TARGET_BUNDLE_CONTENT_DIR:GalaxyEditor>/Resources/AppIcon.icns"
        VERBATIM)
    find_program(CODESIGN codesign)
    if(CODESIGN)
        add_custom_command(TARGET GalaxyEditor POST_BUILD
            COMMAND "${CODESIGN}" --force --sign - "$<TARGET_BUNDLE_DIR:GalaxyEditor>"
            VERBATIM)
    endif()
elseif(WIN32)
    target_sources(GalaxyEditor PRIVATE src/platform/FileDialog_win.cpp src/platform/AppIcon.cpp src/platform/SystemAppearance_win.cpp)
    target_link_libraries(GalaxyEditor PRIVATE comdlg32 advapi32 ole32 shell32 uuid)
    # A window program, not a console one, so no black console window opens beside it.
    # main() stays the entry point.
    if(MSVC)
        target_link_options(GalaxyEditor PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
    else()
        target_link_options(GalaxyEditor PRIVATE -mwindows)
    endif()
    # MinGW builds carry their runtime inside the .exe, so it runs on a machine
    # with nothing else installed.
    if(MINGW)
        target_link_options(GalaxyEditor PRIVATE -static -static-libgcc -static-libstdc++ $<$<CONFIG:Release>:-s>)
    endif()
    # The icon shown in Explorer and on the taskbar comes from the executable itself,
    # and so do the version shown in its Properties and, for MinGW, the manifest.
    enable_language(RC)
    set(WIN_RC "${CMAKE_BINARY_DIR}/generated/GalaxyEditor.rc")
    set(WIN_ICON "${CMAKE_SOURCE_DIR}/assets/icons/AppIcon.ico")
    set(WIN_MANIFEST_IN "${CMAKE_SOURCE_DIR}/assets/windows/GalaxyEditor.manifest.in")
    set(WIN_MANIFEST "${CMAKE_BINARY_DIR}/generated/GalaxyEditor.manifest")
    set(WIN_RC_TEXT "#include <winver.h>\n")
    string(APPEND WIN_RC_TEXT
        "VS_VERSION_INFO VERSIONINFO\n"
        "FILEVERSION ${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},0\n"
        "PRODUCTVERSION ${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},0\n"
        "FILEOS VOS_NT_WINDOWS32\n"
        "FILETYPE VFT_APP\n"
        "BEGIN\n"
        "  BLOCK \"StringFileInfo\"\n"
        "  BEGIN\n"
        "    BLOCK \"040904B0\"\n"
        "    BEGIN\n"
        "      VALUE \"FileDescription\", \"Galaxy Editor\"\n"
        "      VALUE \"FileVersion\", \"${PROJECT_VERSION}\"\n"
        "      VALUE \"ProductName\", \"Galaxy Editor\"\n"
        "      VALUE \"ProductVersion\", \"${PROJECT_VERSION}\"\n"
        "      VALUE \"OriginalFilename\", \"GalaxyEditor.exe\"\n"
        "    END\n"
        "  END\n"
        "  BLOCK \"VarFileInfo\"\n"
        "  BEGIN\n"
        "    VALUE \"Translation\", 0x409, 1200\n"
        "  END\n"
        "END\n")
    if(EXISTS "${WIN_ICON}")
        string(APPEND WIN_RC_TEXT "1 ICON \"${WIN_ICON}\"\n")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${WIN_ICON}")
    else()
        message(WARNING "${WIN_ICON} is missing. The editor will build without its Explorer icon.")
    endif()
    if(MINGW)
        # MinGW does not make a manifest, so it goes in as a resource (1 is the id of an
        # executable's manifest, 24 the manifest resource type). The Microsoft linker
        # makes its own, and FileDialog_win.cpp asks for the same controls with a linker comment.
        # The manifest also turns on the UTF-8 code page and long paths.
        configure_file("${WIN_MANIFEST_IN}" "${WIN_MANIFEST}" @ONLY)
        string(APPEND WIN_RC_TEXT "1 24 \"${WIN_MANIFEST}\"\n")
    endif()
    file(WRITE "${WIN_RC}" "${WIN_RC_TEXT}")
    target_sources(GalaxyEditor PRIVATE "${WIN_RC}")
else()
    target_sources(GalaxyEditor PRIVATE src/platform/FileDialog_posix.cpp src/platform/AppIcon.cpp src/platform/SystemAppearance_posix.cpp)
    # Desktop environments take the launcher icon from an installed desktop entry.
    install(TARGETS GalaxyEditor RUNTIME DESTINATION bin)
    install(FILES assets/linux/galaxy-editor.desktop DESTINATION share/applications)
    install(FILES assets/icons/AppIcon.png DESTINATION share/icons/hicolor/512x512/apps RENAME galaxy-editor.png)
endif()

enable_testing()
add_test(NAME format COMMAND galaxy_tests)
