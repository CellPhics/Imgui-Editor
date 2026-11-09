// Command-line helpers, mainly for Claude when it recreates a screenshot:
//   imgui-menu-editor tool render  [--project DIR] [--out FILE] [--show NODE_ID] [--bg #RRGGBB] [--no-rects]
//   imgui-menu-editor tool compare REFERENCE RENDER [--out diff.png] [--cell N] [--top N]
//   imgui-menu-editor tool inspect IMAGE [--top N] [x,y ...] [x,y,w,h ...]
//   imgui-menu-editor tool crop    IMAGE x y w h OUT [--scale N]
//   imgui-menu-editor tool fonts   [filter]
//   imgui-menu-editor tool import-font PATH [--project DIR]
#include "assets.h"
#include "model.h"
#include "renderer.h"
#include "theme.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cfloat>
#include <functional>
#include <unistd.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <vector>
#include "stb/stb_image.h"
#include "stb/stb_image_write.h"

namespace fs = std::filesystem;

namespace {

struct Img {
    int w = 0, h = 0;
    std::vector<unsigned char> px; // RGBA
    bool Load(const std::string& path)
    {
        int n;
        unsigned char* d = stbi_load(path.c_str(), &w, &h, &n, 4);
        if (!d) {
            // Formats stb can't read (webp, ...): convert with ImageMagick if available.
            std::string tmp = (fs::temp_directory_path() / ("mme_conv_" + std::to_string(getpid()) + ".png")).string();
            std::string cmd = "magick '" + path + "' '" + tmp + "' 2>/dev/null";
            if (path.find('\'') == std::string::npos && system(cmd.c_str()) == 0) d = stbi_load(tmp.c_str(), &w, &h, &n, 4);
            std::error_code ec;
            fs::remove(tmp, ec);
        }
        if (!d) return false;
        px.assign(d, d + (size_t)w * h * 4);
        stbi_image_free(d);
        return true;
    }
    const unsigned char* At(int x, int y) const { return &px[((size_t)y * w + x) * 4]; }
};

std::string Hex(int r, int g, int b)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "#%02X%02X%02X", r, g, b);
    return buf;
}

std::string Floats(int r, int g, int b, int a = 255)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "[%.3f, %.3f, %.3f, %.3f]", r / 255.0, g / 255.0, b / 255.0, a / 255.0);
    return buf;
}

std::string Opt(int argc, char** argv, const char* name, const std::string& def = "")
{
    for (int i = 0; i + 1 < argc; i++)
        if (strcmp(argv[i], name) == 0) return argv[i + 1];
    return def;
}
bool Flag(int argc, char** argv, const char* name)
{
    for (int i = 0; i < argc; i++)
        if (strcmp(argv[i], name) == 0) return true;
    return false;
}

// ---------------------------------------------------------------- inspect
int Inspect(int argc, char** argv)
{
    if (argc < 1) { fprintf(stderr, "usage: tool inspect IMAGE [--top N] [x,y ...] [x,y,w,h ...]\n"); return 2; }
    Img im;
    if (!im.Load(argv[0])) { fprintf(stderr, "Couldn't read image %s\n", argv[0]); return 1; }
    printf("%s: %d x %d px\n", argv[0], im.w, im.h);
    int top = std::stoi(Opt(argc, argv, "--top", "16"));

    // Dominant colors: 4-bit-per-channel buckets, averaged.
    struct Acc { long long r = 0, g = 0, b = 0, n = 0; };
    std::map<int, Acc> bins;
    for (int y = 0; y < im.h; y++)
        for (int x = 0; x < im.w; x++) {
            const unsigned char* p = im.At(x, y);
            int k = ((p[0] >> 4) << 8) | ((p[1] >> 4) << 4) | (p[2] >> 4);
            Acc& a = bins[k];
            a.r += p[0]; a.g += p[1]; a.b += p[2]; a.n++;
        }
    std::vector<Acc> v;
    for (auto& [k, a] : bins) v.push_back(a);
    std::sort(v.begin(), v.end(), [](const Acc& a, const Acc& b) { return a.n > b.n; });
    const double total = (double)im.w * im.h;
    printf("\nDominant colors (share of pixels):\n");
    for (int i = 0; i < (int)v.size() && i < top; i++) {
        int r = (int)(v[i].r / v[i].n), g = (int)(v[i].g / v[i].n), b = (int)(v[i].b / v[i].n);
        printf("  %5.1f%%  %s  %s\n", 100.0 * v[i].n / total, Hex(r, g, b).c_str(), Floats(r, g, b).c_str());
    }

    bool header = false;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') { i++; continue; }
        int x, y, w, h;
        int n = sscanf(argv[i], "%d,%d,%d,%d", &x, &y, &w, &h);
        if (n < 2) continue;
        if (!header) { printf("\nSamples:\n"); header = true; }
        if (n == 2) {
            if (x < 0 || y < 0 || x >= im.w || y >= im.h) { printf("  %d,%d: outside the image\n", x, y); continue; }
            const unsigned char* p = im.At(x, y);
            printf("  pixel %d,%d: %s  %s\n", x, y, Hex(p[0], p[1], p[2]).c_str(), Floats(p[0], p[1], p[2], p[3]).c_str());
        } else if (n == 4) {
            long long r = 0, g = 0, b = 0, c = 0;
            for (int yy = std::max(0, y); yy < std::min(im.h, y + h); yy++)
                for (int xx = std::max(0, x); xx < std::min(im.w, x + w); xx++) {
                    const unsigned char* p = im.At(xx, yy);
                    r += p[0]; g += p[1]; b += p[2]; c++;
                }
            if (!c) { printf("  region %d,%d,%d,%d: outside the image\n", x, y, w, h); continue; }
            printf("  region %d,%d %dx%d average: %s  %s\n", x, y, w, h, Hex(r / c, g / c, b / c).c_str(), Floats(r / c, g / c, b / c).c_str());
        }
    }
    return 0;
}

// ---------------------------------------------------------------- crop
int Crop(int argc, char** argv)
{
    if (argc < 6) { fprintf(stderr, "usage: tool crop IMAGE x y w h OUT [--scale N] [--key auto|#RRGGBB] [--tolerance N]\n"); return 2; }
    Img im;
    if (!im.Load(argv[0])) { fprintf(stderr, "Couldn't read image %s\n", argv[0]); return 1; }
    int x = atoi(argv[1]), y = atoi(argv[2]), w = atoi(argv[3]), h = atoi(argv[4]);
    int scale = std::clamp(std::stoi(Opt(argc, argv, "--scale", "1")), 1, 16);
    x = std::clamp(x, 0, im.w - 1); y = std::clamp(y, 0, im.h - 1);
    w = std::clamp(w, 1, im.w - x); h = std::clamp(h, 1, im.h - y);
    // Optional color key: make the background transparent (soft edge over `tolerance`).
    std::string key = Opt(argc, argv, "--key");
    const float tol = std::max(1.0f, (float)std::stoi(Opt(argc, argv, "--tolerance", "24")));
    int kr = -1, kg = 0, kb = 0;
    if (key == "auto") { const unsigned char* p = im.At(x, y); kr = p[0]; kg = p[1]; kb = p[2]; }
    else if (!key.empty()) {
        ImVec4 c;
        if (ParseColor(json(key), c)) { kr = (int)(c.x * 255 + 0.5f); kg = (int)(c.y * 255 + 0.5f); kb = (int)(c.z * 255 + 0.5f); }
    }
    std::vector<unsigned char> out((size_t)w * scale * h * scale * 4);
    for (int yy = 0; yy < h * scale; yy++)
        for (int xx = 0; xx < w * scale; xx++) {
            unsigned char* o = &out[((size_t)yy * w * scale + xx) * 4];
            memcpy(o, im.At(x + xx / scale, y + yy / scale), 4);
            if (kr >= 0) {
                const float d = (float)std::max({abs(o[0] - kr), abs(o[1] - kg), abs(o[2] - kb)});
                const float a = std::clamp((d - tol * 0.5f) / (tol * 0.5f), 0.0f, 1.0f);
                o[3] = (unsigned char)(o[3] * a);
            }
        }
    if (kr >= 0) printf("Background %s made transparent (tolerance %.0f)\n", Hex(kr, kg, kb).c_str(), tol);
    std::error_code ec;
    fs::create_directories(fs::absolute(argv[5]).parent_path(), ec);
    if (!stbi_write_png(argv[5], w * scale, h * scale, 4, out.data(), w * scale * 4)) { fprintf(stderr, "Couldn't write %s\n", argv[5]); return 1; }
    printf("Wrote %s (%d x %d, region %d,%d %dx%d of %s, scale %d)\n", argv[5], w * scale, h * scale, x, y, w, h, argv[0], scale);
    return 0;
}

// ---------------------------------------------------------------- compare
int Compare(int argc, char** argv)
{
    if (argc < 2) { fprintf(stderr, "usage: tool compare REFERENCE RENDER [--out diff.png] [--cell N] [--top N]\n"); return 2; }
    Img a, b;
    if (!a.Load(argv[0])) { fprintf(stderr, "Couldn't read %s\n", argv[0]); return 1; }
    if (!b.Load(argv[1])) { fprintf(stderr, "Couldn't read %s\n", argv[1]); return 1; }
    const int cell = std::clamp(std::stoi(Opt(argc, argv, "--cell", "16")), 4, 128);
    const int top = std::clamp(std::stoi(Opt(argc, argv, "--top", "15")), 1, 100);
    std::string out = Opt(argc, argv, "--out", ".bridge/diff.png");
    printf("reference %d x %d, render %d x %d\n", a.w, a.h, b.w, b.h);
    if (a.w != b.w || a.h != b.h)
        printf("SIZE MISMATCH: render is %+d px wide and %+d px tall vs the reference (fix window size / padding first). "
               "Comparing the overlapping top-left area.\n", b.w - a.w, b.h - a.h);
    const int w = std::min(a.w, b.w), h = std::min(a.h, b.h);
    std::vector<unsigned char> diff((size_t)w * h * 4);
    double total = 0;
    long long off = 0;
    struct Cell { int x, y; double score; long long ar, ag, ab, br, bg, bb, n; };
    const int cw = (w + cell - 1) / cell, ch = (h + cell - 1) / cell;
    std::vector<Cell> cells((size_t)cw * ch);
    for (int cy = 0; cy < ch; cy++)
        for (int cx = 0; cx < cw; cx++) cells[(size_t)cy * cw + cx] = {cx * cell, cy * cell, 0, 0, 0, 0, 0, 0, 0, 0};
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const unsigned char* p = a.At(x, y);
            const unsigned char* q = b.At(x, y);
            int d = (abs(p[0] - q[0]) + abs(p[1] - q[1]) + abs(p[2] - q[2])) / 3;
            total += d;
            if (d > 24) off++;
            unsigned char* o = &diff[((size_t)y * w + x) * 4];
            // Gray reference with the differences in red.
            unsigned char g = (unsigned char)((p[0] + p[1] + p[2]) / 6);
            o[0] = (unsigned char)std::min(255, g + d * 3); o[1] = g; o[2] = g; o[3] = 255;
            Cell& c = cells[(size_t)(y / cell) * cw + x / cell];
            c.score += d; c.n++;
            c.ar += p[0]; c.ag += p[1]; c.ab += p[2]; c.br += q[0]; c.bg += q[1]; c.bb += q[2];
        }
    const double px = (double)w * h;
    printf("mean difference %.1f / 255, %.1f%% of pixels clearly different (similarity %.1f%%)\n",
           total / px, 100.0 * off / px, 100.0 - 100.0 * total / px / 255.0);
    std::sort(cells.begin(), cells.end(), [](const Cell& l, const Cell& r) { return l.score / std::max(1LL, l.n) > r.score / std::max(1LL, r.n); });
    printf("\nMost different %dx%d areas (x y: mean diff, reference avg -> render avg):\n", cell, cell);
    for (int i = 0; i < top && i < (int)cells.size(); i++) {
        const Cell& c = cells[i];
        if (!c.n || c.score / c.n < 6) break;
        printf("  %4d %4d: %5.1f  %s -> %s\n", c.x, c.y, c.score / c.n,
               Hex(c.ar / c.n, c.ag / c.n, c.ab / c.n).c_str(), Hex(c.br / c.n, c.bg / c.n, c.bb / c.n).c_str());
    }
    std::error_code ec;
    fs::create_directories(fs::absolute(out).parent_path(), ec);
    if (stbi_write_png(out.c_str(), w, h, 4, diff.data(), w * 4)) printf("\nDiff image (differences in red): %s\n", out.c_str());
    return 0;
}

// ---------------------------------------------------------------- fonts
int Fonts(int argc, char** argv)
{
    std::string filter = argc > 0 ? argv[0] : "";
    std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);
    FILE* f = popen("fc-list --format '%{file}\\t%{family[0]}\\t%{style[0]}\\n' 2>/dev/null", "r");
    if (!f) { fprintf(stderr, "fc-list not available\n"); return 1; }
    std::vector<std::string> lines;
    char buf[1024];
    while (fgets(buf, sizeof(buf), f)) {
        std::string l = buf;
        std::string low = l;
        std::transform(low.begin(), low.end(), low.begin(), ::tolower);
        if (low.find(".ttf") == std::string::npos && low.find(".otf") == std::string::npos) continue;
        if (!filter.empty() && low.find(filter) == std::string::npos) continue;
        lines.push_back(l);
    }
    pclose(f);
    std::sort(lines.begin(), lines.end(), [](const std::string& a, const std::string& b) { return a.substr(a.find('\t')) < b.substr(b.find('\t')); });
    printf("file\tfamily\tstyle\n");
    for (auto& l : lines) fputs(l.c_str(), stdout);
    printf("(%zu fonts%s) Use `tool import-font PATH` to copy one into assets/fonts.\n", lines.size(), filter.empty() ? "" : (" matching '" + filter + "'").c_str());
    return 0;
}

int ImportFont(int argc, char** argv)
{
    if (argc < 1) { fprintf(stderr, "usage: tool import-font PATH [--project DIR]\n"); return 2; }
    fs::path src(argv[0]);
    fs::path project = Opt(argc, argv, "--project", fs::current_path().string());
    std::error_code ec;
    if (!fs::is_regular_file(src, ec)) { fprintf(stderr, "Not a file: %s\n", argv[0]); return 1; }
    fs::path dst = project / "assets/fonts" / src.filename();
    fs::create_directories(dst.parent_path(), ec);
    fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
    if (ec) { fprintf(stderr, "Copy failed: %s\n", ec.message().c_str()); return 1; }
    printf("Copied to %s\nUse this path in menu.json fonts: \"assets/fonts/%s\"\n", dst.string().c_str(), src.filename().string().c_str());
    return 0;
}

// ---------------------------------------------------------------- render
int Render(int argc, char** argv, const std::string& data_dir)
{
    fs::path project = Opt(argc, argv, "--project", fs::current_path().string());
    std::string out = Opt(argc, argv, "--out", (project / ".bridge/render.png").string());
    std::string show = Opt(argc, argv, "--show");
    std::string bg_hex = Opt(argc, argv, "--bg", "#1E1F24");
    const bool rects = !Flag(argc, argv, "--no-rects");

    bool ok = false;
    json doc = json::parse(ReadFile((project / "menu.json").string(), &ok), nullptr, false);
    if (!ok || doc.is_discarded()) { fprintf(stderr, "Couldn't read a valid %s/menu.json\n", project.string().c_str()); return 1; }
    for (auto& w : Normalize(doc)) printf("warning: %s\n", w.c_str());

    // Canvas big enough for every window.
    int cw = 400, ch = 300;
    for (auto& w : doc["windows"]) {
        ImVec2 p = JV2(w, "pos", ImVec2(60, 60)), s = JV2(w, "size", ImVec2(720, 460));
        cw = std::max(cw, (int)(p.x + s.x) + 40);
        ch = std::max(ch, (int)(p.y + s.y) + 40);
    }
    cw = std::min(cw, 4096);
    ch = std::min(ch, 4096);

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    if (!glfwInit()) { fprintf(stderr, "Couldn't start GLFW\n"); return 1; }
    GLFWwindow* win = glfwCreateWindow(cw, ch, "render", nullptr, nullptr);
    if (!win) { fprintf(stderr, "Couldn't create an OpenGL context\n"); glfwTerminate(); return 1; }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(0);
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui_ImplGlfw_InitForOpenGL(win, false);
    ImGui_ImplOpenGL3_Init("#version 330");

    Assets assets;
    assets.data_dir = data_dir;
    assets.project_dir = project.string();
    PreviewCtx ctx;
    ctx.edit_mode = false;
    ctx.origin = ImVec2(0, 0);
    if (!show.empty()) { ctx.selected = show; ctx.selection_changed = true; }
    ImVec4 bg(0.12f, 0.12f, 0.14f, 1);
    ParseColor(json(bg_hex), bg);

    int fbw = cw, fbh = ch;
    for (int frame = 0; frame < 40; frame++) {
        assets.SyncFonts(doc);
        if (frame == 0) assets.SyncImages(doc);
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2((float)cw, (float)ch);
        io.DisplayFramebufferScale = ImVec2(1, 1);
        io.DeltaTime = 0.1f; // let animations settle quickly
        ImGui::NewFrame();
        RenderPreview(doc, assets, ctx);
        ImGui::Render();
        glfwGetFramebufferSize(win, &fbw, &fbh);
        fbw = std::min(fbw, cw);
        fbh = std::min(fbh, ch);
        glViewport(0, 0, cw, ch);
        glClearColor(bg.x, bg.y, bg.z, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }

    // Crop to the union of the windows.
    ImRect crop(ImVec2(FLT_MAX, FLT_MAX), ImVec2(-FLT_MAX, -FLT_MAX));
    std::vector<std::string> window_ids;
    for (auto& w : doc["windows"]) {
        auto it = ctx.rects.find(JS(w, "id"));
        if (it == ctx.rects.end()) continue;
        crop.Add(it->second.rect);
        window_ids.push_back(JS(w, "id"));
    }
    if (crop.Min.x > crop.Max.x) crop = ImRect(ImVec2(0, 0), ImVec2((float)cw, (float)ch));
    int x0 = std::max(0, (int)crop.Min.x), y0 = std::max(0, (int)crop.Min.y);
    int x1 = std::min(cw, (int)std::ceil(crop.Max.x)), y1 = std::min(ch, (int)std::ceil(crop.Max.y));
    int w = std::max(1, x1 - x0), h = std::max(1, y1 - y0);

    std::vector<unsigned char> px((size_t)cw * ch * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, cw, ch, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    std::vector<unsigned char> img((size_t)w * h * 4);
    for (int y = 0; y < h; y++) {
        int src_y = ch - 1 - (y0 + y); // GL origin is bottom-left
        memcpy(&img[(size_t)y * w * 4], &px[((size_t)src_y * cw + x0) * 4], (size_t)w * 4);
    }
    std::error_code ec;
    fs::create_directories(fs::absolute(out).parent_path(), ec);
    const bool wrote = stbi_write_png(out.c_str(), w, h, 4, img.data(), w * 4) != 0;

    if (wrote) printf("Wrote %s (%d x %d px, cropped to the menu windows)\n", out.c_str(), w, h);
    else fprintf(stderr, "Couldn't write %s\n", out.c_str());
    if (!assets.font_error.empty()) printf("font warnings:\n%s", assets.font_error.c_str());
    if (rects) {
        printf("\nWidget rectangles in the rendered image (id type x y w h):\n");
        // Walk the tree so output follows the menu structure.
        std::function<void(const json&, int)> walk = [&](const json& arr, int depth) {
            if (!arr.is_array()) return;
            for (auto& n : arr) {
                auto it = ctx.rects.find(JS(n, "id"));
                if (it != ctx.rects.end()) {
                    ImRect r = it->second.rect;
                    printf("%*s%s %s %d %d %d %d\n", depth * 2, "", JS(n, "id").c_str(), JS(n, "type").c_str(),
                           (int)std::lround(r.Min.x - x0), (int)std::lround(r.Min.y - y0), (int)std::lround(r.GetWidth()), (int)std::lround(r.GetHeight()));
                }
                if (n.contains("children")) walk(n["children"], depth + 1);
            }
        };
        walk(doc["windows"], 0);
        printf("(Only widgets on the visible tab are listed; use --show NODE_ID to render another tab.)\n");
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(win);
    glfwTerminate();
    return wrote ? 0 : 1;
}

} // namespace

int RunTool(int argc, char** argv, const std::string& data_dir)
{
    if (argc < 1) {
        printf("tools: render | compare | inspect | crop | fonts | import-font\n");
        return 2;
    }
    std::string cmd = argv[0];
    if (cmd == "render") return Render(argc - 1, argv + 1, data_dir);
    if (cmd == "inspect") return Inspect(argc - 1, argv + 1);
    if (cmd == "crop") return Crop(argc - 1, argv + 1);
    if (cmd == "fonts") return Fonts(argc - 1, argv + 1);
    if (cmd == "compare") return Compare(argc - 1, argv + 1);
    if (cmd == "import-font") return ImportFont(argc - 1, argv + 1);
    fprintf(stderr, "unknown tool '%s'\n", cmd.c_str());
    return 2;
}
