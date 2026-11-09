// `imgui-menu-editor tool ...` - little command line helpers. Mostly there so
// Claude can check its own work when it copies a menu from a screenshot, but
// they're handy by hand too.
//
//   tool render  [--project DIR] [--out FILE] [--show NODE_ID] [--bg #RRGGBB] [--no-rects]
//   tool compare REFERENCE RENDER [--out diff.png] [--cell N] [--top N]
//   tool inspect IMAGE [--top N] [x,y ...] [x,y,w,h ...]
//   tool crop    IMAGE x y w h OUT [--scale N] [--key auto|#RRGGBB] [--tolerance N]
//   tool fonts   [filter]
//   tool import-font PATH [--project DIR]
#include "tools.h"
#include "assets.h"
#include "renderer.h"
#include "theme.h"
#include "util.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <map>
#include <unistd.h>
#include "stb/stb_image.h"
#include "stb/stb_image_write.h"

namespace fs = std::filesystem;

namespace {

// ---------------------------------------------------------------- small helpers

struct Args {
    std::vector<std::string> list;
    Args(int argc, char** argv) : list(argv, argv + argc) {}

    std::string Option(const char* name, const std::string& fallback = "") const
    {
        for (size_t i = 0; i + 1 < list.size(); i++)
            if (list[i] == name) return list[i + 1];
        return fallback;
    }
    int IntOption(const char* name, int fallback) const
    {
        const std::string v = Option(name);
        return v.empty() ? fallback : atoi(v.c_str());
    }
    bool Flag(const char* name) const { return std::find(list.begin(), list.end(), name) != list.end(); }
    // positional args (skips --options and their values)
    std::vector<std::string> Positional() const
    {
        std::vector<std::string> out;
        for (size_t i = 0; i < list.size(); i++) {
            if (list[i].rfind("--", 0) == 0) {
                if (list[i] != "--no-rects") i++; // skip the option's value
                continue;
            }
            out.push_back(list[i]);
        }
        return out;
    }
};

struct Image {
    int w = 0, h = 0;
    std::vector<unsigned char> rgba;

    bool Load(const std::string& path)
    {
        int channels;
        unsigned char* data = stbi_load(path.c_str(), &w, &h, &channels, 4);
        if (!data && HaveCommand("magick")) {
            // stb can't do webp and friends, let ImageMagick convert it
            const std::string tmp = (fs::temp_directory_path() / ("mme_convert_" + std::to_string(getpid()) + ".png")).string();
            if (system(("magick " + ShellQuote(path) + " " + ShellQuote(tmp) + " 2>/dev/null").c_str()) == 0)
                data = stbi_load(tmp.c_str(), &w, &h, &channels, 4);
            std::error_code ec;
            fs::remove(tmp, ec);
        }
        if (!data) return false;
        rgba.assign(data, data + (size_t)w * h * 4);
        stbi_image_free(data);
        return true;
    }
    const unsigned char* At(int x, int y) const { return &rgba[((size_t)y * w + x) * 4]; }
};

std::string Hex(int r, int g, int b)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "#%02X%02X%02X", r, g, b);
    return buf;
}

std::string FloatColor(int r, int g, int b, int a = 255)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "[%.3f, %.3f, %.3f, %.3f]", r / 255.0, g / 255.0, b / 255.0, a / 255.0);
    return buf;
}

bool WritePng(const std::string& path, int w, int h, const unsigned char* rgba)
{
    std::error_code ec;
    fs::create_directories(fs::absolute(path).parent_path(), ec);
    return stbi_write_png(path.c_str(), w, h, 4, rgba, w * 4) != 0;
}

// ---------------------------------------------------------------- inspect

int Inspect(const Args& args)
{
    const auto pos = args.Positional();
    if (pos.empty()) {
        fprintf(stderr, "usage: tool inspect IMAGE [--top N] [x,y ...] [x,y,w,h ...]\n");
        return 2;
    }
    Image img;
    if (!img.Load(pos[0])) {
        fprintf(stderr, "Couldn't read image %s\n", pos[0].c_str());
        return 1;
    }
    printf("%s: %d x %d px\n", pos[0].c_str(), img.w, img.h);

    // Dominant colors: bucket every pixel to 4 bits per channel, average each bucket.
    struct Bucket { long long r = 0, g = 0, b = 0, n = 0; };
    std::map<int, Bucket> buckets;
    for (int y = 0; y < img.h; y++)
        for (int x = 0; x < img.w; x++) {
            const unsigned char* p = img.At(x, y);
            Bucket& b = buckets[((p[0] >> 4) << 8) | ((p[1] >> 4) << 4) | (p[2] >> 4)];
            b.r += p[0]; b.g += p[1]; b.b += p[2]; b.n++;
        }
    std::vector<Bucket> sorted;
    for (auto& [key, b] : buckets) sorted.push_back(b);
    std::sort(sorted.begin(), sorted.end(), [](const Bucket& a, const Bucket& b) { return a.n > b.n; });

    const int top = args.IntOption("--top", 16);
    const double total = (double)img.w * img.h;
    printf("\nDominant colors (share of pixels):\n");
    for (int i = 0; i < (int)sorted.size() && i < top; i++) {
        const Bucket& b = sorted[i];
        const int r = (int)(b.r / b.n), g = (int)(b.g / b.n), bl = (int)(b.b / b.n);
        printf("  %5.1f%%  %s  %s\n", 100.0 * b.n / total, Hex(r, g, bl).c_str(), FloatColor(r, g, bl).c_str());
    }

    bool printed_header = false;
    for (size_t i = 1; i < pos.size(); i++) {
        int x, y, w, h;
        const int count = sscanf(pos[i].c_str(), "%d,%d,%d,%d", &x, &y, &w, &h);
        if (count < 2) continue;
        if (!printed_header) {
            printf("\nSamples:\n");
            printed_header = true;
        }
        if (count == 2) {
            if (x < 0 || y < 0 || x >= img.w || y >= img.h) { printf("  %d,%d: outside the image\n", x, y); continue; }
            const unsigned char* p = img.At(x, y);
            printf("  pixel %d,%d: %s  %s\n", x, y, Hex(p[0], p[1], p[2]).c_str(), FloatColor(p[0], p[1], p[2], p[3]).c_str());
        } else if (count == 4) {
            long long r = 0, g = 0, b = 0, n = 0;
            for (int yy = std::max(0, y); yy < std::min(img.h, y + h); yy++)
                for (int xx = std::max(0, x); xx < std::min(img.w, x + w); xx++) {
                    const unsigned char* p = img.At(xx, yy);
                    r += p[0]; g += p[1]; b += p[2]; n++;
                }
            if (!n) { printf("  region %d,%d,%d,%d: outside the image\n", x, y, w, h); continue; }
            printf("  region %d,%d %dx%d average: %s  %s\n", x, y, w, h, Hex(r / n, g / n, b / n).c_str(), FloatColor(r / n, g / n, b / n).c_str());
        }
    }
    return 0;
}

// ---------------------------------------------------------------- compare

int Compare(const Args& args)
{
    const auto pos = args.Positional();
    if (pos.size() < 2) {
        fprintf(stderr, "usage: tool compare REFERENCE RENDER [--out diff.png] [--cell N] [--top N]\n");
        return 2;
    }
    Image ref, render;
    if (!ref.Load(pos[0])) { fprintf(stderr, "Couldn't read %s\n", pos[0].c_str()); return 1; }
    if (!render.Load(pos[1])) { fprintf(stderr, "Couldn't read %s\n", pos[1].c_str()); return 1; }
    const int cell = std::clamp(args.IntOption("--cell", 16), 4, 128);
    const int top = std::clamp(args.IntOption("--top", 15), 1, 100);
    const std::string out = args.Option("--out", ".bridge/diff.png");

    printf("reference %d x %d, render %d x %d\n", ref.w, ref.h, render.w, render.h);
    if (ref.w != render.w || ref.h != render.h)
        printf("SIZE MISMATCH: render is %+d px wide and %+d px tall vs the reference (fix window size / padding first). "
               "Comparing the overlapping top-left area.\n", render.w - ref.w, render.h - ref.h);

    const int w = std::min(ref.w, render.w), h = std::min(ref.h, render.h);
    struct Cell { int x, y; double diff = 0; long long n = 0, ar = 0, ag = 0, ab = 0, br = 0, bg = 0, bb = 0; };
    const int cols = (w + cell - 1) / cell, rows = (h + cell - 1) / cell;
    std::vector<Cell> cells((size_t)cols * rows);
    for (int cy = 0; cy < rows; cy++)
        for (int cx = 0; cx < cols; cx++) {
            cells[(size_t)cy * cols + cx].x = cx * cell;
            cells[(size_t)cy * cols + cx].y = cy * cell;
        }

    std::vector<unsigned char> diff_image((size_t)w * h * 4);
    double total_diff = 0;
    long long clearly_different = 0;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const unsigned char* a = ref.At(x, y);
            const unsigned char* b = render.At(x, y);
            const int d = (abs(a[0] - b[0]) + abs(a[1] - b[1]) + abs(a[2] - b[2])) / 3;
            total_diff += d;
            if (d > 24) clearly_different++;
            // diff image: the reference in gray with the differences painted red
            unsigned char* o = &diff_image[((size_t)y * w + x) * 4];
            const unsigned char gray = (unsigned char)((a[0] + a[1] + a[2]) / 6);
            o[0] = (unsigned char)std::min(255, gray + d * 3);
            o[1] = gray;
            o[2] = gray;
            o[3] = 255;
            Cell& c = cells[(size_t)(y / cell) * cols + x / cell];
            c.diff += d; c.n++;
            c.ar += a[0]; c.ag += a[1]; c.ab += a[2];
            c.br += b[0]; c.bg += b[1]; c.bb += b[2];
        }

    const double pixels = (double)w * h;
    printf("mean difference %.1f / 255, %.1f%% of pixels clearly different (similarity %.1f%%)\n",
           total_diff / pixels, 100.0 * clearly_different / pixels, 100.0 - 100.0 * total_diff / pixels / 255.0);

    std::sort(cells.begin(), cells.end(), [](const Cell& a, const Cell& b) { return a.diff / std::max(1LL, a.n) > b.diff / std::max(1LL, b.n); });
    printf("\nMost different %dx%d areas (x y: mean diff, reference avg -> render avg):\n", cell, cell);
    for (int i = 0; i < top && i < (int)cells.size(); i++) {
        const Cell& c = cells[i];
        if (!c.n || c.diff / c.n < 6) break; // the rest basically match
        printf("  %4d %4d: %5.1f  %s -> %s\n", c.x, c.y, c.diff / c.n, Hex(c.ar / c.n, c.ag / c.n, c.ab / c.n).c_str(),
               Hex(c.br / c.n, c.bg / c.n, c.bb / c.n).c_str());
    }
    if (WritePng(out, w, h, diff_image.data())) printf("\nDiff image (differences in red): %s\n", out.c_str());
    return 0;
}

// ---------------------------------------------------------------- crop

int Crop(const Args& args)
{
    const auto pos = args.Positional();
    if (pos.size() < 6) {
        fprintf(stderr, "usage: tool crop IMAGE x y w h OUT [--scale N] [--key auto|#RRGGBB] [--tolerance N]\n");
        return 2;
    }
    Image img;
    if (!img.Load(pos[0])) { fprintf(stderr, "Couldn't read image %s\n", pos[0].c_str()); return 1; }
    int x = std::clamp(atoi(pos[1].c_str()), 0, img.w - 1);
    int y = std::clamp(atoi(pos[2].c_str()), 0, img.h - 1);
    const int w = std::clamp(atoi(pos[3].c_str()), 1, img.w - x);
    const int h = std::clamp(atoi(pos[4].c_str()), 1, img.h - y);
    const std::string& out = pos[5];
    const int scale = std::clamp(args.IntOption("--scale", 1), 1, 16);

    // Optional color key: pixels close to the key color fade to transparent,
    // with a soft edge so anti-aliased icons don't get jaggy.
    const std::string key = args.Option("--key");
    const float tolerance = (float)std::max(1, args.IntOption("--tolerance", 24));
    int kr = -1, kg = 0, kb = 0;
    if (key == "auto") {
        const unsigned char* p = img.At(x, y);
        kr = p[0]; kg = p[1]; kb = p[2];
    } else if (!key.empty()) {
        ImVec4 c;
        if (ParseColor(json(key), c)) {
            kr = (int)(c.x * 255 + 0.5f); kg = (int)(c.y * 255 + 0.5f); kb = (int)(c.z * 255 + 0.5f);
        }
    }

    const int ow = w * scale, oh = h * scale;
    std::vector<unsigned char> pixels((size_t)ow * oh * 4);
    for (int yy = 0; yy < oh; yy++)
        for (int xx = 0; xx < ow; xx++) {
            unsigned char* o = &pixels[((size_t)yy * ow + xx) * 4];
            memcpy(o, img.At(x + xx / scale, y + yy / scale), 4);
            if (kr >= 0) {
                const float d = (float)std::max({abs(o[0] - kr), abs(o[1] - kg), abs(o[2] - kb)});
                const float alpha = std::clamp((d - tolerance * 0.5f) / (tolerance * 0.5f), 0.0f, 1.0f);
                o[3] = (unsigned char)(o[3] * alpha);
            }
        }
    if (kr >= 0) printf("Made the background %s transparent (tolerance %.0f)\n", Hex(kr, kg, kb).c_str(), tolerance);
    if (!WritePng(out, ow, oh, pixels.data())) { fprintf(stderr, "Couldn't write %s\n", out.c_str()); return 1; }
    printf("Wrote %s (%d x %d, region %d,%d %dx%d of %s, scale %d)\n", out.c_str(), ow, oh, x, y, w, h, pos[0].c_str(), scale);
    return 0;
}

// ---------------------------------------------------------------- fonts

int Fonts(const Args& args)
{
    const auto pos = args.Positional();
    const std::string filter = pos.empty() ? "" : pos[0];
    int status = 0;
    const std::string listing = RunCommand("fc-list --format '%{file}\\t%{family[0]}\\t%{style[0]}\\n' 2>/dev/null", &status);
    if (status != 0) {
        fprintf(stderr, "fc-list isn't available\n");
        return 1;
    }
    std::vector<std::string> lines;
    for (const std::string& line : SplitLines(listing)) {
        if (!HasExtension(line.substr(0, line.find('\t')), {".ttf", ".otf"})) continue;
        if (ContainsNoCase(line, filter)) lines.push_back(line);
    }
    // sort by family/style instead of path
    std::sort(lines.begin(), lines.end(), [](const std::string& a, const std::string& b) { return a.substr(a.find('\t')) < b.substr(b.find('\t')); });
    printf("file\tfamily\tstyle\n");
    for (auto& l : lines) printf("%s\n", l.c_str());
    printf("(%zu fonts%s) Use `tool import-font PATH` to copy one into assets/fonts.\n", lines.size(),
           filter.empty() ? "" : (" matching '" + filter + "'").c_str());
    return 0;
}

int ImportFont(const Args& args)
{
    const auto pos = args.Positional();
    if (pos.empty()) {
        fprintf(stderr, "usage: tool import-font PATH [--project DIR]\n");
        return 2;
    }
    const fs::path src(pos[0]);
    const fs::path project = args.Option("--project", fs::current_path().string());
    std::error_code ec;
    if (!fs::is_regular_file(src, ec)) { fprintf(stderr, "Not a file: %s\n", pos[0].c_str()); return 1; }
    const fs::path dst = project / "assets/fonts" / src.filename();
    fs::create_directories(dst.parent_path(), ec);
    fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
    if (ec) { fprintf(stderr, "Copy failed: %s\n", ec.message().c_str()); return 1; }
    printf("Copied to %s\nUse this path in menu.json fonts: \"assets/fonts/%s\"\n", dst.string().c_str(), src.filename().string().c_str());
    return 0;
}

// ---------------------------------------------------------------- render

// Renders the menu offscreen exactly like the canvas does and saves it,
// cropped to the menu windows.
int Render(const Args& args, const std::string& data_dir)
{
    const fs::path project = args.Option("--project", fs::current_path().string());
    const std::string out = args.Option("--out", (project / ".bridge/render.png").string());
    const std::string show = args.Option("--show");
    const bool print_rects = !args.Flag("--no-rects");

    bool ok = false;
    json doc = json::parse(ReadFile((project / "menu.json").string(), &ok), nullptr, false);
    if (!ok || doc.is_discarded()) {
        fprintf(stderr, "Couldn't read a valid %s/menu.json\n", project.string().c_str());
        return 1;
    }
    for (auto& w : Normalize(doc)) printf("warning: %s\n", w.c_str());

    // big enough to fit every window
    int canvas_w = 400, canvas_h = 300;
    for (auto& w : doc["windows"]) {
        const ImVec2 p = GetVec2(w, "pos", ImVec2(60, 60)), s = GetVec2(w, "size", ImVec2(720, 460));
        canvas_w = std::max(canvas_w, (int)(p.x + s.x) + 40);
        canvas_h = std::max(canvas_h, (int)(p.y + s.y) + 40);
    }
    canvas_w = std::min(canvas_w, 4096);
    canvas_h = std::min(canvas_h, 4096);

    if (!glfwInit()) { fprintf(stderr, "Couldn't start GLFW\n"); return 1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(canvas_w, canvas_h, "render", nullptr, nullptr);
    if (!window) { fprintf(stderr, "Couldn't create an OpenGL context\n"); glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(0);
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui_ImplGlfw_InitForOpenGL(window, false);
    ImGui_ImplOpenGL3_Init("#version 330");

    Assets assets;
    assets.data_dir = data_dir;
    assets.project_dir = project.string();
    PreviewState state;
    state.edit_mode = false;
    state.origin = ImVec2(0, 0);
    if (!show.empty()) {
        state.selected = show;
        state.selection_changed = true;
    }
    ImVec4 background(0.12f, 0.12f, 0.14f, 1);
    ParseColor(json(args.Option("--bg", "#1E1F24")), background);

    // a bunch of frames with a big fake delta time so fades/slides finish
    for (int frame = 0; frame < 40; frame++) {
        assets.SyncFonts(doc);
        if (frame == 0) assets.SyncImages(doc);
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2((float)canvas_w, (float)canvas_h);
        io.DisplayFramebufferScale = ImVec2(1, 1);
        io.DeltaTime = 0.1f;
        ImGui::NewFrame();
        RenderPreview(doc, assets, state);
        ImGui::Render();
        glViewport(0, 0, canvas_w, canvas_h);
        glClearColor(background.x, background.y, background.z, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }

    // crop to the windows
    ImRect crop(ImVec2(FLT_MAX, FLT_MAX), ImVec2(-FLT_MAX, -FLT_MAX));
    for (auto& w : doc["windows"]) {
        auto it = state.rects.find(GetStr(w, "id"));
        if (it != state.rects.end()) crop.Add(it->second.rect);
    }
    if (crop.Min.x > crop.Max.x) crop = ImRect(ImVec2(0, 0), ImVec2((float)canvas_w, (float)canvas_h));
    const int x0 = std::max(0, (int)crop.Min.x), y0 = std::max(0, (int)crop.Min.y);
    const int x1 = std::min(canvas_w, (int)std::ceil(crop.Max.x)), y1 = std::min(canvas_h, (int)std::ceil(crop.Max.y));
    const int w = std::max(1, x1 - x0), h = std::max(1, y1 - y0);

    std::vector<unsigned char> screen((size_t)canvas_w * canvas_h * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, canvas_w, canvas_h, GL_RGBA, GL_UNSIGNED_BYTE, screen.data());
    std::vector<unsigned char> cropped((size_t)w * h * 4);
    for (int y = 0; y < h; y++) {
        const int src_y = canvas_h - 1 - (y0 + y); // OpenGL's origin is bottom-left
        memcpy(&cropped[(size_t)y * w * 4], &screen[((size_t)src_y * canvas_w + x0) * 4], (size_t)w * 4);
    }
    const bool wrote = WritePng(out, w, h, cropped.data());
    if (wrote) printf("Wrote %s (%d x %d px, cropped to the menu windows)\n", out.c_str(), w, h);
    else fprintf(stderr, "Couldn't write %s\n", out.c_str());
    if (!assets.font_error.empty()) printf("font warnings:\n%s", assets.font_error.c_str());

    if (print_rects) {
        printf("\nWidget rectangles in the rendered image (id type x y w h):\n");
        std::function<void(const json&, int)> print_tree = [&](const json& nodes, int depth) {
            if (!nodes.is_array()) return;
            for (auto& n : nodes) {
                auto it = state.rects.find(GetStr(n, "id"));
                if (it != state.rects.end()) {
                    const ImRect& r = it->second.rect;
                    printf("%*s%s %s %d %d %d %d\n", depth * 2, "", GetStr(n, "id").c_str(), GetStr(n, "type").c_str(),
                           (int)std::lround(r.Min.x - x0), (int)std::lround(r.Min.y - y0), (int)std::lround(r.GetWidth()), (int)std::lround(r.GetHeight()));
                }
                if (n.contains("children")) print_tree(n["children"], depth + 1);
            }
        };
        print_tree(doc["windows"], 0);
        printf("(Only widgets on the visible tab are listed; use --show NODE_ID to render another tab.)\n");
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
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
    const std::string cmd = argv[0];
    const Args args(argc - 1, argv + 1);
    if (cmd == "render") return Render(args, data_dir);
    if (cmd == "compare") return Compare(args);
    if (cmd == "inspect") return Inspect(args);
    if (cmd == "crop") return Crop(args);
    if (cmd == "fonts") return Fonts(args);
    if (cmd == "import-font") return ImportFont(args);
    fprintf(stderr, "unknown tool '%s'\n", cmd.c_str());
    return 2;
}
