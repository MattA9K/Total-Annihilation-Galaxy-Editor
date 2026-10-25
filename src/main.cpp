#include "ui/EditorApp.hpp"
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

#include <cstdio>
#include <filesystem>

namespace {

void glfwError(int code, const char* description) {
    std::fprintf(stderr, "GLFW %d: %s\n", code, description);
}

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
#endif

    GLFWwindow* window = glfwCreateWindow(1440, 900, "Galaxy Editor", nullptr, nullptr);
    GLFWcursor* crosshair = glfwCreateStandardCursor(GLFW_CROSSHAIR_CURSOR);
    if (window == nullptr) {
        std::fprintf(stderr, "Could not create a window\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    float contentScale = 1.0f;
    glfwGetWindowContentScale(window, &contentScale, nullptr);
    galaxy::theme::setup(contentScale);

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 150");

    galaxy::EditorApp editor;
    editor.loadTilesets();
    if (argc > 1) {
        editor.openMap(std::filesystem::path(argv[1]));
    }

    while (!glfwWindowShouldClose(window)) {
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
        ImGui::Render();

        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        glViewport(0, 0, width, height);
        glClearColor(0.925f, 0.925f, 0.925f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    if (crosshair != nullptr) {
        glfwDestroyCursor(crosshair);
    }
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
