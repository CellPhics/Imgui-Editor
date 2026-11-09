// New / Open / Save As / Export. Uses real file dialogs (zenity or kdialog)
// when they're installed and falls back to a "type a path" popup otherwise.
#include "editor.h"
#include "exporter.h"
#include "util.h"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include <cfloat>
#include <filesystem>

namespace fs = std::filesystem;
using namespace ImGui;

// ---------------------------------------------------------------- background jobs

bool Editor::RunInBackground(const std::string& cmd, std::function<void(const std::string& output, bool ok)> done)
{
    if (job_running_) {
        SetStatus("Already waiting on a file window");
        return false;
    }
    if (job_thread_.joinable()) job_thread_.join();
    job_running_ = true;
    job_done_ = std::move(done);
    job_thread_ = std::thread([this, cmd] {
        int status = 0;
        std::string output = RunCommand(cmd, &status);
        while (!output.empty() && (output.back() == '\n' || output.back() == '\r')) output.pop_back();
        std::lock_guard<std::mutex> lock(job_mutex_);
        job_output_ = output;
        job_ok_ = status == 0;
        job_finished_ = true;
        job_running_ = false;
    });
    return true;
}

void Editor::PollBackgroundJob()
{
    std::string output;
    bool ok = false;
    {
        std::lock_guard<std::mutex> lock(job_mutex_);
        if (!job_finished_) return;
        job_finished_ = false;
        output = job_output_;
        ok = job_ok_;
    }
    auto done = std::move(job_done_);
    job_done_ = nullptr;
    if (done) done(output, ok);
}

// ---------------------------------------------------------------- dialogs

bool Editor::HaveDialogs()
{
    static const bool have = HaveCommand("zenity") || HaveCommand("kdialog");
    return have;
}

// `filter` looks like "Menu project | menu.json"
std::string Editor::DialogCommand(FileDialog kind, const std::string& title, const std::string& start, const std::string& filter)
{
    if (HaveCommand("zenity")) {
        std::string cmd = "zenity --file-selection --title=" + ShellQuote(title) + " --filename=" + ShellQuote(start);
        if (kind == FileDialog::Save) cmd += " --save";
        if (kind == FileDialog::Folder) cmd += " --directory";
        if (!filter.empty()) cmd += " --file-filter=" + ShellQuote(filter) + " --file-filter='All files | *'";
        return cmd + " 2>/dev/null";
    }
    // kdialog wants the filter the other way round: "pattern|Name"
    std::string cmd = kind == FileDialog::Open ? "kdialog --getopenfilename " : kind == FileDialog::Save ? "kdialog --getsavefilename " : "kdialog --getexistingdirectory ";
    cmd += ShellQuote(start);
    if (kind == FileDialog::Open && !filter.empty()) {
        const size_t bar = filter.find('|');
        std::string name = filter.substr(0, bar), pattern = bar == std::string::npos ? "*" : filter.substr(bar + 1);
        while (!pattern.empty() && pattern[0] == ' ') pattern.erase(0, 1);
        while (!name.empty() && name.back() == ' ') name.pop_back();
        cmd += " " + ShellQuote(pattern + "|" + name);
    }
    return cmd + " --title " + ShellQuote(title) + " 2>/dev/null";
}

// Where file dialogs start: the folder the current project lives in.
static std::string ProjectsFolder(const std::string& project_dir)
{
    const fs::path parent = fs::path(project_dir).parent_path();
    std::error_code ec;
    return (fs::is_directory(parent, ec) ? parent.string() : HomeDir()) + "/";
}

void Editor::FileNew()
{
    if (!HaveDialogs()) {
        project_action_ = ProjectAction::Create;
        project_path_input_ = (fs::path(project_dir_).parent_path() / "NewMenu").string();
        open_project_popup_ = true;
        return;
    }
    RunInBackground(DialogCommand(FileDialog::Save, "New menu: pick a name and where to put it", ProjectsFolder(project_dir_) + "NewMenu"),
                    [this](const std::string& path, bool ok) {
                        if (!ok || path.empty()) return;
                        std::error_code ec;
                        if (fs::exists(path, ec) && !fs::exists(fs::path(path) / "menu.json", ec) && !fs::is_empty(path, ec)) {
                            SetStatus("That folder isn't empty, pick a new name");
                            return;
                        }
                        if (dirty_) FlushEdits();
                        OpenProject(path, true);
                    });
}

void Editor::FileOpen()
{
    if (!HaveDialogs()) {
        project_action_ = ProjectAction::Open;
        project_path_input_ = project_dir_;
        open_project_popup_ = true;
        return;
    }
    RunInBackground(DialogCommand(FileDialog::Open, "Open a menu (pick its menu.json)", ProjectsFolder(project_dir_), "Menu project | menu.json"),
                    [this](const std::string& path, bool ok) {
                        if (!ok || path.empty()) return;
                        std::error_code ec;
                        const fs::path dir = fs::is_directory(path, ec) ? fs::path(path) : fs::path(path).parent_path();
                        if (!fs::exists(dir / "menu.json", ec)) {
                            SetStatus("That folder doesn't have a menu.json");
                            return;
                        }
                        if (dirty_) FlushEdits();
                        OpenProject(dir.string(), false);
                    });
}

void Editor::FileSaveAs()
{
    if (!HaveDialogs()) {
        project_action_ = ProjectAction::SaveAs;
        project_path_input_ = project_dir_ + "-copy";
        open_project_popup_ = true;
        return;
    }
    RunInBackground(DialogCommand(FileDialog::Save, "Save menu as: pick a name and where to put it", project_dir_ + "-copy"),
                    [this](const std::string& path, bool ok) {
                        if (ok && !path.empty()) SaveProjectAs(path);
                    });
}

// Copies the whole project (menu.json + assets) into a new folder and switches to it.
bool Editor::SaveProjectAs(const std::string& dir_in)
{
    std::error_code ec;
    const fs::path dir = fs::absolute(ExpandHome(dir_in), ec);
    if (dir.empty()) return false;
    if (fs::equivalent(dir, project_dir_, ec)) { // "save as" onto itself is just save
        FlushEdits();
        return true;
    }
    if (fs::exists(dir, ec) && !fs::is_directory(dir, ec)) {
        SetStatus("There's already a file with that name");
        return false;
    }
    if (fs::exists(dir, ec) && !fs::is_empty(dir, ec) && !fs::exists(dir / "menu.json", ec)) {
        SetStatus("That folder isn't empty, pick a new name");
        return false;
    }

    fs::create_directories(dir, ec);
    if (ec) {
        SetStatus("Couldn't create " + dir.string() + ": " + ec.message());
        return false;
    }
    const fs::path assets = fs::path(project_dir_) / "assets";
    if (fs::is_directory(assets, ec)) fs::copy(assets, dir / "assets", fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
    if (ec) {
        SetStatus("Couldn't copy the assets: " + ec.message());
        return false;
    }
    if (!WriteFileAtomic((dir / "menu.json").string(), doc_.dump(2))) {
        SetStatus("Couldn't write menu.json");
        return false;
    }
    FlushEdits(); // the original keeps your latest changes too
    if (!OpenProject(dir.string(), false)) return false;
    SetStatus("Saved as " + dir.string());
    return true;
}

bool Editor::ExportTo(const std::string& dir, std::string& message)
{
    const ExportResult result = ExportProject(doc_, project_dir_, data_dir_, dir.empty() ? export_dir_ : ExpandHome(dir));
    message = result.message;
    return result.ok;
}

// ---------------------------------------------------------------- popups

void Editor::ExportPopup()
{
    if (open_export_popup_) {
        OpenPopup("Export C++");
        open_export_popup_ = false;
        export_message_.clear();
    }
    SetNextWindowSize(ImVec2(560, 0) * content_scale * settings_.ui_scale, ImGuiCond_Appearing);
    if (!BeginPopupModal("Export C++", nullptr)) return;

    TextWrapped("Writes menu_generated.h, the mm_widgets runtime, your fonts and images, and a small example app.");
    SetNextItemWidth(HaveDialogs() ? -110 : -FLT_MIN);
    InputText("##dir", &export_dir_);
    if (HaveDialogs()) {
        SameLine();
        BeginDisabled(job_running_);
        if (Button(IconText("folder-open", "Browse...").c_str(), ImVec2(-FLT_MIN, 0)))
            RunInBackground(DialogCommand(FileDialog::Folder, "Export C++ to...", ExpandHome(export_dir_) + "/"), [this](const std::string& path, bool ok) {
                if (ok && !path.empty()) export_dir_ = path;
            });
        EndDisabled();
    }
    if (Button(IconText("file-export", "Export").c_str(), ImVec2(120, 0))) {
        if (dirty_) FlushEdits();
        const ExportResult result = ExportProject(doc_, project_dir_, data_dir_, ExpandHome(export_dir_));
        export_message_ = result.message;
        if (result.ok)
            for (auto& f : result.files) export_message_ += "\n  " + f;
    }
    SameLine();
    if (Button("Close", ImVec2(120, 0))) CloseCurrentPopup();
    if (!export_message_.empty()) {
        Separator();
        TextWrapped("%s", export_message_.c_str());
    }
    EndPopup();
}

// Only used when there's no zenity/kdialog: type the folder path by hand.
void Editor::ProjectPopup()
{
    if (open_project_popup_) {
        OpenPopup("Project");
        open_project_popup_ = false;
    }
    SetNextWindowSize(ImVec2(560, 0) * content_scale * settings_.ui_scale, ImGuiCond_Appearing);
    if (!BeginPopupModal("Project", nullptr)) return;

    const bool open = project_action_ == ProjectAction::Open;
    const bool create = project_action_ == ProjectAction::Create;
    TextWrapped("%s", open ? "Project folder (the one with menu.json in it):"
                      : create ? "Folder for the new project (gets created if needed):"
                               : "Save a copy of this project (menu + assets) to:");
    SetNextItemWidth(-FLT_MIN);
    InputText("##path", &project_path_input_);
    const std::string path = ExpandHome(project_path_input_);
    const bool has_menu = FileExists((fs::path(path) / "menu.json").string());
    if (open && !has_menu) TextColored(ImVec4(1, 0.6f, 0.4f, 1), "No menu.json there.");
    if (create && has_menu) TextColored(ImVec4(1, 0.8f, 0.4f, 1), "That folder already has a menu.json, it'll just get opened.");
    if (!open && !create && has_menu) TextColored(ImVec4(1, 0.8f, 0.4f, 1), "That folder already has a menu.json, it'll get overwritten.");

    if (Button(open ? "Open" : create ? "Create" : "Save", ImVec2(120, 0))) {
        if (dirty_) FlushEdits();
        const bool ok = open || create ? OpenProject(path, create) : SaveProjectAs(path);
        if (ok) CloseCurrentPopup();
    }
    SameLine();
    if (Button("Cancel", ImVec2(120, 0))) CloseCurrentPopup();
    EndPopup();
}
