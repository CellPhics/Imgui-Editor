// Minimal GLFW + OpenGL3 host for an exported menu.
// Press Insert to show/hide the menu.
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "menu_generated.h"
#include <GLFW/glfw3.h>
#include <cstdio>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#ifndef ASSET_DIR
#define ASSET_DIR "assets"
#endif

static ImTextureID LoadTexture(const char* path, int* w, int* h)
{
    int n = 0;
    unsigned char* px = stbi_load(path, w, h, &n, 4);
    if (!px) { fprintf(stderr, "Couldn't load %s\n", path); return ImTextureID_Invalid; }
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, *w, *h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    stbi_image_free(px);
    return (ImTextureID)(intptr_t)tex;
}

int main(int argc, char** argv)
{
    const char* assets = argc > 1 ? argv[1] : ASSET_DIR;
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* window = glfwCreateWindow(1280, 800, "Menu preview", nullptr, nullptr);
    if (!window) return 1;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    menu::LoadFonts(assets);
    menu::LoadImages(assets, LoadTexture);
    menu::ApplyStyle();
    menu::State state;
    bool show = true;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        if (ImGui::IsKeyPressed(ImGuiKey_Insert, false)) show = !show;
        if (show) menu::Render(state);
        ImGui::Render();
        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.18f, 0.20f, 0.24f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwTerminate();
    return 0;
}
