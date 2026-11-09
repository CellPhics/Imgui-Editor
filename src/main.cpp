// ImGui Menu Editor
//
//   imgui-menu-editor [project_dir]                    open the editor
//   imgui-menu-editor [project_dir] --export [dir]     export C++ and quit (no window)
//   imgui-menu-editor tool <name> ...                  command line helpers, see tools.cpp
//   imgui-menu-editor [project_dir] --screenshot out.png [--frames N] [--size WxH] [--select ID]
//                                                      render the editor offscreen to a png (handy for checking UI changes)
#include "editor.h"
#include "tools.h"
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

struct Options {
    std::string project;
    bool export_only = false;
    std::string export_dir;
    std::string screenshot;
    int screenshot_frames = 45;
    int width = 1600, height = 950;
    std::string select;
};

static bool ParseArgs(int argc, char** argv, Options& o)
{
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        const bool has_value = i + 1 < argc;
        if (a == "--export") {
            o.export_only = true;
            if (has_value && argv[i + 1][0] != '-') o.export_dir = argv[++i];
        } else if (a == "--screenshot" && has_value) o.screenshot = argv[++i];
        else if (a == "--frames" && has_value) o.screenshot_frames = atoi(argv[++i]);
        else if (a == "--size" && has_value) sscanf(argv[++i], "%dx%d", &o.width, &o.height);
        else if (a == "--select" && has_value) o.select = argv[++i];
        else if (!a.empty() && a[0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", a.c_str());
            return false;
        } else o.project = a;
    }
    return true;
}

// Bundled fonts/templates live in a "data" folder next to the binary.
static std::string FindDataDir(const char* argv0)
{
    std::error_code ec;
    fs::path exe = fs::read_symlink("/proc/self/exe", ec);
    if (ec) exe = fs::absolute(argv0, ec);
    for (const fs::path& dir : {exe.parent_path() / "data", exe.parent_path() / "../share/imgui-menu-editor", fs::path(MME_DATA_DIR)})
        if (fs::exists(dir / "fonts" / "fa-solid-900.ttf", ec)) return fs::weakly_canonical(dir, ec).string();
    return (exe.parent_path() / "data").string();
}

static std::string ConfigDir()
{
    const char* xdg = getenv("XDG_CONFIG_HOME");
    const fs::path base = xdg && *xdg ? fs::path(xdg) : fs::path(getenv("HOME") ? getenv("HOME") : ".") / ".config";
    return (base / "imgui-menu-editor").string();
}

static void OnGlfwError(int code, const char* message)
{
    if (code == GLFW_FORMAT_UNAVAILABLE) return; // clipboard has no text (like when it's an image), that's fine
    fprintf(stderr, "GLFW error %d: %s\n", code, message);
}

static void SavePng(const std::string& path, int w, int h)
{
    std::vector<unsigned char> pixels((size_t)w * h * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    stbi_flip_vertically_on_write(1);
    stbi_write_png(path.c_str(), w, h, 4, pixels.data(), w * 4);
}

int main(int argc, char** argv)
{
    const std::string data_dir = FindDataDir(argv[0]);
    if (argc > 1 && std::string(argv[1]) == "tool") return RunTool(argc - 2, argv + 2, data_dir);

    Options opt;
    if (!ParseArgs(argc, argv, opt)) return 2;
    const bool headless = !opt.screenshot.empty() || opt.export_only;

    glfwSetErrorCallback(OnGlfwError);
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_MAXIMIZED, headless ? GLFW_FALSE : GLFW_TRUE);
    if (headless) glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(opt.width, opt.height, "ImGui Menu Editor", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(headless ? 0 : 1); // vsync would block forever on a hidden window

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    // On Wayland GLFW already works in scaled (logical) pixels, so only apply
    // the monitor scale ourselves when the framebuffer isn't scaled already.
    float scale_x = 1, scale_y = 1;
    glfwGetWindowContentScale(window, &scale_x, &scale_y);
    int fb_w, fb_h, win_w, win_h;
    glfwGetFramebufferSize(window, &fb_w, &fb_h);
    glfwGetWindowSize(window, &win_w, &win_h);
    const bool framebuffer_scaled = win_w > 0 && (float)fb_w / win_w > 1.01f;

    // editor font: Roboto with Font Awesome merged in
    const std::string fonts = data_dir + "/fonts/";
    if (!io.Fonts->AddFontFromFileTTF((fonts + "Roboto-Medium.ttf").c_str(), 15.0f)) io.Fonts->AddFontDefault();
    ImFontConfig icons;
    icons.MergeMode = true;
    icons.GlyphMinAdvanceX = 15.0f;
    icons.PixelSnapH = true;
    io.Fonts->AddFontFromFileTTF((fonts + "fa-solid-900.ttf").c_str(), 13.5f, &icons);

    Editor editor;
    editor.content_scale = framebuffer_scaled ? 1.0f : scale_x;
    if (!editor.Init(data_dir, ConfigDir(), opt.project)) {
        fprintf(stderr, "Couldn't open or create a project.\n");
        return 1;
    }

    if (opt.export_only) {
        std::string message;
        const bool ok = editor.ExportTo(opt.export_dir, message);
        printf("%s\n", message.c_str());
        return ok ? 0 : 1;
    }

    glfwSetWindowUserPointer(window, &editor);
    glfwSetDropCallback(window, [](GLFWwindow* w, int count, const char** paths) {
        static_cast<Editor*>(glfwGetWindowUserPointer(w))->OnDrop(count, paths);
    });
    if (!opt.select.empty()) editor.SelectNode(opt.select);

    int frame = 0;
    while (!glfwWindowShouldClose(window) && !editor.quit) {
        if (glfwGetWindowAttrib(window, GLFW_ICONIFIED)) { // minimized, don't burn cpu
            glfwWaitEventsTimeout(0.1);
            continue;
        }
        glfwPollEvents();
        editor.BeforeFrame();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        editor.Frame();
        ImGui::Render();
        glfwGetFramebufferSize(window, &fb_w, &fb_h);
        glViewport(0, 0, fb_w, fb_h);
        glClearColor(0.08f, 0.08f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        if (!opt.screenshot.empty() && ++frame >= opt.screenshot_frames) {
            SavePng(opt.screenshot, fb_w, fb_h);
            break;
        }
        glfwSwapBuffers(window);
        if (!opt.screenshot.empty()) usleep(16000); // let animations play at a normal speed
    }

    editor.Shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
