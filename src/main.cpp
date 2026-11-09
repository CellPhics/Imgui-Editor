// ImGui Menu Editor — entry point.
//   imgui-menu-editor [project_dir]
#include "editor.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <unistd.h>
#include <vector>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

namespace fs = std::filesystem;

static std::string FindDataDir(const char* argv0)
{
    std::error_code ec;
    fs::path exe = fs::read_symlink("/proc/self/exe", ec);
    if (ec) exe = fs::absolute(argv0, ec);
    for (fs::path p : {exe.parent_path() / "data", exe.parent_path() / "../share/imgui-menu-editor", fs::path(MME_DATA_DIR)})
        if (fs::exists(p / "fonts" / "fa-solid-900.ttf", ec)) return fs::weakly_canonical(p, ec).string();
    return (exe.parent_path() / "data").string();
}

int RunTool(int argc, char** argv, const std::string& data_dir); // tools.cpp

static void GlfwError(int code, const char* msg)
{
    if (code == GLFW_FORMAT_UNAVAILABLE) return; // clipboard holds no text (e.g. an image): expected when pasting images
    fprintf(stderr, "GLFW error %d: %s\n", code, msg);
}

int main(int argc, char** argv)
{
    if (argc > 1 && std::string(argv[1]) == "tool") return RunTool(argc - 2, argv + 2, FindDataDir(argv[0]));

    // Args: [project_dir] [--export [out_dir]] [--screenshot out.png] [--frames N] [--size WxH] [--select id]
    std::string project, screenshot, select, export_dir, prompt;
    std::vector<std::string> attach;
    bool do_export = false;
    int frames = 45, width = 1600, height = 950;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--screenshot" && i + 1 < argc) screenshot = argv[++i];
        else if (a == "--frames" && i + 1 < argc) frames = atoi(argv[++i]);
        else if (a == "--size" && i + 1 < argc) sscanf(argv[++i], "%dx%d", &width, &height);
        else if (a == "--select" && i + 1 < argc) select = argv[++i];
        else if (a == "--prompt" && i + 1 < argc) prompt = argv[++i];
        else if (a == "--attach" && i + 1 < argc) attach.push_back(argv[++i]);
        else if (a == "--export") { do_export = true; if (i + 1 < argc && argv[i + 1][0] != '-') export_dir = argv[++i]; }
        else if (!a.empty() && a[0] == '-') { fprintf(stderr, "Unknown option: %s\n", a.c_str()); return 2; }
        else project = a;
    }
    const std::string data_dir = FindDataDir(argv[0]);
    const char* xdg = getenv("XDG_CONFIG_HOME");
    const char* home = getenv("HOME");
    const std::string config_dir = (xdg && *xdg ? fs::path(xdg) : fs::path(home ? home : ".") / ".config") / "imgui-menu-editor";

    glfwSetErrorCallback(GlfwError);
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    const bool headless = !screenshot.empty() || do_export;
    glfwWindowHint(GLFW_MAXIMIZED, headless ? GLFW_FALSE : GLFW_TRUE);
    if (headless) glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(width, height, "ImGui Menu Editor", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(screenshot.empty() ? 1 : 0);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigWindowsMoveFromTitleBarOnly = false;
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    float xs = 1, ys = 1;
    glfwGetWindowContentScale(window, &xs, &ys);
    // GLFW on Wayland already reports logical sizes, so only scale on X11-style setups.
    int fbw, fbh, ww, wh;
    glfwGetFramebufferSize(window, &fbw, &fbh);
    glfwGetWindowSize(window, &ww, &wh);
    const float fb_scale = ww > 0 ? (float)fbw / ww : 1.0f;
    const float ui_scale = fb_scale > 1.01f ? 1.0f : xs;

    // Editor font (Roboto + Font Awesome)
    const std::string fonts = data_dir + "/fonts/";
    ImFont* ui = io.Fonts->AddFontFromFileTTF((fonts + "Roboto-Medium.ttf").c_str(), 15.0f);
    if (!ui) io.Fonts->AddFontDefault();
    ImFontConfig ic;
    ic.MergeMode = true;
    ic.GlyphMinAdvanceX = 15.0f;
    ic.PixelSnapH = true;
    io.Fonts->AddFontFromFileTTF((fonts + "fa-solid-900.ttf").c_str(), 13.5f, &ic);

    Editor editor;
    editor.content_scale = ui_scale;
    if (!editor.Init(data_dir, config_dir, project)) {
        fprintf(stderr, "Couldn't open or create a project.\n");
        return 1;
    }

    if (do_export) {
        std::string msg;
        bool ok = editor.ExportTo(export_dir, msg);
        printf("%s\n", msg.c_str());
        return ok ? 0 : 1;
    }
    glfwSetWindowUserPointer(window, &editor);
    glfwSetDropCallback(window, [](GLFWwindow* w, int count, const char** paths) {
        static_cast<Editor*>(glfwGetWindowUserPointer(w))->OnDrop(count, paths);
    });
    if (!select.empty()) editor.DebugSelect(select);
    for (auto& a : attach) editor.DebugAttach(a);
    if (!prompt.empty()) editor.DebugPrompt(prompt);
    int frame = 0;
    while (!glfwWindowShouldClose(window) && !editor.quit) {
        if (glfwGetWindowAttrib(window, GLFW_ICONIFIED)) { glfwWaitEventsTimeout(0.1); continue; }
        glfwPollEvents();
        editor.BeforeFrame();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        editor.Frame();
        ImGui::Render();
        glfwGetFramebufferSize(window, &fbw, &fbh);
        glViewport(0, 0, fbw, fbh);
        glClearColor(0.08f, 0.08f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        if (!screenshot.empty() && !editor.BridgeBusy() && ++frame >= frames) {
            std::vector<unsigned char> px((size_t)fbw * fbh * 4);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(0, 0, fbw, fbh, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
            stbi_flip_vertically_on_write(1);
            stbi_write_png(screenshot.c_str(), fbw, fbh, 4, px.data(), fbw * 4);
            break;
        }
        glfwSwapBuffers(window);
        if (!screenshot.empty()) usleep(16000);
    }
    editor.Shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
