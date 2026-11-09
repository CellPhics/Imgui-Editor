// Core of the editor: settings, opening/saving projects, undo/redo,
// selection and tree edits, the per-frame loop, the menu bar and the canvas.
#include "editor.h"
#include "mm_widgets.h"
#include "theme.h"
#include "util.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

std::string IconText(const char* icon, const std::string& text)
{
    const std::string glyph = IconUtf8(icon);
    return text.empty() ? glyph : glyph + "  " + text;
}

void ImageFit(ImTextureID tex, int w, int h, float max_w, float max_h)
{
    const float scale = std::min(max_w / std::max(1, w), max_h / std::max(1, h));
    ImGui::Image(tex, ImVec2(w * scale, h * scale));
}

// ---------------------------------------------------------------- setup

bool Editor::Init(const std::string& data_dir, const std::string& config_dir, const std::string& project_dir)
{
    data_dir_ = data_dir;
    config_dir_ = config_dir;
    std::error_code ec;
    exe_path_ = fs::read_symlink("/proc/self/exe", ec).string(); // Claude calls us back for `tool ...`
    fs::create_directories(config_dir_, ec);

    ini_path_ = (fs::path(config_dir_) / "imgui.ini").string();
    ImGui::GetIO().IniFilename = ini_path_.c_str();
    if (!FileExists(ini_path_)) reset_layout_ = true; // first run, build the default layout

    LoadSettings();
    assets_.data_dir = data_dir_;
    ApplyEditorStyle();

    // command line project > last project > ~/ImGuiMenus/MyMenu
    const std::string wanted = project_dir.empty() ? settings_.last_project : project_dir;
    if (!wanted.empty() && OpenProject(wanted, false)) return true;
    if (!project_dir.empty()) return OpenProject(project_dir, true);
    return OpenProject((fs::path(HomeDir()) / "ImGuiMenus" / "MyMenu").string(), true);
}

void Editor::LoadSettings()
{
    bool ok = false;
    const json j = json::parse(ReadFile((fs::path(config_dir_) / "settings.json").string(), &ok), nullptr, false);
    if (!ok || !j.is_object()) return;
    settings_.last_project = GetStr(j, "last_project");
    settings_.claude_path = GetStr(j, "claude_path");
    settings_.model = GetStr(j, "model", settings_.model);
    settings_.use_inbox = GetStr(j, "mode") == "inbox";
    settings_.ui_scale = GetFloat(j, "ui_scale", 1.0f);
}

void Editor::SaveSettings()
{
    const json j = {{"last_project", settings_.last_project},
                    {"claude_path", settings_.claude_path},
                    {"model", settings_.model},
                    {"mode", settings_.use_inbox ? "inbox" : "cli"},
                    {"ui_scale", settings_.ui_scale}};
    WriteFileAtomic((fs::path(config_dir_) / "settings.json").string(), j.dump(2));
}

// The editor's own look. Kept plain and neutral so the menu you're designing stands out.
void Editor::ApplyEditorStyle()
{
    ImGuiStyle& s = ImGui::GetStyle();
    s = ImGuiStyle();
    ImGui::StyleColorsDark(&s);
    s.WindowRounding = 6;
    s.FrameRounding = 4;
    s.ChildRounding = 4;
    s.PopupRounding = 4;
    s.GrabRounding = 4;
    s.TabRounding = 4;
    s.WindowPadding = ImVec2(10, 10);
    s.FramePadding = ImVec2(7, 4);
    s.ItemSpacing = ImVec2(8, 6);
    s.WindowBorderSize = 1;
    s.FrameBorderSize = 0;
    s.DockingSeparatorSize = 2;

    const ImVec4 bg(0.105f, 0.11f, 0.125f, 1), panel(0.13f, 0.135f, 0.155f, 1), frame(0.18f, 0.185f, 0.21f, 1);
    const ImVec4 hover(0.22f, 0.23f, 0.27f, 1), active(0.25f, 0.27f, 0.33f, 1), line(0.2f, 0.21f, 0.24f, 1);
    const ImVec4 accent(0.42f, 0.55f, 0.98f, 1);
    auto tint = [&](float a) { return ImVec4(accent.x, accent.y, accent.z, a); };

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg] = panel;
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = ImVec4(0.12f, 0.125f, 0.145f, 0.98f);
    c[ImGuiCol_Border] = line;
    c[ImGuiCol_Separator] = line;
    c[ImGuiCol_FrameBg] = frame;
    c[ImGuiCol_FrameBgHovered] = hover;
    c[ImGuiCol_FrameBgActive] = active;
    c[ImGuiCol_TitleBg] = bg;
    c[ImGuiCol_TitleBgActive] = bg;
    c[ImGuiCol_MenuBarBg] = bg;
    c[ImGuiCol_Button] = frame;
    c[ImGuiCol_ButtonHovered] = active;
    c[ImGuiCol_ButtonActive] = accent;
    c[ImGuiCol_Header] = tint(0.25f);
    c[ImGuiCol_HeaderHovered] = tint(0.35f);
    c[ImGuiCol_HeaderActive] = tint(0.5f);
    c[ImGuiCol_CheckMark] = accent;
    c[ImGuiCol_SliderGrab] = accent;
    c[ImGuiCol_SliderGrabActive] = accent;
    c[ImGuiCol_Tab] = bg;
    c[ImGuiCol_TabHovered] = hover;
    c[ImGuiCol_TabSelected] = panel;
    c[ImGuiCol_TabDimmed] = bg;
    c[ImGuiCol_TabDimmedSelected] = panel;
    c[ImGuiCol_TabSelectedOverline] = accent;
    c[ImGuiCol_DockingPreview] = tint(0.5f);
    c[ImGuiCol_DockingEmptyBg] = bg;
    c[ImGuiCol_TextSelectedBg] = tint(0.35f);

    const float scale = content_scale * settings_.ui_scale;
    s.ScaleAllSizes(scale);
    s.FontScaleDpi = scale;
}

// ---------------------------------------------------------------- projects

void Editor::CreateProjectFiles(const std::string& dir)
{
    std::error_code ec;
    fs::create_directories(fs::path(dir) / "assets/fonts", ec);
    fs::create_directories(fs::path(dir) / "assets/images", ec);
    for (const char* font : {"Roboto-Medium.ttf", "Karla-Regular.ttf", "Cousine-Regular.ttf"}) {
        const fs::path to = fs::path(dir) / "assets/fonts" / font;
        if (!fs::exists(to, ec)) fs::copy_file(fs::path(data_dir_) / "fonts" / font, to, ec);
    }
    const fs::path menu = fs::path(dir) / "menu.json";
    if (!fs::exists(menu, ec)) WriteFileAtomic(menu.string(), DefaultDoc().dump(2));
}

// Every project gets a CLAUDE.md explaining the menu format to Claude. The
// template has a {{TOOL}} placeholder for the path to this binary.
void Editor::WriteClaudeGuide(const std::string& dir)
{
    std::string guide = ReadFile((fs::path(data_dir_) / "project_template/CLAUDE.md").string());
    if (guide.empty()) return;
    const std::string tool = (exe_path_.find(' ') != std::string::npos ? "\"" + exe_path_ + "\"" : exe_path_) + " tool";
    for (size_t at; (at = guide.find("{{TOOL}}")) != std::string::npos;) guide.replace(at, 8, tool);
    const std::string path = (fs::path(dir) / "CLAUDE.md").string();
    if (ReadFile(path) != guide) WriteFileAtomic(path, guide);
}

bool Editor::OpenProject(const std::string& dir_in, bool create_if_missing)
{
    std::error_code ec;
    const fs::path dir = fs::absolute(ExpandHome(dir_in), ec);
    const fs::path menu = dir / "menu.json";
    if (!fs::exists(menu, ec)) {
        if (!create_if_missing) return false;
        CreateProjectFiles(dir.string());
    }
    WriteClaudeGuide(dir.string());

    project_dir_ = dir.string();
    menu_path_ = menu.string();
    assets_.project_dir = project_dir_;
    export_dir_ = (dir / "export").string();
    undo_.clear();
    redo_.clear();
    selected_.clear();
    preview_ = PreviewState();
    load_error_.clear();
    file_mtime_ = 0;
    committed_.clear();
    doc_ = json();

    ReloadIfChanged(true);
    if (!doc_.is_object()) {
        // the file is broken: show the starter menu but don't overwrite their file
        doc_ = DefaultDoc();
        Normalize(doc_);
        committed_ = doc_.dump();
    }

    settings_.last_project = project_dir_;
    SaveSettings();
    bridge_.Reset();
    bridge_.messages.push_back({ChatMessage::Info, "Project: " + dir.filename().string()});
    chat_messages_seen_ = 0;
    SetStatus("Opened " + project_dir_);
    glfwSetWindowTitle(glfwGetCurrentContext(), ("ImGui Menu Editor - " + GetStr(doc_, "name", "Menu")).c_str());
    return true;
}

void Editor::Save()
{
    if (menu_path_.empty()) return;
    if (WriteFileAtomic(menu_path_, doc_.dump(2))) file_mtime_ = FileMTime(menu_path_);
    else SetStatus("Couldn't save " + menu_path_);
}

void Editor::SetStatus(const std::string& text)
{
    status_ = text;
    status_time_ = Now();
}

// ---------------------------------------------------------------- undo / file sync
//
// Edits change doc_ directly and call MarkDirty(). Once you let go of the mouse
// the edit gets "committed": the previous state goes on the undo stack and the
// file gets saved. Undo states are just whole-document JSON dumps. Menus are
// small, so that's simpler than tracking individual changes.

void Editor::FlushEdits()
{
    dirty_ = false;
    const std::string now = doc_.dump();
    if (now != committed_) {
        undo_.push_back(committed_);
        if (undo_.size() > 300) undo_.erase(undo_.begin());
        redo_.clear();
        committed_ = now;
    }
    Save();
}

void Editor::CommitIfIdle()
{
    if (!dirty_ || ImGui::IsAnyItemActive() || ImGui::IsMouseDown(ImGuiMouseButton_Left)) return;
    FlushEdits();
}

void Editor::SetDoc(const json& doc)
{
    doc_ = doc;
    if (!selected_.empty() && !FindNode(doc_, selected_)) selected_.clear();
    preview_.selection_changed = true;
}

void Editor::Undo()
{
    if (dirty_) FlushEdits();
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

// Picks up changes made to menu.json by anything else (Claude, a text editor, git).
void Editor::ReloadIfChanged(bool force)
{
    if (menu_path_.empty()) return;
    if (!force && Now() - last_file_check_ < 0.25) return;
    last_file_check_ = Now();
    const long long mtime = FileMTime(menu_path_);
    if (mtime == 0 || (mtime == file_mtime_ && !force)) return;
    file_mtime_ = mtime;

    bool ok = false;
    const std::string text = ReadFile(menu_path_, &ok);
    if (!ok) return;
    json doc = json::parse(text, nullptr, false);
    if (doc.is_discarded()) {
        // parse again with exceptions on, just to get a useful error message
        try {
            doc = json::parse(text);
        } catch (const std::exception& e) {
            load_error_ = e.what();
        }
        if (load_error_.empty()) load_error_ = "menu.json isn't valid JSON";
        return;
    }
    load_error_.clear();
    load_warnings_ = Normalize(doc);
    const std::string dump = doc.dump();
    if (dump == committed_) return;

    if (!committed_.empty()) { // an outside change is undoable like any other edit
        if (dirty_) FlushEdits();
        undo_.push_back(committed_);
        redo_.clear();
    }
    committed_ = dump;
    dirty_ = false;
    SetDoc(doc);
    if (!load_warnings_.empty()) Save(); // write the fixed-up version back
    SetStatus("Reloaded menu.json");
}

// ---------------------------------------------------------------- selection and tree edits

void Editor::Select(const std::string& id)
{
    if (id == selected_) return;
    selected_ = id;
    preview_.selection_changed = true;
    reveal_in_tree_.clear();
    const auto path = PathTo(doc_, id);
    for (size_t i = 0; i + 1 < path.size(); i++) reveal_in_tree_.insert(path[i]);
    scroll_tree_to_selection_ = true;
}

json* Editor::Selected() { return FindNode(doc_, selected_).node; }

void Editor::AddNode(const std::string& type) { InsertNode(MakeNode(type, doc_)); }

// Puts a new node somewhere sensible: inside the selection if it fits there,
// otherwise right after the selection (or after its closest ancestor that can
// hold it), otherwise in the first place anywhere that takes it.
void Editor::InsertNode(json node)
{
    const std::string type = GetStr(node, "type");
    const std::string id = GetStr(node, "id");
    if (type == "window") {
        doc_["windows"].push_back(node);
        Select(id);
        MarkDirty();
        return;
    }

    json* target = nullptr;
    int index = -1; // -1 = append
    const NodeRef sel = FindNode(doc_, selected_);
    if (sel && CanContain(sel.node, type)) {
        target = sel.node;
    } else if (sel) {
        const auto path = PathTo(doc_, selected_);
        for (int i = (int)path.size() - 2; i >= 0 && !target; i--) {
            const NodeRef r = FindNode(doc_, path[i]);
            if (!r || !CanContain(r.node, type)) continue;
            target = r.node;
            const json& kids = (*target)["children"];
            for (int k = 0; k < (int)kids.size(); k++)
                if (GetStr(kids[k], "id") == path[i + 1]) index = k + 1;
        }
    }
    if (!target) {
        std::function<json*(json&)> first_fit = [&](json& nodes) -> json* {
            for (auto& n : nodes) {
                if (CanContain(&n, type)) return &n;
                if (n.contains("children"))
                    if (json* found = first_fit(n["children"])) return found;
            }
            return nullptr;
        };
        target = first_fit(doc_["windows"]);
    }
    if (!target && type != "tab") {
        doc_["windows"].push_back(MakeNode("window", doc_));
        target = &doc_["windows"].back();
    }
    if (!target) {
        SetStatus("Add a tab bar first, tabs go inside it");
        return;
    }

    json* kids = ChildArray(doc_, target);
    if (index >= 0 && index <= (int)kids->size()) kids->insert(kids->begin() + index, node);
    else kids->push_back(node);
    Select(id);
    MarkDirty();
}

void Editor::DeleteSelected()
{
    const NodeRef r = FindNode(doc_, selected_);
    if (!r) return;
    // select a neighbor so you can keep hitting Delete
    std::string next;
    if (r.index + 1 < (int)r.parent_array->size()) next = GetStr((*r.parent_array)[r.index + 1], "id");
    else if (r.index > 0) next = GetStr((*r.parent_array)[r.index - 1], "id");
    else if (r.parent) next = GetStr(*r.parent, "id");
    r.parent_array->erase(r.parent_array->begin() + r.index);
    selected_.clear();
    Select(next);
    MarkDirty();
}

void Editor::DuplicateSelected()
{
    const NodeRef r = FindNode(doc_, selected_);
    if (!r) return;
    const json copy = CloneWithNewIds(*r.node, doc_);
    r.parent_array->insert(r.parent_array->begin() + r.index + 1, copy);
    Select(GetStr(copy, "id"));
    MarkDirty();
}

void Editor::MoveSelected(int direction)
{
    const NodeRef r = FindNode(doc_, selected_);
    if (!r) return;
    const int other = r.index + direction;
    if (other < 0 || other >= (int)r.parent_array->size()) return;
    std::swap((*r.parent_array)[r.index], (*r.parent_array)[other]);
    MarkDirty();
}

// Drag & drop in the tree: put `id` inside `target` (into = true) or right after it.
void Editor::MoveNode(const std::string& id, const std::string& target, bool into)
{
    if (id == target || IsAncestor(doc_, id, target)) return; // can't drop something into itself
    NodeRef src = FindNode(doc_, id);
    NodeRef dst = FindNode(doc_, target);
    if (!src || !dst) return;
    const json node = *src.node;
    const std::string type = GetStr(node, "type");
    if (into && !CanContain(dst.node, type)) into = false;
    if (!into && !CanContain(dst.parent, type)) {
        SetStatus("Can't put a " + type + " there");
        return;
    }
    src.parent_array->erase(src.parent_array->begin() + src.index);
    dst = FindNode(doc_, target); // erasing can move things around, look it up again
    if (into) ChildArray(doc_, dst.node)->push_back(node);
    else dst.parent_array->insert(dst.parent_array->begin() + dst.index + 1, node);
    selected_.clear();
    Select(id);
    MarkDirty();
}

// Nodes go on the clipboard as JSON, so you can paste them into a text editor
// (or another project) too.
void Editor::CopySelected()
{
    if (json* n = Selected()) ImGui::SetClipboardText(n->dump(2).c_str());
}

void Editor::PasteNode()
{
    const char* clip = ImGui::GetClipboardText();
    const json n = json::parse(clip ? clip : "", nullptr, false);
    if (n.is_object() && FindType(GetStr(n, "type"))) InsertNode(CloneWithNewIds(n, doc_));
}

// ---------------------------------------------------------------- frame

void Editor::BeforeFrame()
{
    if (!doc_.is_object()) return;
    assets_.SyncFonts(doc_);
    if (Now() - last_image_check_ > 0.5) {
        assets_.SyncImages(doc_);
        last_image_check_ = Now();
    }
}

void Editor::Shutdown()
{
    if (job_thread_.joinable()) {
        // a file picker might still be open, don't hang on exit waiting for it
        if (job_running_) job_thread_.detach();
        else job_thread_.join();
    }
    if (dirty_) FlushEdits();
    bridge_.Stop();
    SaveSettings();
}

void Editor::Frame()
{
    ReloadIfChanged();
    PollBackgroundJob();
    bridge_.Poll();
    if (bridge_.TakeFinished()) {
        ReloadIfChanged(true);
        if (!load_error_.empty()) {
            bridge_.messages.push_back({ChatMessage::Error, "menu.json isn't valid JSON after that change:\n" + load_error_});
            offer_json_fix_ = true;
        }
    }

    HandleShortcuts();
    MenuBar();

    const ImGuiID dock = ImGui::GetID("MainDock");
    if (reset_layout_ || !ImGui::DockBuilderGetNode(dock)) {
        SetupDockLayout(dock);
        reset_layout_ = false;
    }
    dock_id_ = ImGui::DockSpaceOverViewport(dock, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);

    HierarchyPanel();
    InspectorPanel();
    ThemePanel();
    AssetsPanel();
    ClaudePanel();
    CodePanel();
    Canvas();
    ExportPopup();
    ProjectPopup();

    auto deferred = std::move(deferred_);
    deferred_.clear();
    for (auto& f : deferred) f();
    CommitIfIdle();
}

void Editor::HandleShortcuts()
{
    if (ImGui::GetIO().WantTextInput) return; // typing in a text box, leave the keys alone
    using namespace ImGui;
    if (IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z)) Undo();
    if (IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Y) || IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z)) Redo();
    if (IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S)) {
        FlushEdits();
        SetStatus("Saved");
    }
    if (IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S)) FileSaveAs();
    if (IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_O)) FileOpen();
    if (IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_N)) FileNew();
    if (IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_D)) DuplicateSelected();
    if (IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_C)) CopySelected();
    if (IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_V)) PasteNode();
    if (IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_E)) preview_.edit_mode = !preview_.edit_mode;
    if (IsKeyPressed(ImGuiKey_Delete, false)) DeleteSelected();
    if (IsKeyPressed(ImGuiKey_Escape, false)) selected_.clear();
}

// Default panel layout: tree + assets on the left, inspector/theme + Claude on
// the right, code at the bottom, the menu canvas in the middle.
void Editor::SetupDockLayout(unsigned dock_id)
{
    ImGui::DockBuilderRemoveNode(dock_id);
    ImGui::DockBuilderAddNode(dock_id, (ImGuiDockNodeFlags)ImGuiDockNodeFlags_DockSpace | ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::DockBuilderSetNodeSize(dock_id, ImGui::GetMainViewport()->WorkSize);
    ImGuiID center = dock_id;
    ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.19f, nullptr, &center);
    ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.30f, nullptr, &center);
    const ImGuiID left_bottom = ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.38f, nullptr, &left);
    const ImGuiID right_bottom = ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.55f, nullptr, &right);
    const ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.22f, nullptr, &center);
    ImGui::DockBuilderDockWindow("Hierarchy", left);
    ImGui::DockBuilderDockWindow("Assets", left_bottom);
    ImGui::DockBuilderDockWindow("Inspector", right);
    ImGui::DockBuilderDockWindow("Theme", right);
    ImGui::DockBuilderDockWindow("Claude", right_bottom);
    ImGui::DockBuilderDockWindow("Code", bottom);
    ImGui::DockBuilderFinish(dock_id);
}

// ---------------------------------------------------------------- menu bar

void Editor::MenuBar()
{
    using namespace ImGui;
    if (!BeginMainMenuBar()) return;

    if (BeginMenu("File")) {
        if (MenuItem(IconText("file-circle-plus", "New...").c_str(), "Ctrl+N")) FileNew();
        if (MenuItem(IconText("folder-open", "Open...").c_str(), "Ctrl+O")) FileOpen();
        if (MenuItem(IconText("floppy-disk", "Save").c_str(), "Ctrl+S")) {
            FlushEdits();
            SetStatus("Saved");
        }
        if (MenuItem(IconText("copy", "Save As...").c_str(), "Ctrl+Shift+S")) FileSaveAs();
        Separator();
        if (MenuItem(IconText("file-export", "Export C++...").c_str())) open_export_popup_ = true;
        if (MenuItem(IconText("link", "Copy project path").c_str())) SetClipboardText(project_dir_.c_str());
        Separator();
        if (MenuItem("Quit")) quit = true;
        EndMenu();
    }
    if (BeginMenu("Edit")) {
        if (MenuItem("Undo", "Ctrl+Z", false, !undo_.empty() || dirty_)) Undo();
        if (MenuItem("Redo", "Ctrl+Y", false, !redo_.empty())) Redo();
        Separator();
        if (MenuItem("Duplicate", "Ctrl+D", false, !selected_.empty())) DuplicateSelected();
        if (MenuItem("Delete", "Del", false, !selected_.empty())) DeleteSelected();
        if (MenuItem("Copy", "Ctrl+C", false, !selected_.empty())) CopySelected();
        if (MenuItem("Paste", "Ctrl+V")) PasteNode();
        EndMenu();
    }
    if (BeginMenu("View")) {
        MenuItem("Edit mode (click to select)", "Ctrl+E", &preview_.edit_mode);
        if (MenuItem("Reset widget values")) preview_.reset_values = true;
        if (MenuItem("Replay animations")) mm::ResetAnimations();
        if (MenuItem("Reset panel layout")) reset_layout_ = true;
        Separator();
        SetNextItemWidth(120);
        SliderFloat("UI scale", &settings_.ui_scale, 0.75f, 2.0f, "%.2f");
        if (IsItemDeactivatedAfterEdit()) {
            ApplyEditorStyle();
            SaveSettings();
        }
        EndMenu();
    }

    // edit / interact switch, the active one is highlighted
    Separator();
    const ImVec4 accent = GetStyle().Colors[ImGuiCol_CheckMark];
    auto mode_button = [&](const char* icon, const char* label, bool on) {
        if (on) PushStyleColor(ImGuiCol_Text, accent);
        const bool clicked = MenuItem(IconText(icon, label).c_str(), nullptr, on);
        if (on) PopStyleColor();
        return clicked;
    };
    if (mode_button("arrow-pointer", "Edit", preview_.edit_mode)) preview_.edit_mode = true;
    if (mode_button("hand-pointer", "Interact", !preview_.edit_mode)) preview_.edit_mode = false;
    Separator();

    // quick theme switch
    const std::string current = GetStr(doc_, "theme");
    SetNextItemWidth(150);
    if (BeginCombo("##theme", IconText("palette", current).c_str())) {
        for (auto& [name, theme] : doc_["themes"].items())
            if (Selectable(name.c_str(), name == current)) {
                doc_["theme"] = name;
                MarkDirty();
            }
        EndCombo();
    }
    if (MenuItem(IconText("file-export", "Export").c_str())) open_export_popup_ = true;

    // right side: JSON error > recent status message > project path
    std::string right;
    if (!load_error_.empty()) right = IconText("triangle-exclamation", "menu.json has a syntax error");
    else if (!status_.empty() && Now() - status_time_ < 4.0) right = status_;
    else right = project_dir_;
    const float w = CalcTextSize(right.c_str()).x;
    SetCursorPosX(std::max(GetCursorPosX() + 10, GetWindowWidth() - w - 16));
    if (!load_error_.empty()) {
        TextColored(ImVec4(1, 0.45f, 0.4f, 1), "%s", right.c_str());
        SetItemTooltip("%s", load_error_.c_str());
    } else {
        TextDisabled("%s", right.c_str());
    }
    EndMainMenuBar();
}

// ---------------------------------------------------------------- canvas

// The empty middle of the dock space. The menu windows float on top of it.
void Editor::Canvas()
{
    ImGuiDockNode* central = ImGui::DockBuilderGetCentralNode(dock_id_);
    if (!central) return;
    const ImVec2 p0 = central->Pos, p1 = central->Pos + central->Size;

    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    bg->AddRectFilled(p0, p1, IM_COL32(20, 21, 25, 255));
    const float grid = 32.0f * content_scale * settings_.ui_scale;
    for (float x = p0.x; x < p1.x; x += grid) bg->AddLine(ImVec2(x, p0.y), ImVec2(x, p1.y), IM_COL32(255, 255, 255, 8));
    for (float y = p0.y; y < p1.y; y += grid) bg->AddLine(ImVec2(p0.x, y), ImVec2(p1.x, y), IM_COL32(255, 255, 255, 8));
    const char* hint = preview_.edit_mode ? "Edit mode: click a widget to select it  |  Ctrl+E to interact  |  drag windows to move them"
                                          : "Interact mode: use the menu like in game  |  Ctrl+E to go back to editing";
    bg->AddText(ImVec2(p0.x + 12, p1.y - ImGui::GetFontSize() - 10), IM_COL32(255, 255, 255, 70), hint);

    preview_.origin = p0;
    preview_.selected = selected_;
    RenderPreview(doc_, assets_, preview_);
    if (preview_.doc_changed) MarkDirty();

    // hovering something in the tree highlights it on the canvas too
    if (preview_.hovered.empty()) preview_.hovered = tree_hovered_;
    tree_hovered_.clear();

    if (!preview_.clicked.empty()) {
        Select(preview_.clicked);
    } else if (preview_.edit_mode && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) &&
               ImGui::IsMouseHoveringRect(p0, p1, false)) {
        selected_.clear(); // clicked empty canvas
    }
    DrawSelectionOverlay(preview_, ImGui::GetColorU32(ImGuiCol_CheckMark));
}
