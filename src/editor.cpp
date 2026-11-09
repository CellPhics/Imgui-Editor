#include "editor.h"
#include "exporter.h"
#include "mm_widgets.h"
#include "theme.h"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

static bool HasExt(const std::string& path, std::initializer_list<const char*> exts)
{
    std::string e = fs::path(path).extension().string();
    std::transform(e.begin(), e.end(), e.begin(), ::tolower);
    for (auto* x : exts) if (e == x) return true;
    return false;
}

static bool IsImageFile(const std::string& p) { return HasExt(p, {".png", ".jpg", ".jpeg", ".webp", ".bmp", ".gif", ".tga"}); }

static bool HaveCommand(const char* name)
{
    std::string cmd = std::string("command -v ") + name + " >/dev/null 2>&1";
    return system(cmd.c_str()) == 0;
}

// Runs a command and returns its stdout (binary safe).
static std::string RunCapture(const std::string& cmd, int* status = nullptr)
{
    std::string out;
    FILE* f = popen(cmd.c_str(), "r");
    if (!f) { if (status) *status = -1; return out; }
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    int st = pclose(f);
    if (status) *status = st;
    return out;
}

static std::string ExpandHome(std::string p)
{
    while (!p.empty() && (p.back() == ' ' || p.back() == '\n')) p.pop_back();
    const char* home = getenv("HOME");
    if (home && (p == "~" || p.rfind("~/", 0) == 0)) p = std::string(home) + p.substr(1);
    return p;
}

static std::string ShellQuote(const std::string& s)
{
    std::string o = "'";
    for (char c : s) o += c == '\'' ? std::string("'\\''") : std::string(1, c);
    return o + "'";
}

static double Now()
{
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

static std::string IL(const char* icon, const std::string& text)
{
    std::string i = IconUtf8(icon);
    return text.empty() ? i : i + "  " + text;
}

static std::string DisplayName(const json& n)
{
    for (const char* k : {"label", "title", "text", "header"}) {
        std::string s = JS(n, k);
        if (!s.empty()) return s.size() > 40 ? s.substr(0, 40) + "..." : s;
    }
    const TypeDef* t = FindType(JS(n, "type"));
    return t ? t->name : JS(n, "type");
}

// ============================================================ init / settings

bool Editor::Init(const std::string& data_dir, const std::string& config_dir, const std::string& project_dir)
{
    std::error_code ec_init_;
    data_dir_ = data_dir;
    config_dir_ = config_dir;
    exe_path_ = fs::read_symlink("/proc/self/exe", ec_init_).string();
    std::error_code ec;
    fs::create_directories(config_dir_, ec);
    ini_path_ = (fs::path(config_dir_) / "imgui.ini").string();
    ImGui::GetIO().IniFilename = ini_path_.c_str();
    if (!FileExists(ini_path_)) layout_reset_ = true;
    LoadSettings();
    assets_.data_dir = data_dir_;
    ApplyEditorStyle();

    std::string dir = project_dir;
    if (dir.empty()) dir = settings_.last_project;
    if (dir.empty() || !OpenProject(dir, false)) {
        const char* home = getenv("HOME");
        std::string def = (fs::path(home ? home : ".") / "ImGuiMenus" / "MyMenu").string();
        if (!OpenProject(dir.empty() ? def : dir, true)) return false;
    }
    return true;
}

void Editor::LoadSettings()
{
    bool ok = false;
    std::string s = ReadFile((fs::path(config_dir_) / "settings.json").string(), &ok);
    if (!ok) return;
    json j = json::parse(s, nullptr, false);
    if (!j.is_object()) return;
    settings_.last_project = JS(j, "last_project");
    settings_.claude_path = JS(j, "claude_path");
    settings_.model = JS(j, "model", settings_.model);
    settings_.mode = JS(j, "mode", settings_.mode);
    settings_.ui_scale = JF(j, "ui_scale", 1.0f);
}

void Editor::SaveSettings()
{
    json j = {{"last_project", settings_.last_project}, {"claude_path", settings_.claude_path}, {"model", settings_.model},
              {"mode", settings_.mode}, {"ui_scale", settings_.ui_scale}};
    WriteFileAtomic((fs::path(config_dir_) / "settings.json").string(), j.dump(2));
}

void Editor::ApplyEditorStyle()
{
    ImGuiStyle& s = ImGui::GetStyle();
    s = ImGuiStyle();
    ImGui::StyleColorsDark(&s);
    s.WindowRounding = 6; s.FrameRounding = 4; s.ChildRounding = 4; s.PopupRounding = 4; s.GrabRounding = 4; s.TabRounding = 4;
    s.WindowPadding = ImVec2(10, 10); s.FramePadding = ImVec2(7, 4); s.ItemSpacing = ImVec2(8, 6);
    s.WindowBorderSize = 1; s.FrameBorderSize = 0; s.DockingSeparatorSize = 2;
    ImVec4* c = s.Colors;
    const ImVec4 bg(0.105f, 0.11f, 0.125f, 1), panel(0.13f, 0.135f, 0.155f, 1), frame(0.18f, 0.185f, 0.21f, 1);
    const ImVec4 acc(0.42f, 0.55f, 0.98f, 1);
    c[ImGuiCol_WindowBg] = panel; c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0); c[ImGuiCol_PopupBg] = ImVec4(0.12f, 0.125f, 0.145f, 0.98f);
    c[ImGuiCol_Border] = ImVec4(0.2f, 0.21f, 0.24f, 1);
    c[ImGuiCol_FrameBg] = frame; c[ImGuiCol_FrameBgHovered] = ImVec4(0.22f, 0.23f, 0.27f, 1); c[ImGuiCol_FrameBgActive] = ImVec4(0.25f, 0.27f, 0.33f, 1);
    c[ImGuiCol_TitleBg] = bg; c[ImGuiCol_TitleBgActive] = bg; c[ImGuiCol_MenuBarBg] = bg;
    c[ImGuiCol_Button] = frame; c[ImGuiCol_ButtonHovered] = ImVec4(0.25f, 0.27f, 0.33f, 1); c[ImGuiCol_ButtonActive] = acc;
    c[ImGuiCol_Header] = ImVec4(acc.x, acc.y, acc.z, 0.25f); c[ImGuiCol_HeaderHovered] = ImVec4(acc.x, acc.y, acc.z, 0.35f); c[ImGuiCol_HeaderActive] = ImVec4(acc.x, acc.y, acc.z, 0.5f);
    c[ImGuiCol_CheckMark] = acc; c[ImGuiCol_SliderGrab] = acc; c[ImGuiCol_SliderGrabActive] = acc;
    c[ImGuiCol_Tab] = bg; c[ImGuiCol_TabHovered] = ImVec4(0.22f, 0.23f, 0.27f, 1); c[ImGuiCol_TabSelected] = panel;
    c[ImGuiCol_TabDimmed] = bg; c[ImGuiCol_TabDimmedSelected] = panel;
    c[ImGuiCol_TabSelectedOverline] = acc; c[ImGuiCol_DockingPreview] = ImVec4(acc.x, acc.y, acc.z, 0.5f);
    c[ImGuiCol_Separator] = ImVec4(0.2f, 0.21f, 0.24f, 1); c[ImGuiCol_TextSelectedBg] = ImVec4(acc.x, acc.y, acc.z, 0.35f);
    c[ImGuiCol_DockingEmptyBg] = bg;
    const float scale = content_scale * settings_.ui_scale;
    s.ScaleAllSizes(scale);
    s.FontScaleDpi = scale;
}

// ============================================================ project

void Editor::CreateProjectFiles(const std::string& dir)
{
    std::error_code ec;
    fs::create_directories(fs::path(dir) / "assets/fonts", ec);
    fs::create_directories(fs::path(dir) / "assets/images", ec);
    for (auto* f : {"Roboto-Medium.ttf", "Karla-Regular.ttf", "Cousine-Regular.ttf"}) {
        fs::path to = fs::path(dir) / "assets/fonts" / f;
        if (!fs::exists(to)) fs::copy_file(fs::path(data_dir_) / "fonts" / f, to, ec);
    }
    fs::path menu = fs::path(dir) / "menu.json";
    if (!fs::exists(menu)) WriteFileAtomic(menu.string(), DefaultDoc().dump(2));
}

bool Editor::OpenProject(const std::string& dir_in, bool create_if_missing)
{
    std::error_code ec;
    fs::path dir = fs::absolute(dir_in, ec);
    fs::path menu = dir / "menu.json";
    if (!fs::exists(menu)) {
        if (!create_if_missing) return false;
        CreateProjectFiles(dir.string());
    }
    // Keep the Claude guide in every project up to date.
    std::string guide = ReadFile((fs::path(data_dir_) / "project_template/CLAUDE.md").string());
    const std::string tool_cmd = (exe_path_.find(' ') != std::string::npos ? "\"" + exe_path_ + "\"" : exe_path_) + " tool";
    for (size_t k; (k = guide.find("{{TOOL}}")) != std::string::npos;) guide.replace(k, 8, tool_cmd);
    if (!guide.empty() && ReadFile((dir / "CLAUDE.md").string()) != guide) WriteFileAtomic((dir / "CLAUDE.md").string(), guide);

    project_dir_ = dir.string();
    menu_path_ = menu.string();
    assets_.project_dir = project_dir_;
    undo_.clear();
    redo_.clear();
    selected_.clear();
    preview_ = PreviewCtx();
    load_error_.clear();
    file_mtime_ = 0;
    doc_ = json();
    committed_.clear();
    PollFile(true);
    if (!doc_.is_object()) { // unreadable file: start from default but don't overwrite it
        doc_ = DefaultDoc();
        Normalize(doc_);
        committed_ = doc_.dump();
    }
    export_dir_ = (dir / "export").string();
    settings_.last_project = project_dir_;
    SaveSettings();
    bridge_.Stop();
    bridge_.messages.clear();
    bridge_.SetSessionId("");
    bridge_.messages.push_back({ChatMsg::Info, "Project: " + dir.filename().string()});
    chat_seen_ = 0;
    status_ = "Opened " + project_dir_;
    status_time_ = Now();
    glfwSetWindowTitle(glfwGetCurrentContext(), ("ImGui Menu Editor — " + JS(doc_, "name", "Menu")).c_str());
    return true;
}

void Editor::Save()
{
    if (menu_path_.empty()) return;
    if (WriteFileAtomic(menu_path_, doc_.dump(2))) file_mtime_ = FileMTime(menu_path_);
    else { status_ = "Couldn't save " + menu_path_; status_time_ = Now(); }
}

void Editor::SetDoc(const json& d)
{
    doc_ = d;
    if (!selected_.empty() && !FindNode(doc_, selected_)) selected_.clear();
    preview_.selection_changed = true;
}

void Editor::PollFile(bool force)
{
    if (menu_path_.empty()) return;
    double t = Now();
    if (!force && t - last_poll_ < 0.25) return;
    last_poll_ = t;
    long long mt = FileMTime(menu_path_);
    if (mt == 0 || (mt == file_mtime_ && !force)) return;
    file_mtime_ = mt;
    bool ok = false;
    std::string text = ReadFile(menu_path_, &ok);
    if (!ok) return;
    json d = json::parse(text, nullptr, false);
    if (d.is_discarded()) {
        try { json probe = json::parse(text); (void)probe; } catch (const std::exception& e) { load_error_ = e.what(); }
        if (load_error_.empty()) load_error_ = "menu.json is not valid JSON";
        return;
    }
    load_error_.clear();
    warnings_ = Normalize(d);
    std::string dump = d.dump();
    if (dump == committed_) return;
    if (!committed_.empty()) {
        if (dirty_) CommitIfIdle();
        undo_.push_back(committed_);
        redo_.clear();
    }
    committed_ = dump;
    dirty_ = false;
    SetDoc(d);
    if (!warnings_.empty()) Save(); // write back fixed ids etc.
    status_ = "Reloaded menu.json";
    status_time_ = t;
}

void Editor::CommitIfIdle()
{
    if (!dirty_ || ImGui::IsAnyItemActive() || ImGui::IsMouseDown(ImGuiMouseButton_Left)) return;
    dirty_ = false;
    std::string dump = doc_.dump();
    if (dump == committed_) return;
    undo_.push_back(committed_);
    if (undo_.size() > 300) undo_.erase(undo_.begin());
    redo_.clear();
    committed_ = dump;
    Save();
}

void Editor::Undo()
{
    if (dirty_) { dirty_ = false; std::string d = doc_.dump(); if (d != committed_) { undo_.push_back(committed_); committed_ = d; } }
    if (undo_.empty()) return;
    redo_.push_back(committed_);
    committed_ = undo_.back();
    undo_.pop_back();
    SetDoc(json::parse(committed_));
    Save();
}

void Editor::Redo()
{
    if (redo_.empty()) return;
    undo_.push_back(committed_);
    committed_ = redo_.back();
    redo_.pop_back();
    SetDoc(json::parse(committed_));
    Save();
}

// ============================================================ selection / edits

void Editor::Select(const std::string& id)
{
    if (id == selected_) return;
    selected_ = id;
    preview_.selection_changed = true;
    reveal_.clear();
    auto path = PathTo(doc_, id);
    for (size_t i = 0; i + 1 < path.size(); i++) reveal_.insert(path[i]);
    scroll_to_sel_ = true;
}

json* Editor::Selected()
{
    NodeRef r = FindNode(doc_, selected_);
    return r.node;
}

void Editor::AddNode(const std::string& type) { InsertNode(MakeNode(type, doc_)); }

void Editor::InsertNode(json node)
{
    const std::string type = JS(node, "type");
    if (type == "window") {
        doc_["windows"].push_back(node);
        Select(JS(node, "id"));
        MarkDirty();
        return;
    }
    json* target = nullptr;
    int index = -1;
    NodeRef sel = FindNode(doc_, selected_);
    if (sel && CanContain(sel.node, type)) target = sel.node;
    else if (sel) {
        auto path = PathTo(doc_, selected_);
        for (int i = (int)path.size() - 2; i >= 0 && !target; i--) {
            NodeRef r = FindNode(doc_, path[i]);
            if (!r || !CanContain(r.node, type)) continue;
            target = r.node;
            json& arr = (*target)["children"];
            for (int k = 0; k < (int)arr.size(); k++)
                if (JS(arr[k], "id") == path[i + 1]) index = k + 1;
        }
    }
    if (!target) {
        // First container anywhere that accepts this type.
        std::function<json*(json&)> find = [&](json& arr) -> json* {
            for (auto& n : arr) {
                if (CanContain(&n, type)) return &n;
                if (n.contains("children"))
                    if (json* r = find(n["children"])) return r;
            }
            return nullptr;
        };
        target = find(doc_["windows"]);
    }
    if (!target && type != "tab") {
        doc_["windows"].push_back(MakeNode("window", doc_));
        target = &doc_["windows"].back();
    }
    if (!target) { status_ = "Add a Tab bar first, then tabs can go inside it."; status_time_ = Now(); return; }
    json* arr = ChildArray(doc_, target);
    std::string id = JS(node, "id");
    if (index >= 0 && index <= (int)arr->size()) arr->insert(arr->begin() + index, node);
    else arr->push_back(node);
    Select(id);
    MarkDirty();
}

void Editor::DeleteSelected()
{
    NodeRef r = FindNode(doc_, selected_);
    if (!r) return;
    std::string next;
    if (r.index + 1 < (int)r.parent_array->size()) next = JS((*r.parent_array)[r.index + 1], "id");
    else if (r.index > 0) next = JS((*r.parent_array)[r.index - 1], "id");
    else if (r.parent) next = JS(*r.parent, "id");
    r.parent_array->erase(r.parent_array->begin() + r.index);
    selected_.clear();
    Select(next);
    MarkDirty();
}

void Editor::DuplicateSelected()
{
    NodeRef r = FindNode(doc_, selected_);
    if (!r) return;
    json copy = CloneWithNewIds(*r.node, doc_);
    r.parent_array->insert(r.parent_array->begin() + r.index + 1, copy);
    Select(JS(copy, "id"));
    MarkDirty();
}

void Editor::MoveSelected(int dir)
{
    NodeRef r = FindNode(doc_, selected_);
    if (!r) return;
    int j = r.index + dir;
    if (j < 0 || j >= (int)r.parent_array->size()) return;
    std::swap((*r.parent_array)[r.index], (*r.parent_array)[j]);
    MarkDirty();
}

void Editor::MoveNode(const std::string& id, const std::string& target, bool into)
{
    if (id == target || IsAncestor(doc_, id, target)) return;
    NodeRef src = FindNode(doc_, id);
    NodeRef dst = FindNode(doc_, target);
    if (!src || !dst) return;
    json node = *src.node;
    const std::string type = JS(node, "type");
    if (into && !CanContain(dst.node, type)) into = false;
    if (!into && !CanContain(dst.parent, type)) { status_ = "Can't put a " + type + " there"; status_time_ = Now(); return; }
    src.parent_array->erase(src.parent_array->begin() + src.index);
    dst = FindNode(doc_, target);
    if (into) ChildArray(doc_, dst.node)->push_back(node);
    else dst.parent_array->insert(dst.parent_array->begin() + dst.index + 1, node);
    selected_.clear();
    Select(id);
    MarkDirty();
}

// ============================================================ frame

void Editor::BeforeFrame()
{
    if (doc_.is_object()) {
        assets_.SyncFonts(doc_);
        if (Now() - last_image_sync_ > 0.5) { assets_.SyncImages(doc_); last_image_sync_ = Now(); }
    }
}

void Editor::Shutdown()
{
    if (async_.joinable()) { if (async_busy_) async_.detach(); else async_.join(); }
    if (dirty_) { dirty_ = false; Save(); }
    bridge_.Stop();
    SaveSettings();
}

void Editor::Frame()
{
    PollFile();
    PollAsync();
    bridge_.Poll();
    if (bridge_.TakeFinished()) {
        PollFile(true);
        if (!load_error_.empty()) {
            bridge_.messages.push_back({ChatMsg::Error, "menu.json is not valid JSON after that change:\n" + load_error_});
            json_error_offered_ = true;
        }
    }

    ImGuiIO& io = ImGui::GetIO();
    if (!io.WantTextInput) {
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z)) Undo();
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Y) || ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z)) Redo();
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_D)) DuplicateSelected();
        if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) DeleteSelected();
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_E)) preview_.edit_mode = !preview_.edit_mode;
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S)) { dirty_ = false; Save(); status_ = "Saved"; status_time_ = Now(); }
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S)) FileSaveAs();
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_O)) FileOpen();
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_N)) FileNew();
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_C)) {
            if (json* n = Selected()) { clipboard_ = n->dump(2); ImGui::SetClipboardText(clipboard_.c_str()); }
        }
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_V)) {
            const char* clip = ImGui::GetClipboardText();
            json n = json::parse(clip ? clip : "", nullptr, false);
            if (n.is_object() && FindType(JS(n, "type"))) InsertNode(CloneWithNewIds(n, doc_));
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) selected_.clear();
    }

    MenuBar();
    ImGuiID dock = ImGui::GetID("MainDock");
    if (layout_reset_ || !ImGui::DockBuilderGetNode(dock)) { SetupDock(dock); layout_reset_ = false; }
    dock_id_ = ImGui::DockSpaceOverViewport(dock, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);

    Hierarchy();
    Inspector();
    ThemePanel();
    AssetsPanel();
    ClaudePanel();
    CodePanel();
    Canvas();
    ExportPopup();
    ProjectPopup();

    auto later = std::move(later_);
    later_.clear();
    for (auto& f : later) f();
    CommitIfIdle();
}

void Editor::SetupDock(unsigned dock_id)
{
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::DockBuilderRemoveNode(dock_id);
    ImGui::DockBuilderAddNode(dock_id, (ImGuiDockNodeFlags)ImGuiDockNodeFlags_DockSpace | ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::DockBuilderSetNodeSize(dock_id, vp->WorkSize);
    ImGuiID center = dock_id;
    ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.19f, nullptr, &center);
    ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.30f, nullptr, &center);
    ImGuiID left_bottom = ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.38f, nullptr, &left);
    ImGuiID right_bottom = ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.55f, nullptr, &right);
    ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.22f, nullptr, &center);
    ImGui::DockBuilderDockWindow("Hierarchy", left);
    ImGui::DockBuilderDockWindow("Assets", left_bottom);
    ImGui::DockBuilderDockWindow("Inspector", right);
    ImGui::DockBuilderDockWindow("Theme", right);
    ImGui::DockBuilderDockWindow("Claude", right_bottom);
    ImGui::DockBuilderDockWindow("Code", bottom);
    ImGui::DockBuilderFinish(dock_id);
}

// ============================================================ menu bar

void Editor::MenuBar()
{
    if (!ImGui::BeginMainMenuBar()) return;
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem(IL("file-circle-plus", "New...").c_str(), "Ctrl+N")) FileNew();
        if (ImGui::MenuItem(IL("folder-open", "Open...").c_str(), "Ctrl+O")) FileOpen();
        if (ImGui::MenuItem(IL("floppy-disk", "Save").c_str(), "Ctrl+S")) { dirty_ = false; Save(); status_ = "Saved"; status_time_ = Now(); }
        if (ImGui::MenuItem(IL("copy", "Save As...").c_str(), "Ctrl+Shift+S")) FileSaveAs();
        ImGui::Separator();
        if (ImGui::MenuItem(IL("file-export", "Export C++...").c_str())) open_export_ = true;
        if (ImGui::MenuItem(IL("link", "Copy project path").c_str())) ImGui::SetClipboardText(project_dir_.c_str());
        ImGui::Separator();
        if (ImGui::MenuItem("Quit")) quit = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, !undo_.empty() || dirty_)) Undo();
        if (ImGui::MenuItem("Redo", "Ctrl+Y", false, !redo_.empty())) Redo();
        ImGui::Separator();
        if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, !selected_.empty())) DuplicateSelected();
        if (ImGui::MenuItem("Delete", "Del", false, !selected_.empty())) DeleteSelected();
        if (ImGui::MenuItem("Copy", "Ctrl+C", false, !selected_.empty())) {
            if (json* n = Selected()) ImGui::SetClipboardText(n->dump(2).c_str());
        }
        if (ImGui::MenuItem("Paste", "Ctrl+V")) {
            const char* clip = ImGui::GetClipboardText();
            json n = json::parse(clip ? clip : "", nullptr, false);
            if (n.is_object() && FindType(JS(n, "type"))) InsertNode(CloneWithNewIds(n, doc_));
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem("Edit mode (click to select)", "Ctrl+E", &preview_.edit_mode);
        if (ImGui::MenuItem("Reset widget values")) preview_.reset_values = true;
        if (ImGui::MenuItem("Replay animations")) mm::ResetAnimations();
        if (ImGui::MenuItem("Reset panel layout")) layout_reset_ = true;
        ImGui::Separator();
        float sc = settings_.ui_scale;
        ImGui::SetNextItemWidth(120);
        if (ImGui::SliderFloat("UI scale", &sc, 0.75f, 2.0f, "%.2f")) settings_.ui_scale = sc;
        if (ImGui::IsItemDeactivatedAfterEdit()) { ApplyEditorStyle(); SaveSettings(); }
        ImGui::EndMenu();
    }

    ImGui::Separator();
    // Mode switch
    const ImVec4 acc = ImGui::GetStyle().Colors[ImGuiCol_CheckMark];
    if (preview_.edit_mode) ImGui::PushStyleColor(ImGuiCol_Text, acc);
    if (ImGui::MenuItem(IL("arrow-pointer", "Edit").c_str(), nullptr, preview_.edit_mode)) preview_.edit_mode = true;
    if (preview_.edit_mode) ImGui::PopStyleColor();
    if (!preview_.edit_mode) ImGui::PushStyleColor(ImGuiCol_Text, acc);
    if (ImGui::MenuItem(IL("hand-pointer", "Interact").c_str(), nullptr, !preview_.edit_mode)) preview_.edit_mode = false;
    if (!preview_.edit_mode) ImGui::PopStyleColor();
    ImGui::Separator();

    // Quick theme switch
    std::string cur = JS(doc_, "theme");
    ImGui::SetNextItemWidth(150);
    if (ImGui::BeginCombo("##theme", IL("palette", cur).c_str())) {
        for (auto& [name, t] : doc_["themes"].items()) {
            (void)t;
            if (ImGui::Selectable(name.c_str(), name == cur)) { doc_["theme"] = name; MarkDirty(); }
        }
        ImGui::EndCombo();
    }
    if (ImGui::MenuItem(IL("file-export", "Export").c_str())) open_export_ = true;

    // Status on the right
    std::string right;
    if (!load_error_.empty()) right = IL("triangle-exclamation", "menu.json has a syntax error");
    else if (!status_.empty() && Now() - status_time_ < 4.0) right = status_;
    else right = project_dir_;
    float w = ImGui::CalcTextSize(right.c_str()).x;
    ImGui::SetCursorPosX(ImMax(ImGui::GetCursorPosX() + 10, ImGui::GetWindowWidth() - w - 16));
    if (!load_error_.empty()) ImGui::TextColored(ImVec4(1, 0.45f, 0.4f, 1), "%s", right.c_str());
    else ImGui::TextDisabled("%s", right.c_str());
    if (!load_error_.empty()) ImGui::SetItemTooltip("%s", load_error_.c_str());
    ImGui::EndMainMenuBar();
}

// ============================================================ canvas

void Editor::Canvas()
{
    ImGuiDockNode* central = ImGui::DockBuilderGetCentralNode(dock_id_);
    if (!central) return;
    const ImVec2 p0 = central->Pos, p1 = central->Pos + central->Size;
    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    bg->AddRectFilled(p0, p1, IM_COL32(20, 21, 25, 255));
    const float step = 32.0f * content_scale * settings_.ui_scale;
    for (float x = p0.x; x < p1.x; x += step) bg->AddLine(ImVec2(x, p0.y), ImVec2(x, p1.y), IM_COL32(255, 255, 255, 8));
    for (float y = p0.y; y < p1.y; y += step) bg->AddLine(ImVec2(p0.x, y), ImVec2(p1.x, y), IM_COL32(255, 255, 255, 8));
    const char* hint = preview_.edit_mode ? "Edit mode: click a widget to select it  |  Ctrl+E to interact  |  drag windows to move them"
                                          : "Interact mode: use the menu like in game  |  Ctrl+E to go back to editing";
    bg->AddText(ImVec2(p0.x + 12, p1.y - ImGui::GetFontSize() - 10), IM_COL32(255, 255, 255, 70), hint);

    preview_.origin = p0;
    preview_.selected = selected_;
    RenderPreview(doc_, assets_, preview_);
    if (preview_.doc_changed) MarkDirty();
    if (preview_.hovered.empty()) preview_.hovered = hier_hover_;
    hier_hover_.clear();
    if (!preview_.clicked.empty()) Select(preview_.clicked);
    else if (preview_.edit_mode && ImGui::IsMouseClicked(0) && !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) &&
             ImGui::IsMouseHoveringRect(p0, p1, false))
        selected_.clear();
    DrawSelectionOverlay(preview_, ImGui::GetColorU32(ImGuiCol_CheckMark));
}

// ============================================================ hierarchy

void Editor::Hierarchy()
{
    if (!ImGui::Begin("Hierarchy")) { ImGui::End(); return; }
    if (ImGui::Button(IL("plus", "Add").c_str())) ImGui::OpenPopup("add_menu");
    if (ImGui::BeginPopup("add_menu")) {
        ImGui::TextDisabled("Adds into the selection (or next to it)");
        for (auto& t : Types()) {
            if (ImGui::Selectable(IL(t.icon.c_str(), t.name).c_str())) AddNode(t.type);
        }
        ImGui::EndPopup();
    }
    const bool has = !selected_.empty();
    ImGui::BeginDisabled(!has);
    ImGui::SameLine(); if (ImGui::Button(IconUtf8("clone"))) DuplicateSelected(); ImGui::SetItemTooltip("Duplicate (Ctrl+D)");
    ImGui::SameLine(); if (ImGui::Button(IconUtf8("trash"))) DeleteSelected(); ImGui::SetItemTooltip("Delete (Del)");
    ImGui::SameLine(); if (ImGui::Button(IconUtf8("arrow-up"))) MoveSelected(-1); ImGui::SetItemTooltip("Move up");
    ImGui::SameLine(); if (ImGui::Button(IconUtf8("arrow-down"))) MoveSelected(1); ImGui::SetItemTooltip("Move down");
    if (json* n = Selected()) {
        ImGui::SameLine();
        bool hidden = JB(*n, "hidden", false);
        if (ImGui::Button(IconUtf8(hidden ? "eye-slash" : "eye"))) { if (hidden) n->erase("hidden"); else (*n)["hidden"] = true; MarkDirty(); }
        ImGui::SetItemTooltip(hidden ? "Show" : "Hide");
    }
    ImGui::EndDisabled();
    ImGui::Separator();

    ImGui::BeginChild("tree");
    for (auto& w : doc_["windows"]) HierarchyNode(w, nullptr);
    ImGui::Dummy(ImVec2(0, 40));
    ImGui::EndChild();
    reveal_.clear();
    scroll_to_sel_ = false;
    ImGui::End();
}

void Editor::HierarchyNode(json& n, json* parent)
{
    (void)parent;
    if (!n.is_object()) return;
    const std::string id = JS(n, "id");
    const std::string type = JS(n, "type");
    const TypeDef* t = FindType(type);
    const bool container = t && t->container;
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth |
                               ImGuiTreeNodeFlags_FramePadding;
    if (!container || !n.contains("children") || n["children"].empty()) flags |= ImGuiTreeNodeFlags_Leaf;
    if (type == "window" || type == "tabs") flags |= ImGuiTreeNodeFlags_DefaultOpen;
    if (id == selected_) flags |= ImGuiTreeNodeFlags_Selected;
    if (reveal_.count(id)) ImGui::SetNextItemOpen(true);

    const bool hidden = JB(n, "hidden", false);
    if (hidden) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    std::string label = IL(t ? t->icon.c_str() : "question", DisplayName(n));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));
    const bool open = ImGui::TreeNodeEx(id.c_str(), flags, "%s", label.c_str());
    ImGui::PopStyleVar();
    if (hidden) ImGui::PopStyleColor();
    if (id == selected_ && scroll_to_sel_) ImGui::SetScrollHereY(0.4f);
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) Select(id);
    if (ImGui::IsItemHovered()) {
        ImGui::SetItemTooltip("%s  (%s)", id.c_str(), type.c_str());
        hier_hover_ = id;
    }
    // id on the right
    {
        ImVec2 r = ImGui::GetItemRectMax();
        float tw = ImGui::CalcTextSize(id.c_str()).x;
        float x = r.x - tw - 6;
        if (x > ImGui::GetItemRectMin().x + ImGui::CalcTextSize(label.c_str()).x + 40)
            ImGui::GetWindowDrawList()->AddText(ImVec2(x, ImGui::GetItemRectMin().y + 2), ImGui::GetColorU32(ImGuiCol_TextDisabled), id.c_str());
    }

    if (ImGui::BeginPopupContextItem()) {
        Select(id);
        if (container && ImGui::BeginMenu(IL("plus", "Add inside").c_str())) {
            for (auto& tt : Types())
                if (CanContain(&n, tt.type) && ImGui::MenuItem(IL(tt.icon.c_str(), tt.name).c_str())) {
                    json node = MakeNode(tt.type, doc_);
                    std::string nid = JS(node, "id");
                    Later([this, id, node]() {
                        NodeRef r = FindNode(doc_, id);
                        if (!r) return;
                        ChildArray(doc_, r.node)->push_back(node);
                        Select(JS(node, "id"));
                        MarkDirty();
                    });
                }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Duplicate", "Ctrl+D")) Later([this] { DuplicateSelected(); });
        if (ImGui::MenuItem("Delete", "Del")) Later([this] { DeleteSelected(); });
        if (ImGui::MenuItem("Move up")) Later([this] { MoveSelected(-1); });
        if (ImGui::MenuItem("Move down")) Later([this] { MoveSelected(1); });
        if (ImGui::MenuItem(hidden ? "Show" : "Hide")) Later([this, id, hidden] {
            NodeRef r = FindNode(doc_, id);
            if (!r) return;
            if (hidden) r.node->erase("hidden"); else (*r.node)["hidden"] = true;
            MarkDirty();
        });
        if (ImGui::MenuItem("Copy JSON", "Ctrl+C")) ImGui::SetClipboardText(n.dump(2).c_str());
        if (ImGui::MenuItem("Ask Claude about this...")) { prompt_ = "For \"" + id + "\": "; ImGui::SetWindowFocus("Claude"); }
        ImGui::EndPopup();
    }

    if (ImGui::BeginDragDropSource()) {
        ImGui::SetDragDropPayload("MM_NODE", id.c_str(), id.size() + 1);
        ImGui::TextUnformatted(label.c_str());
        ImGui::EndDragDropSource();
    }
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("MM_NODE")) {
            std::string src = (const char*)p->Data;
            NodeRef s = FindNode(doc_, src);
            bool into = s && container && CanContain(&n, JS(*s.node, "type"));
            Later([this, src, id, into] { MoveNode(src, id, into); });
        }
        ImGui::EndDragDropTarget();
    }

    if (open) {
        if (container && n.contains("children"))
            for (auto& c : n["children"]) HierarchyNode(c, &n);
        ImGui::TreePop();
    }
}

// ============================================================ inspector

static bool InputStd(const char* label, std::string& s, ImGuiInputTextFlags f = 0) { return ImGui::InputText(label, &s, f); }

void Editor::PropEditor(json& n, const PropDef& p)
{
    using namespace ImGui;
    const bool set = n.contains(p.key);
    const json cur = set ? n[p.key] : p.def;
    PushID(p.key.c_str());
    TableNextRow();
    TableNextColumn();
    AlignTextToFramePadding();
    if (set) TextUnformatted(p.key.c_str()); else TextDisabled("%s", p.key.c_str());
    if (!p.help.empty()) SetItemTooltip("%s", p.help.c_str());
    TableNextColumn();
    const float reset_w = set ? GetFrameHeight() + GetStyle().ItemSpacing.x : 0;
    SetNextItemWidth(-FLT_MIN - reset_w);
    bool changed = false;
    json val = cur;

    switch (p.kind) {
    case PK::Bool: {
        bool b = cur.is_boolean() && cur.get<bool>();
        if (Checkbox("##v", &b)) { val = b; changed = true; }
        break;
    }
    case PK::Int: {
        int v = cur.is_number() ? (int)cur.get<double>() : 0;
        if (p.vmax > p.vmin ? DragInt("##v", &v, 0.2f, (int)p.vmin, (int)p.vmax) : DragInt("##v", &v)) { val = v; changed = true; }
        break;
    }
    case PK::Float: {
        float v = cur.is_number() ? cur.get<float>() : 0.0f;
        float speed = p.vmax > p.vmin ? (p.vmax - p.vmin) / 400.0f : 0.1f;
        if (DragFloat("##v", &v, speed, p.vmin, p.vmax, "%.3g")) { val = std::round(v * 1000.0f) / 1000.0f; changed = true; }
        break;
    }
    case PK::String: case PK::Key: {
        std::string s = cur.is_string() ? cur.get<std::string>() : (cur.is_null() ? "" : cur.dump());
        if (p.kind == PK::Key) {
            int k = KeyFromName(s);
            if (mm::Keybind("##kb", &k)) { val = KeyName(k); changed = true; }
            SameLine();
            SetNextItemWidth(-FLT_MIN - reset_w);
        }
        if (InputStd("##v", s)) { val = s; changed = true; }
        break;
    }
    case PK::Text: {
        std::string s = cur.is_string() ? cur.get<std::string>() : "";
        if (InputTextMultiline("##v", &s, ImVec2(-FLT_MIN - reset_w, GetTextLineHeight() * 3.5f))) { val = s; changed = true; }
        break;
    }
    case PK::Color: {
        ImVec4 c(1, 1, 1, 1);
        bool has = ParseColor(cur, c);
        if (!has && cur.is_null()) {
            if (Button("Set color")) { val = json::array({1, 1, 1, 1}); changed = true; }
        } else if (ColorEdit4("##v", &c.x, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf)) {
            val = json::array({std::round(c.x * 1000) / 1000, std::round(c.y * 1000) / 1000, std::round(c.z * 1000) / 1000, std::round(c.w * 1000) / 1000});
            changed = true;
        }
        break;
    }
    case PK::Vec2: {
        ImVec2 v(0, 0);
        bool has = cur.is_array() && cur.size() == 2;
        if (has) v = ImVec2(cur[0].get<float>(), cur[1].get<float>());
        if (!has && cur.is_null()) {
            if (Button("Override")) { val = json::array({0, 0}); changed = true; }
        } else if (DragFloat2("##v", &v.x, 0.5f, -1, 4000, "%.0f")) { val = json::array({(int)v.x, (int)v.y}); changed = true; }
        break;
    }
    case PK::Items: {
        std::string s;
        if (cur.is_array())
            for (size_t i = 0; i < cur.size(); i++) s += (i ? "\n" : "") + (cur[i].is_string() ? cur[i].get<std::string>() : cur[i].dump());
        if (InputTextMultiline("##v", &s, ImVec2(-FLT_MIN - reset_w, GetTextLineHeight() * (std::min<size_t>(cur.size(), 6) + 1.5f)))) {
            json arr = json::array();
            size_t start = 0;
            while (start <= s.size()) {
                size_t e = s.find('\n', start);
                if (e == std::string::npos) e = s.size();
                arr.push_back(s.substr(start, e - start));
                start = e + 1;
            }
            val = arr;
            changed = true;
        }
        SetItemTooltip("One item per line");
        break;
    }
    case PK::BoolList: {
        auto items = JItems(n, "items");
        json arr = cur.is_array() ? cur : json::array();
        for (size_t i = 0; i < items.size(); i++) {
            bool b = i < arr.size() && arr[i].is_boolean() && arr[i].get<bool>();
            PushID((int)i);
            if (Checkbox(items[i].c_str(), &b)) {
                while (arr.size() < items.size()) arr.push_back(false);
                arr[i] = b;
                val = arr;
                changed = true;
            }
            PopID();
        }
        break;
    }
    case PK::Enum: {
        std::string s = cur.is_string() ? cur.get<std::string>() : "";
        if (BeginCombo("##v", s.c_str())) {
            for (auto& o : p.options)
                if (Selectable(o.c_str(), o == s)) { val = o; changed = true; }
            EndCombo();
        }
        break;
    }
    case PK::Image: case PK::Font: {
        const char* list = p.kind == PK::Image ? "images" : "fonts";
        std::string s = cur.is_string() ? cur.get<std::string>() : "";
        if (BeginCombo("##v", s.empty() ? (p.kind == PK::Font ? "(default)" : "(none)") : s.c_str())) {
            if (Selectable(p.kind == PK::Font ? "(default)" : "(none)", s.empty())) { val = ""; changed = true; }
            for (auto& e : doc_[list]) {
                std::string id = JS(e, "id");
                if (Selectable(id.c_str(), id == s)) { val = id; changed = true; }
            }
            EndCombo();
        }
        break;
    }
    case PK::Icon: {
        std::string s = cur.is_string() ? cur.get<std::string>() : "";
        const char* glyph = IconUtf8(s);
        if (Button(glyph[0] ? glyph : "...", ImVec2(GetFrameHeight() * 1.4f, 0))) OpenPopup("icon_pick");
        SetItemTooltip("Pick an icon");
        SameLine();
        SetNextItemWidth(-FLT_MIN - reset_w);
        if (InputStd("##v", s)) { val = s; changed = true; }
        if (IconPicker("icon_pick", s)) { val = s; changed = true; }
        break;
    }
    case PK::Style: {
        json o = cur.is_object() ? cur : json::object();
        ImGuiStyle base;
        mm::Theme base_mm;
        ThemeFromJson(ActiveTheme(doc_), base, base_mm);
        int count = 0;
        std::string remove_sec, remove_key;
        auto row_end = [&](const char* sec, const std::string& key) {
            SameLine();
            PushID(key.c_str());
            if (SmallButton(IconUtf8("xmark"))) { remove_sec = sec; remove_key = key; }
            PopID();
        };
        if (o.contains("colors") && o["colors"].is_object())
            for (auto& [k, v] : o["colors"].items()) {
                ImVec4 c(1, 1, 1, 1);
                ParseColor(v, c);
                PushID(("c" + k).c_str());
                SetNextItemWidth(-FLT_MIN - GetFrameHeight() * 2);
                if (ColorEdit4(k.c_str(), &c.x, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs)) {
                    o["colors"][k] = json::array({std::round(c.x * 1000) / 1000, std::round(c.y * 1000) / 1000, std::round(c.z * 1000) / 1000, std::round(c.w * 1000) / 1000});
                    changed = true;
                }
                PopID();
                row_end("colors", k);
                count++;
            }
        if (o.contains("vars") && o["vars"].is_object())
            for (auto& [k, v] : o["vars"].items()) {
                PushID(("v" + k).c_str());
                SetNextItemWidth(GetContentRegionAvail().x * 0.5f);
                if (v.is_array() && v.size() == 2) {
                    float f[2] = {v[0].get<float>(), v[1].get<float>()};
                    if (DragFloat2(k.c_str(), f, 0.1f, 0, 100, "%.1f")) { o["vars"][k] = json::array({f[0], f[1]}); changed = true; }
                } else if (v.is_number()) {
                    float f = v.get<float>();
                    if (DragFloat(k.c_str(), &f, 0.1f, 0, 100, "%.1f")) { o["vars"][k] = f; changed = true; }
                }
                PopID();
                row_end("vars", k);
                count++;
            }
        if (o.contains("mm") && o["mm"].is_object())
            for (auto& [k, v] : o["mm"].items()) {
                PushID(("m" + k).c_str());
                ImVec4 c;
                if (v.is_boolean()) { bool b = v.get<bool>(); if (Checkbox(k.c_str(), &b)) { o["mm"][k] = b; changed = true; } }
                else if (v.is_number()) { float f = v.get<float>(); SetNextItemWidth(GetContentRegionAvail().x * 0.5f); if (DragFloat(k.c_str(), &f, 0.1f)) { o["mm"][k] = f; changed = true; } }
                else if (ParseColor(v, c) && ColorEdit4(k.c_str(), &c.x, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs)) {
                    o["mm"][k] = json::array({c.x, c.y, c.z, c.w});
                    changed = true;
                }
                PopID();
                row_end("mm", k);
                count++;
            }
        if (!remove_key.empty()) {
            o[remove_sec].erase(remove_key);
            if (o[remove_sec].empty()) o.erase(remove_sec);
            changed = true;
        }
        if (SmallButton(IL("plus", count ? "Add override" : "Override a color/size...").c_str())) { OpenPopup("add_override"); icon_filter_.clear(); }
        SetNextWindowSizeConstraints(ImVec2(260, 0), ImVec2(400, 420));
        if (BeginPopup("add_override")) {
            if (IsWindowAppearing()) SetKeyboardFocusHere();
            InputTextWithHint("##f", "Filter", &icon_filter_);
            auto match = [&](const char* name) {
                if (icon_filter_.empty()) return true;
                std::string a = name, b = icon_filter_;
                std::transform(a.begin(), a.end(), a.begin(), ::tolower);
                std::transform(b.begin(), b.end(), b.begin(), ::tolower);
                return a.find(b) != std::string::npos;
            };
            auto r3 = [](float v) { return std::round(v * 1000) / 1000; };
            SeparatorText("Colors");
            for (int i = 0; i < ImGuiCol_COUNT; i++) {
                const char* name = GetStyleColorName(i);
                if (!match(name)) continue;
                if (Selectable(name)) { const ImVec4& c = base.Colors[i]; o["colors"][name] = json::array({r3(c.x), r3(c.y), r3(c.z), r3(c.w)}); changed = true; }
            }
            SeparatorText("Sizes");
            for (auto& v : StyleVars()) {
                if (!match(v.name)) continue;
                if (Selectable(v.name)) {
                    const float* f = (const float*)((const char*)&base + v.offset);
                    o["vars"][v.name] = v.count == 1 ? json(r3(f[0])) : json::array({r3(f[0]), r3(f[1])});
                    changed = true;
                }
            }
            SeparatorText("Menu widgets");
            for (auto& f : MmFields()) {
                if (!match(f.name)) continue;
                if (Selectable(f.name)) {
                    const char* p = (const char*)&base_mm + f.offset;
                    if (f.kind == 0) { const ImVec4& c = *(const ImVec4*)p; o["mm"][f.name] = json::array({r3(c.x), r3(c.y), r3(c.z), r3(c.w)}); }
                    else if (f.kind == 1) o["mm"][f.name] = *(const float*)p;
                    else o["mm"][f.name] = *(const bool*)p;
                    changed = true;
                }
            }
            EndPopup();
        }
        if (changed) val = o.empty() ? json() : o;
        break;
    }
    default: break;
    }

    if (set) {
        SameLine();
        if (Button(IconUtf8("xmark"), ImVec2(GetFrameHeight(), 0))) { n.erase(p.key); MarkDirty(); }
        SetItemTooltip("Reset to default");
    }
    if (changed && val.is_null()) { n.erase(p.key); MarkDirty(); changed = false; }
    if (changed) {
        if ((p.kind == PK::String || p.kind == PK::Icon || p.kind == PK::Image || p.kind == PK::Font) && val.is_string() &&
            val.get<std::string>().empty() && p.def.is_string() && p.def.get<std::string>().empty())
            n.erase(p.key);
        else
            n[p.key] = val;
        MarkDirty();
    }
    PopID();
}

bool Editor::IconPicker(const char* popup_id, std::string& icon)
{
    bool picked = false;
    ImGui::SetNextWindowSize(ImVec2(420, 360) * content_scale * settings_.ui_scale, ImGuiCond_Appearing);
    if (ImGui::BeginPopup(popup_id)) {
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##filter", "Search icons (e.g. gun, eye, shield)", &icon_filter_);
        ImGui::BeginChild("grid", ImVec2(0, ImGui::GetTextLineHeight() * 18));
        const float cell = ImGui::GetFrameHeight() * 1.6f;
        int cols = ImMax(1, (int)(ImGui::GetContentRegionAvail().x / (cell + ImGui::GetStyle().ItemSpacing.x)));
        int i = 0;
        if (ImGui::Button("none", ImVec2(cell * 2 + ImGui::GetStyle().ItemSpacing.x, cell))) { icon.clear(); picked = true; ImGui::CloseCurrentPopup(); }
        i += 2;
        for (auto& e : AllIcons()) {
            if (strlen(e.name) <= 1) continue; // skip digit/letter glyphs
            if (!icon_filter_.empty() && !strstr(e.name, icon_filter_.c_str())) continue;
            if (i % cols != 0) ImGui::SameLine();
            ImGui::PushID(e.name);
            std::string g = Utf8(e.codepoint);
            if (ImGui::Button(g.c_str(), ImVec2(cell, cell))) { icon = e.name; picked = true; ImGui::CloseCurrentPopup(); }
            ImGui::SetItemTooltip("%s", e.name);
            ImGui::PopID();
            i++;
        }
        ImGui::EndChild();
        ImGui::EndPopup();
    }
    return picked;
}

void Editor::Inspector()
{
    using namespace ImGui;
    if (!Begin("Inspector")) { End(); return; }
    json* n = Selected();
    if (!n) {
        TextDisabled("Nothing selected.");
        TextWrapped("Click a widget in the preview or the Hierarchy to edit it.");
        Spacing();
        SeparatorText("Menu");
        std::string name = JS(doc_, "name");
        SetNextItemWidth(-FLT_MIN);
        if (InputText("##name", &name)) { doc_["name"] = name; MarkDirty(); }
        SetItemTooltip("Menu name");
        if (!warnings_.empty()) {
            SeparatorText("Warnings from the last reload");
            for (auto& w : warnings_) TextColored(ImVec4(1, 0.8f, 0.4f, 1), "%s", w.c_str());
        }
        End();
        return;
    }
    const std::string type = JS(*n, "type");
    const TypeDef* t = FindType(type);
    PushFont(nullptr, GetStyle().FontSizeBase * 1.15f);
    TextUnformatted(IL(t ? t->icon.c_str() : "question", t ? t->name : type).c_str());
    PopFont();

    if (id_edit_for_ != selected_) { id_edit_ = selected_; id_edit_for_ = selected_; }
    AlignTextToFramePadding();
    TextDisabled("id");
    SameLine();
    SetNextItemWidth(-FLT_MIN);
    if (InputText("##id", &id_edit_, ImGuiInputTextFlags_EnterReturnsTrue) || (IsItemDeactivated() && id_edit_ != selected_)) {
        std::string nid = Slug(id_edit_);
        if (nid != selected_) {
            if (FindNode(doc_, nid)) { status_ = "id '" + nid + "' already exists"; status_time_ = Now(); }
            else { (*n)["id"] = nid; selected_ = nid; id_edit_for_.clear(); MarkDirty(); }
        }
        id_edit_ = selected_;
    }
    Spacing();

    if (t && BeginTable("props", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerH)) {
        TableSetupColumn("k", ImGuiTableColumnFlags_WidthFixed, CalcTextSize("sidebar_width").x + 8);
        TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch);
        for (auto& p : t->props) PropEditor(*n, p);
        EndTable();
    } else if (!t) {
        TextColored(ImVec4(1, 0.5f, 0.4f, 1), "Unknown type '%s'", type.c_str());
    }

    Spacing();
    if (CollapsingHeader("Raw JSON")) {
        n = Selected();
        if (!n) { End(); return; }
        std::string sig = selected_ + "|" + committed_;
        if (raw_json_for_ != sig && !IsAnyItemActive()) { json c = *n; c.erase("children"); raw_json_ = c.dump(2); raw_json_for_ = sig; raw_json_err_.clear(); }
        TextDisabled("Every property, including ones not listed above (children hidden)");
        InputTextMultiline("##raw", &raw_json_, ImVec2(-FLT_MIN, GetTextLineHeight() * 12));
        if (Button("Apply JSON")) {
            json j = json::parse(raw_json_, nullptr, false);
            if (!j.is_object()) raw_json_err_ = "Not a JSON object";
            else {
                if (n->contains("children")) j["children"] = (*n)["children"];
                std::string nid = JS(j, "id", selected_);
                if (nid != selected_ && FindNode(doc_, nid)) raw_json_err_ = "id already exists";
                else { *n = j; selected_ = nid; raw_json_for_.clear(); MarkDirty(); }
            }
        }
        if (!raw_json_err_.empty()) { SameLine(); TextColored(ImVec4(1, 0.5f, 0.4f, 1), "%s", raw_json_err_.c_str()); }
    }
    End();
}

// ============================================================ theme

void Editor::ThemePanel()
{
    using namespace ImGui;
    if (!Begin("Theme")) { End(); return; }
    std::string cur = JS(doc_, "theme");
    SetNextItemWidth(-FLT_MIN);
    if (BeginCombo("##active", IL("palette", cur).c_str())) {
        for (auto& [name, t] : doc_["themes"].items()) {
            (void)t;
            if (Selectable(name.c_str(), name == cur)) { doc_["theme"] = name; MarkDirty(); }
        }
        EndCombo();
    }
    auto unique_name = [&](std::string base) {
        std::string n = base;
        for (int i = 2; doc_["themes"].contains(n); i++) n = base + " " + std::to_string(i);
        return n;
    };
    if (Button(IL("plus", "From preset").c_str())) OpenPopup("presets");
    if (BeginPopup("presets")) {
        for (auto& p : ThemePresetNames())
            if (Selectable(p.c_str())) {
                std::string n = unique_name(p);
                doc_["themes"][n] = ThemePreset(p);
                doc_["theme"] = n;
                MarkDirty();
            }
        EndPopup();
    }
    SameLine();
    if (Button(IL("clone", "Duplicate").c_str())) {
        std::string n = unique_name(cur + " copy");
        doc_["themes"][n] = doc_["themes"][cur];
        doc_["theme"] = n;
        MarkDirty();
    }
    SameLine();
    if (Button(IL("pen", "Rename").c_str())) { new_theme_name_ = cur; OpenPopup("rename_theme"); }
    if (BeginPopup("rename_theme")) {
        if (IsWindowAppearing()) SetKeyboardFocusHere();
        if (InputText("##n", &new_theme_name_, ImGuiInputTextFlags_EnterReturnsTrue) && !new_theme_name_.empty() &&
            !doc_["themes"].contains(new_theme_name_)) {
            json themes = json::object();
            for (auto& [k, v] : doc_["themes"].items()) themes[k == cur ? new_theme_name_ : k] = v;
            doc_["themes"] = themes;
            doc_["theme"] = new_theme_name_;
            MarkDirty();
            CloseCurrentPopup();
        }
        EndPopup();
    }
    SameLine();
    BeginDisabled(doc_["themes"].size() <= 1);
    if (Button(IconUtf8("trash"))) {
        doc_["themes"].erase(cur);
        doc_["theme"] = doc_["themes"].begin().key();
        MarkDirty();
    }
    SetItemTooltip("Delete this theme");
    EndDisabled();

    json& th = ActiveTheme(doc_);
    ImGuiStyle st;
    mm::Theme mt;
    ThemeFromJson(th, st, mt);

    if (CollapsingHeader("Quick recolor", ImGuiTreeNodeFlags_DefaultOpen)) {
        static ImVec4 base(0.085f, 0.085f, 0.105f, 1);
        static bool light = false;
        ImVec4 acc = mt.accent;
        TextDisabled("Rebuilds every color from an accent + background");
        bool ch = ColorEdit3("Accent", &acc.x, ImGuiColorEditFlags_NoInputs);
        SameLine();
        ch |= ColorEdit3("Background", &base.x, ImGuiColorEditFlags_NoInputs);
        SameLine();
        ch |= Checkbox("Light", &light);
        if (ch) {
            json fresh = ThemeFromAccent(acc, base, light);
            th["colors"] = fresh["colors"];
            for (auto* k : {"accent", "sidebar_bg", "toggle_off", "text_dim", "group_title"}) th["mm"][k] = fresh["mm"][k];
            MarkDirty();
        }
    }
    if (CollapsingHeader("Menu widgets & animation", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (auto& f : MmFields()) {
            char* p = (char*)&mt + f.offset;
            bool ch = false;
            if (f.kind == 0) ch = ColorEdit4(f.name, (float*)p, ImGuiColorEditFlags_AlphaBar);
            else if (f.kind == 1) ch = SliderFloat(f.name, (float*)p, f.vmin, f.vmax, "%.1f");
            else ch = Checkbox(f.name, (bool*)p);
            if (ch) {
                if (f.kind == 0) { ImVec4 c = *(ImVec4*)p; th["mm"][f.name] = json::array({c.x, c.y, c.z, c.w}); }
                else if (f.kind == 1) th["mm"][f.name] = std::round(*(float*)p * 100) / 100;
                else th["mm"][f.name] = *(bool*)p;
                MarkDirty();
            }
        }
    }
    if (CollapsingHeader("Sizes & rounding")) {
        for (auto& v : StyleVars()) {
            float* f = (float*)((char*)&st + v.offset);
            bool ch = v.count == 1 ? SliderFloat(v.name, f, v.vmin, v.vmax, "%.1f") : SliderFloat2(v.name, f, v.vmin, v.vmax, "%.1f");
            if (ch) {
                if (v.count == 1) th["vars"][v.name] = std::round(f[0] * 10) / 10;
                else th["vars"][v.name] = json::array({std::round(f[0] * 10) / 10, std::round(f[1] * 10) / 10});
                MarkDirty();
            }
        }
    }
    if (CollapsingHeader("All colors")) {
        SetNextItemWidth(-FLT_MIN);
        InputTextWithHint("##cf", "Filter (e.g. Button, Frame)", &color_filter_);
        for (int i = 0; i < ImGuiCol_COUNT; i++) {
            const char* name = GetStyleColorName(i);
            if (!color_filter_.empty()) {
                std::string a = name, b = color_filter_;
                std::transform(a.begin(), a.end(), a.begin(), ::tolower);
                std::transform(b.begin(), b.end(), b.begin(), ::tolower);
                if (a.find(b) == std::string::npos) continue;
            }
            ImVec4 c = st.Colors[i];
            if (ColorEdit4(name, &c.x, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf)) {
                th["colors"][name] = json::array({std::round(c.x * 1000) / 1000, std::round(c.y * 1000) / 1000, std::round(c.z * 1000) / 1000, std::round(c.w * 1000) / 1000});
                MarkDirty();
            }
        }
    }
    End();
}

// ============================================================ assets

static void RenameRefs(json& arr, const char* key, const std::string& from, const std::string& to)
{
    if (!arr.is_array()) return;
    for (auto& n : arr) {
        if (!n.is_object()) continue;
        if (JS(n, key) == from) n[key] = to;
        if (n.contains("children")) RenameRefs(n["children"], key, from, to);
    }
}

void Editor::AssetsPanel()
{
    using namespace ImGui;
    if (!Begin("Assets")) { End(); return; }
    if (BeginTabBar("assets_tabs")) {
        auto import_file = [&](const char* sub, std::string& out_rel) -> bool {
            std::error_code ec;
            fs::path src(import_path_);
            if (!fs::is_regular_file(src, ec)) { status_ = "File not found: " + import_path_; status_time_ = Now(); return false; }
            fs::path dst = fs::path(project_dir_) / "assets" / sub / src.filename();
            fs::create_directories(dst.parent_path(), ec);
            fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
            if (ec) { status_ = ec.message(); status_time_ = Now(); return false; }
            out_rel = (fs::path("assets") / sub / src.filename()).string();
            import_path_.clear();
            return true;
        };

        if (BeginTabItem(IL("font", "Fonts").c_str())) {
            auto files = assets_.ListFiles("assets/fonts", {".ttf", ".otf"});
            int remove = -1;
            json& fonts = doc_["fonts"];
            for (int i = 0; i < (int)fonts.size(); i++) {
                json& f = fonts[i];
                PushID(i);
                std::string id = JS(f, "id");
                SetNextItemWidth(GetContentRegionAvail().x * 0.3f);
                if (InputText("##id", &id, ImGuiInputTextFlags_EnterReturnsTrue) && !id.empty()) {
                    std::string old = JS(f, "id");
                    f["id"] = Slug(id);
                    RenameRefs(doc_["windows"], "font", old, Slug(id));
                    RenameRefs(doc_["windows"], "header_font", old, Slug(id));
                    MarkDirty();
                }
                SetItemTooltip(i == 0 ? "Font id (first font = default)" : "Font id");
                SameLine();
                std::string path = JS(f, "path");
                SetNextItemWidth(GetContentRegionAvail().x - 110);
                if (BeginCombo("##path", fs::path(path).filename().string().c_str())) {
                    for (auto& file : files)
                        if (Selectable(fs::path(file).filename().string().c_str(), file == path)) { f["path"] = file; MarkDirty(); }
                    EndCombo();
                }
                SetItemTooltip("%s", path.c_str());
                SameLine();
                float size = JF(f, "size", 16);
                SetNextItemWidth(60);
                if (DragFloat("##size", &size, 0.2f, 6, 96, "%.0f")) { f["size"] = (int)size; MarkDirty(); }
                SameLine();
                if (Button(IconUtf8("trash"))) remove = i;
                PopID();
            }
            if (remove >= 0) { fonts.erase(fonts.begin() + remove); MarkDirty(); }
            if (Button(IL("plus", "Add font").c_str())) {
                json f = {{"id", Slug("font_" + std::to_string(fonts.size() + 1))}, {"path", files.empty() ? "" : files[0]}, {"size", 16}};
                fonts.push_back(f);
                MarkDirty();
            }
            Separator();
            SetNextItemWidth(-90);
            InputTextWithHint("##imp", "/path/to/font.ttf", &import_path_);
            SameLine();
            if (Button("Import")) {
                std::string rel;
                if (import_file("fonts", rel)) { fonts.push_back({{"id", Slug(fs::path(rel).stem().string())}, {"path", rel}, {"size", 16}}); MarkDirty(); }
            }
            if (!assets_.font_error.empty()) TextColored(ImVec4(1, 0.6f, 0.4f, 1), "%s", assets_.font_error.c_str());
            EndTabItem();
        }

        if (BeginTabItem(IL("image", "Images").c_str())) {
            auto files = assets_.ListFiles("assets/images", {".png", ".jpg", ".jpeg", ".bmp", ".tga", ".gif"});
            json& images = doc_["images"];
            int remove = -1;
            for (int i = 0; i < (int)images.size(); i++) {
                json& im = images[i];
                PushID(i);
                std::string id = JS(im, "id");
                const Texture* tex = assets_.Image(id);
                const float th = GetFrameHeight() * 2;
                if (tex && tex->gl && tex->error.empty()) {
                    float s = th / ImMax(tex->w, tex->h);
                    Image((ImTextureID)(intptr_t)tex->gl, ImVec2(tex->w * s, tex->h * s));
                } else Dummy(ImVec2(th, th));
                SameLine();
                BeginGroup();
                SetNextItemWidth(GetContentRegionAvail().x - 40);
                if (InputText("##id", &id, ImGuiInputTextFlags_EnterReturnsTrue) && !id.empty()) {
                    std::string old = JS(im, "id");
                    im["id"] = Slug(id);
                    RenameRefs(doc_["windows"], "image", old, Slug(id));
                    RenameRefs(doc_["windows"], "logo", old, Slug(id));
                    MarkDirty();
                }
                SameLine();
                if (Button(IconUtf8("trash"))) remove = i;
                std::string path = JS(im, "path");
                SetNextItemWidth(-FLT_MIN);
                if (BeginCombo("##path", path.c_str())) {
                    for (auto& file : files)
                        if (Selectable(file.c_str(), file == path)) { im["path"] = file; MarkDirty(); }
                    EndCombo();
                }
                if (tex && !tex->error.empty()) TextColored(ImVec4(1, 0.6f, 0.4f, 1), "%s", tex->error.c_str());
                EndGroup();
                PopID();
            }
            if (remove >= 0) { images.erase(images.begin() + remove); MarkDirty(); }
            if (images.empty()) TextDisabled("Put images in assets/images, or import one below.");
            BeginDisabled(files.empty());
            if (Button(IL("plus", "Add image").c_str())) {
                images.push_back({{"id", Slug(fs::path(files[0]).stem().string())}, {"path", files[0]}});
                MarkDirty();
            }
            EndDisabled();
            Separator();
            SetNextItemWidth(-90);
            InputTextWithHint("##imp", "/path/to/logo.png", &import_path_);
            SameLine();
            if (Button("Import")) {
                std::string rel;
                if (import_file("images", rel)) {
                    std::string id = Slug(fs::path(rel).stem().string());
                    for (int k = 2; std::any_of(images.begin(), images.end(), [&](const json& e) { return JS(e, "id") == id; }); k++)
                        id = Slug(fs::path(rel).stem().string()) + "_" + std::to_string(k);
                    images.push_back({{"id", id}, {"path", rel}});
                    MarkDirty();
                }
            }
            EndTabItem();
        }

        if (BeginTabItem(IL("icons", "Icons").c_str())) {
            json* sel = Selected();
            const TypeDef* t = sel ? FindType(JS(*sel, "type")) : nullptr;
            const bool can_set = t && FindProp(*t, "icon");
            TextDisabled(can_set ? "Click to set the selected widget's icon" : "Click to copy an icon name");
            SetNextItemWidth(-FLT_MIN);
            InputTextWithHint("##filter", "Search icons", &icon_filter_);
            BeginChild("icons");
            const float cell = GetFrameHeight() * 1.6f;
            int cols = ImMax(1, (int)(GetContentRegionAvail().x / (cell + GetStyle().ItemSpacing.x)));
            int i = 0;
            for (auto& e : AllIcons()) {
                if (strlen(e.name) <= 1) continue;
                if (!icon_filter_.empty() && !strstr(e.name, icon_filter_.c_str())) continue;
                if (i % cols != 0) SameLine();
                PushID(e.name);
                if (Button(Utf8(e.codepoint).c_str(), ImVec2(cell, cell))) {
                    if (can_set) { (*sel)["icon"] = e.name; MarkDirty(); }
                    else { SetClipboardText(e.name); status_ = std::string("Copied icon name: ") + e.name; status_time_ = Now(); }
                }
                SetItemTooltip("%s", e.name);
                PopID();
                i++;
            }
            EndChild();
            EndTabItem();
        }
        EndTabBar();
    }
    End();
}

// ============================================================ claude

void Editor::SendPrompt(const std::string& text)
{
    if (text.empty() || bridge_.Busy()) return;
    if (dirty_) { dirty_ = false; std::string d = doc_.dump(); if (d != committed_) { undo_.push_back(committed_); committed_ = d; } Save(); }
    BridgeRequest r;
    r.prompt = text;
    r.project_dir = project_dir_;
    r.selected_id = selected_;
    if (json* n = Selected()) r.selected_type = JS(*n, "type");
    r.model = settings_.model;
    r.claude_path = settings_.claude_path;
    r.mode = settings_.mode;
    r.tool_exe = exe_path_;
    for (auto& a : attachments_) r.images.push_back(fs::relative(a, project_dir_).string());
    attachments_.clear();
    json_error_offered_ = false;
    bridge_.Send(r);
    scroll_chat_ = true;
}

std::string Editor::NewAttachmentPath(const std::string& ext)
{
    fs::path dir = fs::path(project_dir_) / ".bridge" / "attachments";
    std::error_code ec;
    fs::create_directories(dir, ec);
    char stamp[32];
    time_t t = time(nullptr);
    strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", localtime(&t));
    return (dir / ("ref_" + std::string(stamp) + "_" + std::to_string(++attach_counter_) + ext)).string();
}

bool Editor::AttachImage(const std::string& src)
{
    std::error_code ec;
    if (!fs::is_regular_file(src, ec)) { status_ = "Not a file: " + src; status_time_ = Now(); return false; }
    if (!IsImageFile(src)) { status_ = "Not an image: " + src; status_time_ = Now(); return false; }
    // Keep PNG/JPEG as-is; convert anything else to PNG so every tool can read it.
    std::string dst;
    if (HasExt(src, {".png", ".jpg", ".jpeg"})) {
        dst = NewAttachmentPath(fs::path(src).extension().string());
        fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
        if (ec) { status_ = "Couldn't copy " + src + ": " + ec.message(); status_time_ = Now(); return false; }
    } else {
        dst = NewAttachmentPath(".png");
        int st = 0;
        RunCapture("magick " + ShellQuote(src) + " " + ShellQuote(dst) + " 2>/dev/null", &st);
        if (st != 0 || !fs::exists(dst)) {
            dst = NewAttachmentPath(fs::path(src).extension().string());
            fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
        }
    }
    attachments_.push_back(dst);
    focus_claude_ = true;
    status_ = "Attached " + fs::path(src).filename().string();
    status_time_ = Now();
    return true;
}

void Editor::PasteImage()
{
    const bool wayland = getenv("WAYLAND_DISPLAY") != nullptr;
    std::string types = wayland ? RunCapture("wl-paste --list-types 2>/dev/null")
                                : RunCapture("xclip -selection clipboard -t TARGETS -o 2>/dev/null");
    std::string chosen;
    bool uri_list = false;
    size_t start = 0;
    while (start < types.size()) {
        size_t e = types.find('\n', start);
        if (e == std::string::npos) e = types.size();
        std::string t = types.substr(start, e - start);
        start = e + 1;
        if (t == "image/png") { chosen = t; break; }
        if (chosen.empty() && t.rfind("image/", 0) == 0) chosen = t;
        if (t == "text/uri-list") uri_list = true;
    }
    for (char c : chosen)
        if (!(isalnum((unsigned char)c) || c == '/' || c == '+' || c == '-' || c == '.')) { chosen.clear(); break; }
    if (!chosen.empty()) {
        std::string data = wayland ? RunCapture("wl-paste --no-newline --type " + chosen + " 2>/dev/null")
                                   : RunCapture("xclip -selection clipboard -t " + chosen + " -o 2>/dev/null");
        if (data.empty()) { status_ = "The clipboard image was empty"; status_time_ = Now(); return; }
        std::string ext = chosen == "image/png" ? ".png" : chosen == "image/jpeg" ? ".jpg" : "." + chosen.substr(6);
        std::string tmp = NewAttachmentPath(ext);
        { std::ofstream f(tmp, std::ios::binary); f << data; }
        if (ext == ".png" || ext == ".jpg") { attachments_.push_back(tmp); focus_claude_ = true; status_ = "Pasted image"; status_time_ = Now(); }
        else { AttachImage(tmp); std::error_code ec; fs::remove(tmp, ec); }
        return;
    }
    if (uri_list) { // files copied in a file manager
        std::string uris = wayland ? RunCapture("wl-paste --no-newline --type text/uri-list 2>/dev/null")
                                   : RunCapture("xclip -selection clipboard -t text/uri-list -o 2>/dev/null");
        size_t st = 0;
        while (st < uris.size()) {
            size_t e = uris.find_first_of("\r\n", st);
            if (e == std::string::npos) e = uris.size();
            std::string u = uris.substr(st, e - st);
            st = e + 1;
            if (u.rfind("file://", 0) != 0) continue;
            std::string path;
            for (size_t i = 7; i < u.size(); i++) {
                if (u[i] == '%' && i + 2 < u.size()) { path += (char)std::stoi(u.substr(i + 1, 2), nullptr, 16); i += 2; }
                else path += u[i];
            }
            if (IsImageFile(path)) AttachImage(path);
        }
    }
}

void Editor::StartPicker()
{
    if (async_busy_) return;
    if (async_.joinable()) async_.join();
    if (!HaveCommand("zenity")) { ImGui::OpenPopup("attach_path"); return; }
    async_busy_ = true;
    async_ = std::thread([this] {
        std::string out = RunCapture("zenity --file-selection --multiple --separator='\n' --title='Attach reference screenshots' "
                                     "--file-filter='Images | *.png *.jpg *.jpeg *.webp *.bmp *.gif' 2>/dev/null");
        std::lock_guard<std::mutex> lk(async_mu_);
        size_t st = 0;
        while (st < out.size()) {
            size_t e = out.find('\n', st);
            if (e == std::string::npos) e = out.size();
            std::string p = out.substr(st, e - st);
            st = e + 1;
            if (!p.empty()) async_files_.push_back({p, false});
        }
        async_busy_ = false;
    });
}

void Editor::StartCapture()
{
    if (async_busy_) return;
    if (async_.joinable()) async_.join();
    const bool wayland = getenv("WAYLAND_DISPLAY") != nullptr;
    std::string dst = NewAttachmentPath(".png");
    std::string cmd;
    if (wayland && HaveCommand("grim") && HaveCommand("slurp")) cmd = "region=$(slurp) && grim -g \"$region\" " + ShellQuote(dst);
    else if (HaveCommand("magick")) cmd = "magick import " + ShellQuote(dst);
    else if (HaveCommand("import")) cmd = "import " + ShellQuote(dst);
    else { status_ = "No screenshot tool found (install grim + slurp, or ImageMagick)"; status_time_ = Now(); return; }
    async_busy_ = true;
    status_ = "Drag a box around the area to capture (Esc cancels)";
    status_time_ = Now();
    async_ = std::thread([this, cmd, dst] {
        int st = 0;
        RunCapture(cmd + " 2>/dev/null", &st);
        std::error_code ec;
        std::lock_guard<std::mutex> lk(async_mu_);
        if (st == 0 && fs::exists(dst, ec) && fs::file_size(dst, ec) > 0) async_files_.push_back({dst, true});
        else { async_errors_.push_back("Capture cancelled"); fs::remove(dst, ec); }
        async_busy_ = false;
    });
}

void Editor::PollAsync()
{
    std::vector<std::pair<std::string, bool>> files;
    std::vector<std::string> errors;
    std::string picked;
    bool have_pick = false;
    {
        std::lock_guard<std::mutex> lk(async_mu_);
        files.swap(async_files_);
        errors.swap(async_errors_);
        if (dialog_ready_) { picked = dialog_result_; have_pick = true; dialog_ready_ = false; }
    }
    if (have_pick) {
        auto cb = std::move(dialog_cb_);
        dialog_cb_ = nullptr;
        if (cb && !picked.empty()) cb(picked);
    }
    for (auto& [p, in_place] : files) {
        if (in_place) { attachments_.push_back(p); focus_claude_ = true; }
        else AttachImage(p);
    }
    for (auto& e : errors) { status_ = e; status_time_ = Now(); }
}

const Texture* Editor::Thumb(const std::string& path)
{
    auto it = thumbs_.find(path);
    if (it != thumbs_.end()) return it->second.gl ? &it->second : nullptr;
    Texture t;
    t.path = path;
    LoadTexture(t);
    auto& ref = thumbs_[path] = t;
    return ref.gl ? &ref : nullptr;
}

bool Editor::ImportFont(const std::string& src)
{
    std::error_code ec;
    fs::path dst = fs::path(project_dir_) / "assets/fonts" / fs::path(src).filename();
    fs::create_directories(dst.parent_path(), ec);
    fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
    if (ec) { status_ = ec.message(); status_time_ = Now(); return false; }
    std::string rel = fs::relative(dst, project_dir_).string();
    std::string id = Slug(fs::path(src).stem().string());
    for (int k = 2; std::any_of(doc_["fonts"].begin(), doc_["fonts"].end(), [&](const json& f) { return JS(f, "id") == id; }); k++)
        id = Slug(fs::path(src).stem().string()) + "_" + std::to_string(k);
    doc_["fonts"].push_back({{"id", id}, {"path", rel}, {"size", 16}});
    MarkDirty();
    status_ = "Added font '" + id + "'";
    status_time_ = Now();
    return true;
}

void Editor::OnDrop(int count, const char** paths)
{
    for (int i = 0; i < count; i++) {
        std::string p = paths[i];
        if (IsImageFile(p)) AttachImage(p);
        else if (HasExt(p, {".ttf", ".otf"})) ImportFont(p);
        else { status_ = "Drop images (to attach for Claude) or .ttf/.otf fonts"; status_time_ = Now(); }
    }
}

static void ImageFit(ImTextureID tex, int w, int h, float max_w, float max_h)
{
    float s = std::min(max_w / std::max(1, w), max_h / std::max(1, h));
    ImGui::Image(tex, ImVec2(w * s, h * s));
}

void Editor::ClaudePanel()
{
    using namespace ImGui;
    if (focus_claude_) { SetNextWindowFocus(); focus_claude_ = false; }
    if (!Begin("Claude")) { End(); return; }
    // Ctrl+V anywhere in this panel pastes a clipboard image.
    if (IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_V)) PasteImage();
    // Settings row
    const char* modes[] = {"Claude Code CLI", "Inbox file"};
    int mode = settings_.mode == "inbox" ? 1 : 0;
    SetNextItemWidth(140);
    if (Combo("##mode", &mode, modes, 2)) { settings_.mode = mode ? "inbox" : "cli"; SaveSettings(); }
    SetItemTooltip("CLI: runs Claude Code headless in the project folder.\nInbox: writes prompts to .bridge/inbox.jsonl for a Claude session watching the folder.");
    if (mode == 0) {
        SameLine();
        static const char* models[] = {"claude-sonnet-5-5", "claude-opus-5-5", "claude-fable-5-1", "claude-haiku-4-5-20251001"};
        SetNextItemWidth(170);
        if (BeginCombo("##model", settings_.model.c_str())) {
            for (auto* m : models)
                if (Selectable(m, settings_.model == m)) { settings_.model = m; SaveSettings(); }
            EndCombo();
        }
    }
    SameLine();
    if (Button(IconUtf8("gear"))) OpenPopup("bridge_settings");
    SetItemTooltip("Bridge settings");
    SameLine();
    if (Button(IL("rotate-left", "New chat").c_str())) bridge_.NewConversation();
    if (BeginPopup("bridge_settings")) {
        TextUnformatted("Claude Code CLI path (empty = auto-detect)");
        SetNextItemWidth(420);
        if (InputText("##path", &settings_.claude_path)) SaveSettings();
        std::string found = Bridge::DetectClaude(settings_.claude_path);
        if (found.empty()) TextColored(ImVec4(1, 0.6f, 0.4f, 1), "Not found. Install Claude Code or enter its path.");
        else { TextDisabled("Using: %s", found.c_str()); if (SmallButton("Copy login command")) SetClipboardText(("\"" + found + "\"").c_str()); }
        TextWrapped("First time: run that command in a terminal and log in with /login. The editor reuses that login.");
        Separator();
        TextDisabled("Session: %s", bridge_.SessionId().empty() ? "(new)" : bridge_.SessionId().c_str());
        if (bridge_.last_cost > 0) TextDisabled("Last request: $%.4f", bridge_.last_cost);
        EndPopup();
    }

    const float input_h = GetTextLineHeight() * 4 + GetStyle().FramePadding.y * 2;
    const float thumb_h = GetFrameHeight() * 2.2f;
    const float footer = input_h + GetFrameHeightWithSpacing() * 2 + GetStyle().ItemSpacing.y * 2 +
                         (attachments_.empty() ? 0.0f : thumb_h + GetStyle().ItemSpacing.y);
    BeginChild("chat", ImVec2(0, -footer), ImGuiChildFlags_Borders);
    if (bridge_.messages.size() <= 1) {
        TextDisabled("Describe a change and Claude edits the menu live. Try:");
        static const char* ideas[] = {
            "Make the theme blood red with sharper corners",
            "Add a Players tab with a player list and teleport/spectate buttons",
            "Turn the sidebar into top tabs with icons",
            "Add a config tab with save/load slots and a theme selector",
            "Make every toggle in Visuals a checkbox instead",
        };
        for (auto* idea : ideas)
            if (Selectable(IL("wand-magic-sparkles", idea).c_str())) prompt_ = idea;
        Spacing();
        TextDisabled("%s Recreate a menu from a screenshot: paste it (Ctrl+V), drop the file here,", IconUtf8("image"));
        TextDisabled("   use Attach or Capture below, then press Send.");
    }
    const ImVec4 user_col(0.55f, 0.7f, 1.0f, 1), tool_col(0.6f, 0.62f, 0.68f, 1), err_col(1, 0.5f, 0.45f, 1);
    for (size_t i = 0; i < bridge_.messages.size(); i++) {
        const ChatMsg& m = bridge_.messages[i];
        PushID((int)i);
        switch (m.role) {
        case ChatMsg::User:
            Spacing();
            PushStyleColor(ImGuiCol_Text, user_col);
            TextWrapped("%s %s", IconUtf8("user"), m.text.c_str());
            PopStyleColor();
            for (size_t k = 0; k < m.images.size(); k++) {
                if (k) SameLine();
                if (const Texture* t = Thumb(m.images[k])) {
                    ImageFit((ImTextureID)(intptr_t)t->gl, t->w, t->h, thumb_h * 2.5f, thumb_h * 1.5f);
                    if (BeginItemTooltip()) { ImageFit((ImTextureID)(intptr_t)t->gl, t->w, t->h, 640, 480); EndTooltip(); }
                } else TextDisabled("[%s]", fs::path(m.images[k]).filename().string().c_str());
            }
            break;
        case ChatMsg::Assistant:
            TextWrapped("%s", m.text.c_str());
            break;
        case ChatMsg::Tool:
            PushStyleColor(ImGuiCol_Text, tool_col);
            TextWrapped("%s %s", IconUtf8("pen-to-square"), m.text.c_str());
            PopStyleColor();
            break;
        case ChatMsg::Info:
            PushStyleColor(ImGuiCol_Text, GetStyle().Colors[ImGuiCol_TextDisabled]);
            TextWrapped("%s", m.text.c_str());
            PopStyleColor();
            break;
        case ChatMsg::Error:
            PushStyleColor(ImGuiCol_Text, err_col);
            TextWrapped("%s %s", IconUtf8("triangle-exclamation"), m.text.c_str());
            PopStyleColor();
            break;
        }
        PopID();
    }
    if (bridge_.Busy()) {
        const char* spin = "|/-\\";
        TextDisabled("%c working", spin[(int)(GetTime() * 8) % 4]);
    }
    if (json_error_offered_ && !bridge_.Busy() && !load_error_.empty()) {
        if (Button(IL("wrench", "Ask Claude to fix menu.json").c_str())) {
            json_error_offered_ = false;
            SendPrompt("menu.json no longer parses: " + load_error_ + "\nPlease fix the JSON syntax without changing the design.");
        }
    }
    if (bridge_.messages.size() != chat_seen_ || scroll_chat_) { SetScrollHereY(1.0f); chat_seen_ = bridge_.messages.size(); scroll_chat_ = false; }
    EndChild();

    if (json* n = Selected()) TextDisabled("%s Context: %s (%s)", IconUtf8("crosshairs"), selected_.c_str(), JS(*n, "type").c_str());
    else TextDisabled("Context: nothing selected (whole menu)");

    // Attachments
    if (Button(IL("paperclip", "Attach").c_str())) StartPicker();
    SetItemTooltip("Attach screenshots (or drag & drop image files onto the window, or Ctrl+V a copied image)");
    SameLine();
    if (Button(IL("crop-simple", "Capture").c_str())) StartCapture();
    SetItemTooltip("Select a region of your screen to capture as a reference");
    SameLine();
    if (Button(IL("paste", "Paste").c_str())) PasteImage();
    SetItemTooltip("Paste an image from the clipboard (Ctrl+V)");
    if (async_busy_) { SameLine(); TextDisabled("waiting..."); }
    if (BeginPopup("attach_path")) {
        TextUnformatted("Image path:");
        SetNextItemWidth(380);
        if (IsWindowAppearing()) SetKeyboardFocusHere();
        if (InputText("##ap", &attach_path_input_, ImGuiInputTextFlags_EnterReturnsTrue)) {
            if (AttachImage(attach_path_input_)) { attach_path_input_.clear(); CloseCurrentPopup(); }
        }
        EndPopup();
    }
    if (!attachments_.empty()) {
        int remove = -1;
        for (size_t k = 0; k < attachments_.size(); k++) {
            if (k) SameLine();
            PushID((int)k);
            BeginGroup();
            if (const Texture* t = Thumb(attachments_[k])) {
                ImageFit((ImTextureID)(intptr_t)t->gl, t->w, t->h, thumb_h * 2.0f, thumb_h);
                if (BeginItemTooltip()) {
                    ImageFit((ImTextureID)(intptr_t)t->gl, t->w, t->h, 720, 540);
                    TextDisabled("%d x %d px", t->w, t->h);
                    EndTooltip();
                }
            } else Button(IconUtf8("image"), ImVec2(thumb_h, thumb_h));
            const ImVec2 br = GetItemRectMax();
            const ImVec2 tl = GetItemRectMin();
            SetCursorScreenPos(ImVec2(br.x - GetFrameHeight() * 0.8f, tl.y));
            if (SmallButton(IconUtf8("xmark"))) remove = (int)k;
            EndGroup();
            PopID();
        }
        if (remove >= 0) attachments_.erase(attachments_.begin() + remove);
    }

    const bool busy = bridge_.Busy();
    BeginDisabled(busy);
    bool send = InputTextMultiline("##prompt", &prompt_, ImVec2(GetContentRegionAvail().x - 70, input_h),
                                   ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CtrlEnterForNewLine);
    EndDisabled();
    if (IsItemHovered() && prompt_.empty())
        SetTooltip(attachments_.empty() ? "Enter to send, Ctrl+Enter for a new line"
                                        : "Enter to send. Empty prompt = recreate the screenshot as accurately as possible");
    SameLine();
    if (busy) {
        if (Button(IL("stop", "Stop").c_str(), ImVec2(-FLT_MIN, input_h))) bridge_.Stop();
    } else if (Button(IL("paper-plane", "Send").c_str(), ImVec2(-FLT_MIN, input_h)) || send) {
        std::string p = prompt_;
        while (!p.empty() && (p.back() == '\n' || p.back() == ' ')) p.pop_back();
        if (p.empty() && !attachments_.empty()) p = "Recreate the menu in this screenshot as accurately as possible.";
        if (!p.empty()) { SendPrompt(p); prompt_.clear(); }
    }
    End();
}

// ============================================================ code / export / project

void Editor::CodePanel()
{
    using namespace ImGui;
    if (!Begin("Code")) { End(); return; }
    if (code_sig_ != committed_) { code_cache_ = GenerateMenuHeader(doc_); code_sig_ = committed_; }
    if (Button(IL("copy", "Copy").c_str())) SetClipboardText(code_cache_.c_str());
    SameLine();
    if (Button(IL("file-export", "Export project...").c_str())) open_export_ = true;
    SameLine();
    TextDisabled("menu_generated.h — live preview of the exported code");
    InputTextMultiline("##code", &code_cache_, ImVec2(-FLT_MIN, -FLT_MIN), ImGuiInputTextFlags_ReadOnly);
    End();
}

bool Editor::ExportTo(const std::string& dir, std::string& msg)
{
    ExportResult r = ExportProject(doc_, project_dir_, data_dir_, dir.empty() ? export_dir_ : dir);
    msg = r.message;
    return r.ok;
}

bool Editor::HaveDialogs()
{
    static int have = -1;
    if (have < 0) have = HaveCommand("zenity") || HaveCommand("kdialog");
    return have == 1;
}

// kind: 0 open file, 1 save file, 2 pick folder
std::string Editor::DialogCmd(int kind, const std::string& title, const std::string& start, const std::string& filter)
{
    if (HaveCommand("zenity")) {
        std::string c = "zenity --file-selection --title=" + ShellQuote(title) + " --filename=" + ShellQuote(start);
        if (kind == 1) c += " --save";
        if (kind == 2) c += " --directory";
        if (!filter.empty()) c += " --file-filter=" + ShellQuote(filter) + " --file-filter='All files | *'";
        return c + " 2>/dev/null";
    }
    std::string c = kind == 0 ? "kdialog --getopenfilename " : kind == 1 ? "kdialog --getsavefilename " : "kdialog --getexistingdirectory ";
    c += ShellQuote(start);
    if (kind == 0 && !filter.empty()) {
        // "Name | pattern" -> "pattern|Name"
        size_t bar = filter.find('|');
        std::string name = filter.substr(0, bar), pat = bar == std::string::npos ? "*" : filter.substr(bar + 1);
        while (!pat.empty() && pat[0] == ' ') pat.erase(0, 1);
        while (!name.empty() && name.back() == ' ') name.pop_back();
        c += " " + ShellQuote(pat + "|" + name);
    }
    return c + " --title " + ShellQuote(title) + " 2>/dev/null";
}

bool Editor::RunDialog(const std::string& cmd, std::function<void(const std::string&)> on_pick)
{
    if (async_busy_) { status_ = "A file window is already open"; status_time_ = Now(); return false; }
    if (async_.joinable()) async_.join();
    async_busy_ = true;
    dialog_cb_ = std::move(on_pick);
    async_ = std::thread([this, cmd] {
        int st = 0;
        std::string out = RunCapture(cmd, &st);
        while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
        std::lock_guard<std::mutex> lk(async_mu_);
        dialog_result_ = st == 0 ? out : "";
        dialog_ready_ = true;
        async_busy_ = false;
    });
    return true;
}

static std::string MenusDir(const std::string& project_dir)
{
    fs::path p = fs::path(project_dir).parent_path();
    std::error_code ec;
    return (fs::is_directory(p, ec) ? p : fs::path(getenv("HOME") ? getenv("HOME") : ".")).string() + "/";
}

void Editor::FileOpen()
{
    if (!HaveDialogs()) { open_project_ = true; project_mode_ = 0; project_path_input_ = project_dir_; return; }
    RunDialog(DialogCmd(0, "Open menu project (pick its menu.json)", MenusDir(project_dir_), "Menu project | menu.json"),
              [this](const std::string& path) {
                  fs::path p(path);
                  std::error_code ec;
                  fs::path dir = fs::is_directory(p, ec) ? p : p.parent_path();
                  if (!fs::exists(dir / "menu.json", ec)) { status_ = "That folder has no menu.json"; status_time_ = Now(); return; }
                  if (dirty_) { dirty_ = false; Save(); }
                  OpenProject(dir.string(), false);
              });
}

void Editor::FileNew()
{
    if (!HaveDialogs()) {
        open_project_ = true; project_mode_ = 1;
        project_path_input_ = (fs::path(project_dir_).parent_path() / "NewMenu").string();
        return;
    }
    RunDialog(DialogCmd(1, "New menu project: choose a name and location", MenusDir(project_dir_) + "NewMenu"),
              [this](const std::string& path) {
                  std::error_code ec;
                  if (fs::exists(path, ec) && !fs::exists(fs::path(path) / "menu.json", ec) && !fs::is_empty(path, ec)) {
                      status_ = "That folder isn't empty; pick a new name"; status_time_ = Now(); return;
                  }
                  if (dirty_) { dirty_ = false; Save(); }
                  OpenProject(path, true);
              });
}

void Editor::FileSaveAs()
{
    if (!HaveDialogs()) {
        open_project_ = true; project_mode_ = 2;
        project_path_input_ = project_dir_ + "-copy";
        return;
    }
    RunDialog(DialogCmd(1, "Save menu project as: choose a name and location", project_dir_ + "-copy"),
              [this](const std::string& path) { SaveProjectAs(path); });
}

bool Editor::SaveProjectAs(const std::string& dir_in)
{
    std::error_code ec;
    fs::path dir = fs::absolute(ExpandHome(dir_in), ec);
    if (dir.empty()) return false;
    if (fs::equivalent(dir, project_dir_, ec)) { dirty_ = false; Save(); return true; }
    if (fs::exists(dir, ec) && !fs::is_directory(dir, ec)) { status_ = "A file with that name already exists"; status_time_ = Now(); return false; }
    if (fs::exists(dir, ec) && !fs::is_empty(dir, ec) && !fs::exists(dir / "menu.json", ec)) {
        status_ = "That folder isn't empty; pick a new name"; status_time_ = Now(); return false;
    }
    fs::create_directories(dir, ec);
    if (ec) { status_ = "Couldn't create " + dir.string() + ": " + ec.message(); status_time_ = Now(); return false; }
    fs::path assets = fs::path(project_dir_) / "assets";
    if (fs::is_directory(assets, ec))
        fs::copy(assets, dir / "assets", fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
    if (ec) { status_ = "Couldn't copy assets: " + ec.message(); status_time_ = Now(); return false; }
    if (!WriteFileAtomic((dir / "menu.json").string(), doc_.dump(2))) { status_ = "Couldn't write menu.json"; status_time_ = Now(); return false; }
    dirty_ = false;
    Save(); // keep the original up to date too
    if (!OpenProject(dir.string(), false)) return false;
    status_ = "Saved as " + dir.string();
    status_time_ = Now();
    return true;
}

void Editor::ExportPopup()
{
    using namespace ImGui;
    if (open_export_) { OpenPopup("Export C++"); open_export_ = false; export_msg_.clear(); }
    SetNextWindowSize(ImVec2(560, 0) * content_scale * settings_.ui_scale, ImGuiCond_Appearing);
    if (BeginPopupModal("Export C++", nullptr)) {
        TextWrapped("Writes menu_generated.h, the mm_widgets runtime, fonts, images and an example GLFW app.");
        SetNextItemWidth(HaveDialogs() ? -110 : -FLT_MIN);
        InputText("##dir", &export_dir_);
        if (HaveDialogs()) {
            SameLine();
            BeginDisabled(async_busy_);
            if (Button(IL("folder-open", "Browse...").c_str(), ImVec2(-FLT_MIN, 0)))
                RunDialog(DialogCmd(2, "Export C++ to folder", ExpandHome(export_dir_) + "/"), [this](const std::string& p) { export_dir_ = p; });
            EndDisabled();
        }
        if (Button(IL("file-export", "Export").c_str(), ImVec2(120, 0))) {
            if (dirty_) { dirty_ = false; Save(); }
            ExportResult r = ExportProject(doc_, project_dir_, data_dir_, ExpandHome(export_dir_));
            export_msg_ = r.message;
            if (r.ok) for (auto& f : r.files) export_msg_ += "\n  " + f;
        }
        SameLine();
        if (Button("Close", ImVec2(120, 0))) CloseCurrentPopup();
        if (!export_msg_.empty()) { Separator(); TextWrapped("%s", export_msg_.c_str()); }
        EndPopup();
    }
}

void Editor::ProjectPopup()
{
    using namespace ImGui;
    if (open_project_) { OpenPopup("Project"); open_project_ = false; }
    SetNextWindowSize(ImVec2(560, 0) * content_scale * settings_.ui_scale, ImGuiCond_Appearing);
    if (BeginPopupModal("Project", nullptr)) {
        const char* prompts[] = {"Project folder (contains menu.json):", "Folder for the new project (created if needed):",
                                 "Save a copy of this project (menu + assets) to folder:"};
        TextWrapped("%s", prompts[project_mode_]);
        SetNextItemWidth(-FLT_MIN);
        InputText("##p", &project_path_input_);
        const std::string path = ExpandHome(project_path_input_);
        bool exists = FileExists((fs::path(path) / "menu.json").string());
        if (project_mode_ == 1 && exists) TextColored(ImVec4(1, 0.8f, 0.4f, 1), "That folder already has a menu.json; it will be opened.");
        if (project_mode_ == 0 && !exists) TextColored(ImVec4(1, 0.6f, 0.4f, 1), "No menu.json there.");
        if (project_mode_ == 2 && exists) TextColored(ImVec4(1, 0.8f, 0.4f, 1), "That folder already has a menu.json; it will be overwritten.");
        const char* verbs[] = {"Open", "Create", "Save"};
        if (Button(verbs[project_mode_], ImVec2(120, 0))) {
            if (dirty_) { dirty_ = false; Save(); }
            bool ok = project_mode_ == 2 ? SaveProjectAs(path) : OpenProject(path, project_mode_ == 1);
            if (ok) CloseCurrentPopup();
        }
        SameLine();
        if (Button("Cancel", ImVec2(120, 0))) CloseCurrentPopup();
        EndPopup();
    }
}
