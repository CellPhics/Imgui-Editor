// The editor application: panels, selection, undo, file sync and export.
#pragma once
#include "assets.h"
#include "bridge.h"
#include "model.h"
#include "renderer.h"
#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <string>
#include <vector>

struct Settings {
    std::string last_project;
    std::string claude_path;
    std::string model = "claude-sonnet-5-5";
    std::string mode = "cli";
    float ui_scale = 1.0f;
};

class Editor {
public:
    bool Init(const std::string& data_dir, const std::string& config_dir, const std::string& project_dir);
    void BeforeFrame(); // between frames: font/image sync
    void Frame();
    void Shutdown();
    bool quit = false;
    void DebugSelect(const std::string& id) { Select(id); }
    void DebugAttach(const std::string& path) { AttachImage(path); }
    void OnDrop(int count, const char** paths); // files dropped on the window
    void DebugPrompt(const std::string& p) { SendPrompt(p); }
    bool BridgeBusy() const { return bridge_.Busy(); }
    void SetClaudePath(const std::string& p) { settings_.claude_path = p; }
    bool ExportTo(const std::string& dir, std::string& msg);
    float content_scale = 1.0f;

private:
    // project / document
    bool OpenProject(const std::string& dir, bool create_if_missing);
    void CreateProjectFiles(const std::string& dir);
    void Save();
    void PollFile(bool force = false);
    void MarkDirty() { dirty_ = true; }
    void CommitIfIdle();
    void Undo();
    void Redo();
    void SetDoc(const json& d);

    // selection / edits
    void Select(const std::string& id);
    json* Selected();
    void AddNode(const std::string& type);
    void InsertNode(json node);
    void DeleteSelected();
    void DuplicateSelected();
    void MoveSelected(int dir);
    void MoveNode(const std::string& id, const std::string& target, bool into);
    void Later(std::function<void()> f) { later_.push_back(std::move(f)); }

    // panels
    void SetupDock(unsigned dock_id);
    void MenuBar();
    void Canvas();
    void Hierarchy();
    void HierarchyNode(json& n, json* parent);
    void Inspector();
    void PropEditor(json& n, const PropDef& p);
    void ThemePanel();
    void AssetsPanel();
    void ClaudePanel();
    void CodePanel();
    void ExportPopup();
    void ProjectPopup();
    bool IconPicker(const char* popup_id, std::string& icon);
    void SendPrompt(const std::string& text);
    bool AttachImage(const std::string& src);
    std::string NewAttachmentPath(const std::string& ext);
    void PasteImage();
    void StartPicker();
    void StartCapture();
    void PollAsync();
    // Native file dialogs (zenity / kdialog), run off the UI thread.
    bool HaveDialogs();
    bool RunDialog(const std::string& cmd, std::function<void(const std::string&)> on_pick);
    std::string DialogCmd(int kind, const std::string& title, const std::string& start, const std::string& filter = "");
    void FileOpen();
    void FileSaveAs();
    void FileNew();
    bool SaveProjectAs(const std::string& dir);
    const Texture* Thumb(const std::string& path);
    bool ImportFont(const std::string& src);
    void LoadSettings();
    void SaveSettings();
    void ApplyEditorStyle();

    std::string data_dir_, config_dir_, ini_path_, exe_path_;
    std::string project_dir_, menu_path_;
    json doc_;
    std::string committed_;
    std::vector<std::string> undo_, redo_;
    bool dirty_ = false;
    long long file_mtime_ = 0;
    double last_poll_ = 0;
    std::string load_error_;
    std::vector<std::string> warnings_;
    std::string status_;
    double status_time_ = 0;

    std::string selected_;
    std::set<std::string> reveal_;
    bool scroll_to_sel_ = false;
    std::string hier_hover_;
    std::string clipboard_;
    PreviewCtx preview_;
    Assets assets_;
    Bridge bridge_;
    Settings settings_;
    std::vector<std::function<void()>> later_;

    // ui state
    unsigned dock_id_ = 0;
    bool layout_reset_ = false;
    bool open_export_ = false, open_project_ = false;
    std::string project_path_input_;
    std::string export_dir_;
    std::string export_msg_;
    std::string prompt_;
    bool scroll_chat_ = false;
    size_t chat_seen_ = 0;
    std::string code_cache_, code_sig_;
    std::string raw_json_, raw_json_for_;
    std::string raw_json_err_;
    std::string id_edit_, id_edit_for_;
    std::string icon_filter_;
    std::string new_theme_name_;
    std::string color_filter_;
    std::string import_path_;
    double last_image_sync_ = 0;
    bool fonts_dirty_ = true;
    bool json_error_offered_ = false;

    // reference screenshots for the next prompt
    std::vector<std::string> attachments_;
    std::map<std::string, Texture> thumbs_;
    std::string attach_path_input_;
    bool focus_claude_ = false;
    int attach_counter_ = 0;
    std::mutex async_mu_;
    std::vector<std::pair<std::string, bool>> async_files_; // path, already in attachments dir
    std::vector<std::string> async_errors_;
    std::atomic<bool> async_busy_{false};
    std::function<void(const std::string&)> dialog_cb_;
    std::string dialog_result_;
    bool dialog_ready_ = false;
    int project_mode_ = 0; // text fallback popup: 0 open, 1 new, 2 save as
    std::thread async_;
};
