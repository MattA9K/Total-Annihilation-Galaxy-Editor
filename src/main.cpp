#include "core/EditorSettings.hpp"
#include "core/I18n.hpp"
#include "core/Version.hpp"
#include "platform/AppIcon.hpp"
#include "platform/FileDialog.hpp"
#include "platform/SystemAppearance.hpp"
#include "ui/EditorApp.hpp"
#include "ui/LanguagePicker.hpp"
#include "ui/Splash.hpp"
#include "ui/Theme.hpp"

#if defined(__APPLE__)
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
#endif

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace {

constexpr int kMainWidth = 1440;
constexpr int kMainHeight = 900;

void glfwError(int code, const char* description) {
    std::fprintf(stderr, "GLFW %d: %s\n", code, description);
}

void presentFrame(GLFWwindow* window, float red, float green, float blue) {
    ImGui::Render();
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    glViewport(0, 0, width, height);
    glClearColor(red, green, blue, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(window);
}

// Windows and Linux lay windows out in pixels, so the splash grows with the
// display scale. macOS lays them out in points and scales them itself.
float splashWindowScale() {
#if defined(__APPLE__)
    return 1.0f;
#else
    float scale = 1.0f;
    if (GLFWmonitor* monitor = glfwGetPrimaryMonitor()) {
        glfwGetMonitorContentScale(monitor, &scale, nullptr);
    }
    return std::max(scale, 1.0f);
#endif
}

void centerOnPrimaryMonitor(GLFWwindow* window, int width, int height, int frameTop) {
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    if (monitor == nullptr) {
        return;
    }
    int x = 0;
    int y = 0;
    int areaWidth = 0;
    int areaHeight = 0;
    glfwGetMonitorWorkarea(monitor, &x, &y, &areaWidth, &areaHeight);
    glfwSetWindowPos(window, x + std::max(0, (areaWidth - width) / 2), y + frameTop + std::max(0, (areaHeight - frameTop - height) / 2));
}

// Asks where Total Annihilation is installed when the editor does not know yet.
// This runs before the splash screen. Cancelling is fine: the editor starts
// without the game archives, and the folder can be set later in Options.
void askForGameFolder() {
    const std::filesystem::path settingsPath = galaxy::editorSettingsPath();
    galaxy::EditorSettings settings = galaxy::loadEditorSettings(settingsPath);
    if (!galaxy::shouldAskForGameFolder(settings)) {
        return;
    }
    std::filesystem::path start;
    std::error_code error;
    if (!settings.gameFolder.empty() && std::filesystem::is_directory(settings.gameFolder.parent_path(), error)) {
        start = settings.gameFolder.parent_path();
    }
    const std::string detail = settings.gameFolder.empty()
        ? galaxy::text(galaxy::Text::GameFolderDetail)
        : galaxy::formatText(galaxy::Text::GameFolderMissing, {galaxy::pathToUtf8(settings.gameFolder)});
    settings.gameFolderAsked = true;
    std::optional<std::filesystem::path> chosen;
    if (galaxy::alertDialog(
            galaxy::text(galaxy::Text::GameFolderMessage),
            detail,
            galaxy::text(galaxy::Text::Cancel),
            galaxy::text(galaxy::Text::FindTaInstall))) {
        chosen = galaxy::chooseFolderDialog(galaxy::text(galaxy::Text::SelectTaFolder), start);
    }
    if (chosen) {
        settings.gameFolder = *chosen;
    }
    galaxy::saveEditorSettings(settingsPath, settings);
}

// Shows the splash picture at once, then opens the tileset archives a few at a
// time so the bar moves with each file. Returns false if the window was closed.
bool runSplash(GLFWwindow* window, galaxy::SplashScreen& splash, galaxy::EditorApp& editor) {
    using Clock = std::chrono::steady_clock;
    constexpr auto kMinimumShown = std::chrono::milliseconds(700);
    constexpr auto kLoadBudget = std::chrono::milliseconds(24);

    splash.uploadTexture();
    editor.beginLoadingTilesets();
    const auto started = Clock::now();
    float shown = 0.0f;
    bool visible = false;

    while (true) {
        glfwPollEvents();
        if (glfwWindowShouldClose(window)) {
            return false;
        }

        const std::size_t total = editor.tilesetLoadTotal();
        const std::size_t done = editor.tilesetLoadDone();
        const float target = total == 0 ? 1.0f : static_cast<float>(done) / static_cast<float>(total);
        shown += (target - shown) * 0.4f;
        if (target - shown < 0.002f) {
            shown = target;
        }

        std::string status;
        std::string count;
        if (editor.loadingTilesets()) {
            status = galaxy::formatText(galaxy::Text::SplashLoading, {editor.tilesetLoadNextName()});
            count = galaxy::formatText(galaxy::Text::SplashCount, {std::to_string(done), std::to_string(total)});
        } else {
            status = galaxy::text(galaxy::Text::SplashReady);
            if (total > 0) {
                count = galaxy::formatText(galaxy::Text::SplashCount, {std::to_string(total), std::to_string(total)});
            }
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        splash.draw(shown, status, count);
        presentFrame(window, 0.0f, 0.0f, 0.0f);
        if (!visible) {
            // The first picture is already on screen when the window appears.
            glfwShowWindow(window);
            visible = true;
        }

        if (editor.loadingTilesets()) {
            editor.continueLoadingTilesets(kLoadBudget);
        } else if (shown >= 1.0f && Clock::now() - started >= kMinimumShown) {
            return true;
        }
    }
}

void initImGui(GLFWwindow* window) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // The Finder starts an app with / as its working directory, so window
    // positions are kept beside settings.json instead of in a relative imgui.ini.
    static std::string layoutFile;
    const std::filesystem::path settingsFolder = galaxy::editorSettingsPath().parent_path();
    std::error_code folderError;
    std::filesystem::create_directories(settingsFolder, folderError);
    layoutFile = galaxy::pathToUtf8(settingsFolder / "imgui.ini");
    io.IniFilename = layoutFile.c_str();

    float contentScale = 1.0f;
    glfwGetWindowContentScale(window, &contentScale, nullptr);
    galaxy::theme::setup(contentScale);
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 150");
}

void shutdownGraphics(GLFWwindow* window, GLFWcursor* crosshair, bool imguiReady) {
    if (imguiReady) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }
    if (crosshair != nullptr) {
        glfwDestroyCursor(crosshair);
    }
    if (window != nullptr) {
        glfwDestroyWindow(window);
    }
    glfwTerminate();
}

// The language list is its own window, shown before the splash picture is decoded
// and before any archive is opened. Closing it without a choice leaves no language
// saved, so the next launch asks again.
bool runLanguagePicker(GLFWwindow* window) {
    ImGuiIO& io = ImGui::GetIO();
    const ImGuiConfigFlags navigation = io.ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;

    galaxy::LanguagePickerState state;
    bool open = true;
    while (open && !state.confirm) {
        glfwPollEvents();
        if (glfwWindowShouldClose(window)) {
            open = false;
            break;
        }
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        galaxy::drawLanguagePicker(state);
        const ImVec4 background = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
        presentFrame(window, background.x, background.y, background.z);
    }
    io.ConfigFlags |= navigation;
    if (!state.confirm) {
        return false;
    }
    const galaxy::Language& chosen = galaxy::languageAt(state.selected);
    galaxy::EditorSettings settings = galaxy::loadEditorSettings(galaxy::editorSettingsPath());
    settings.language = chosen.id;
    galaxy::saveEditorSettings(galaxy::editorSettingsPath(), settings);
    galaxy::setActiveLanguage(chosen.id);
    return true;
}

GLFWwindow* createLanguageWindow() {
    const float scale = splashWindowScale();
    int width = static_cast<int>(480.0f * scale);
    int height = static_cast<int>(640.0f * scale);
    if (GLFWmonitor* monitor = glfwGetPrimaryMonitor()) {
        int x = 0;
        int y = 0;
        int areaWidth = 0;
        int areaHeight = 0;
        glfwGetMonitorWorkarea(monitor, &x, &y, &areaWidth, &areaHeight);
        if (areaWidth > 0 && areaHeight > 0) {
            width = std::clamp(width, 360, std::max(360, areaWidth - 48));
            height = std::clamp(height, 420, std::max(420, areaHeight - 48));
        }
    }
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
    glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(width, height, galaxy::applicationTitle().c_str(), nullptr, nullptr);
    if (window != nullptr) {
        centerOnPrimaryMonitor(window, width, height, 0);
    }
    return window;
}

// Turns the borderless splash window into the editor window.
void becomeEditorWindow(GLFWwindow* window) {
    glfwSetWindowAttrib(window, GLFW_DECORATED, GLFW_TRUE);
    glfwSetWindowAttrib(window, GLFW_RESIZABLE, GLFW_TRUE);
    int frameLeft = 0;
    int frameTop = 0;
    int frameRight = 0;
    int frameBottom = 0;
    glfwGetWindowFrameSize(window, &frameLeft, &frameTop, &frameRight, &frameBottom);

    int width = kMainWidth;
    int height = kMainHeight;
    if (GLFWmonitor* monitor = glfwGetPrimaryMonitor()) {
        int x = 0;
        int y = 0;
        int areaWidth = 0;
        int areaHeight = 0;
        glfwGetMonitorWorkarea(monitor, &x, &y, &areaWidth, &areaHeight);
        if (areaWidth > 0 && areaHeight > 0) {
            width = std::clamp(width, 640, std::max(640, areaWidth - frameLeft - frameRight));
            height = std::clamp(height, 480, std::max(480, areaHeight - frameTop - frameBottom));
        }
    }
    glfwSetWindowSize(window, width, height);
    centerOnPrimaryMonitor(window, width, height, frameTop);
}

}  // namespace

namespace {
// The editor that receives files dropped on the window.
galaxy::EditorApp* gDropTarget = nullptr;
}  // namespace

int main(int argc, char** argv) {
    glfwSetErrorCallback(glfwError);
    if (!glfwInit()) {
        std::fprintf(stderr, "Could not initialize GLFW\n");
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#if defined(__APPLE__)
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#elif !defined(_WIN32)
    // Lets the desktop match this window to the installed galaxy-editor.desktop entry.
    glfwWindowHintString(GLFW_X11_CLASS_NAME, "galaxy-editor");
    glfwWindowHintString(GLFW_X11_INSTANCE_NAME, "galaxy-editor");
    glfwWindowHintString(GLFW_WAYLAND_APP_ID, "galaxy-editor");
#endif

    const galaxy::EditorSettings saved = galaxy::loadEditorSettings(galaxy::editorSettingsPath());
    const bool needLanguage = !galaxy::isKnownLanguage(saved.language);
    if (!needLanguage) {
        galaxy::setActiveLanguage(saved.language);
    }

    GLFWwindow* window = nullptr;
    bool imguiReady = false;
    if (needLanguage) {
        window = createLanguageWindow();
        if (window == nullptr) {
            std::fprintf(stderr, "Could not create a window\n");
            glfwTerminate();
            return 1;
        }
        galaxy::applyApplicationIcon(window);
        glfwMakeContextCurrent(window);
        glfwSwapInterval(1);
        initImGui(window);
        imguiReady = true;
        const bool dark = saved.appearance == 2 || (saved.appearance == 0 && galaxy::systemPrefersDarkMode());
        galaxy::theme::setDark(dark);
        galaxy::applyNativeAppearance(saved.appearance);
        if (!runLanguagePicker(window)) {
            shutdownGraphics(window, nullptr, true);
            return 0;
        }
        glfwHideWindow(window);
    }

    // The install folder is asked after the language, still before any archive opens.
    askForGameFolder();

    // The splash picture and the HPI archives wait until a language is chosen.
    galaxy::SplashImage splashImage = galaxy::decodeSplashImage();
    const bool useSplash = splashImage.valid();
    auto splash = std::make_unique<galaxy::SplashScreen>(std::move(splashImage));

    if (window == nullptr) {
        int windowWidth = kMainWidth;
        int windowHeight = kMainHeight;
        if (useSplash) {
            const float scale = splashWindowScale();
            windowWidth = static_cast<int>(galaxy::SplashScreen::kWidth * scale);
            windowHeight = static_cast<int>(splash->windowHeight() * scale);
            glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
            glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
            glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
        }
        window = glfwCreateWindow(windowWidth, windowHeight, galaxy::applicationTitle().c_str(), nullptr, nullptr);
        if (window == nullptr) {
            std::fprintf(stderr, "Could not create a window\n");
            glfwTerminate();
            return 1;
        }
        galaxy::applyApplicationIcon(window);
        if (useSplash) {
            centerOnPrimaryMonitor(window, windowWidth, windowHeight, 0);
        }
        glfwMakeContextCurrent(window);
        glfwSwapInterval(1);
        initImGui(window);
        imguiReady = true;
    } else if (useSplash) {
        const float scale = splashWindowScale();
        const int windowWidth = static_cast<int>(galaxy::SplashScreen::kWidth * scale);
        const int windowHeight = static_cast<int>(splash->windowHeight() * scale);
        glfwSetWindowAttrib(window, GLFW_DECORATED, GLFW_FALSE);
        glfwSetWindowAttrib(window, GLFW_RESIZABLE, GLFW_FALSE);
        glfwSetWindowSize(window, windowWidth, windowHeight);
        centerOnPrimaryMonitor(window, windowWidth, windowHeight, 0);
    } else {
        becomeEditorWindow(window);
        glfwShowWindow(window);
    }

    GLFWcursor* crosshair = glfwCreateStandardCursor(GLFW_CROSSHAIR_CURSOR);
    galaxy::EditorApp editor;
    gDropTarget = &editor;
    glfwSetDropCallback(window, [](GLFWwindow*, int count, const char** paths) {
        if (gDropTarget == nullptr) {
            return;
        }
        std::vector<std::filesystem::path> dropped;
        for (int i = 0; i < count; ++i) {
            dropped.push_back(std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(paths[i]))));
        }
        gDropTarget->handleDroppedPaths(dropped);
    });
    bool running = true;
    if (useSplash) {
        running = runSplash(window, *splash, editor);
        // The picture's texture belongs to this context, so free it while the context is current.
        splash.reset();
        if (running) {
            becomeEditorWindow(window);
        }
    } else {
        editor.loadTilesets();
    }
    if (running && argc > 1) {
        editor.openMap(std::filesystem::path(argv[1]));
    }

    while (running && !glfwWindowShouldClose(window)) {
        glfwPollEvents();
        const bool closeRequested = glfwWindowShouldClose(window) != 0;
        if (closeRequested) {
            glfwSetWindowShouldClose(window, GLFW_FALSE);
        }
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        if (closeRequested) {
            editor.requestClose();
        }
        if (!editor.draw()) {
            break;
        }
        if (editor.selectionMode() && crosshair != nullptr) {
            ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
            glfwSetCursor(window, crosshair);
        } else if ((ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange) != 0) {
            ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
            glfwSetCursor(window, nullptr);
        }
        const ImVec4 background = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
        presentFrame(window, background.x, background.y, background.z);
    }

    shutdownGraphics(window, crosshair, imguiReady);
    return 0;
}
