// The editor app. It's split over a few files so none of them is a monster:
//   editor.cpp         project/document handling, undo, selection, the frame loop, menu bar, canvas
//   editor_panels.cpp  Hierarchy, Inspector, Theme, Assets and Code panels
//   editor_claude.cpp  the Claude panel + screenshot attachments
//   editor_files.cpp   New/Open/Save As/Export (file dialogs and their fallbacks)
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
#include <string>
#include <thread>
#include <vector>

struct Settings {
    std::string last_project;
    std::string claude_path; // empty = auto-detect
    std::string model = "claude-opus-5-5";
    bool use_inbox = false;
    float ui_scale = 1.0f;
};

class Editor {
public:
    bool Init(const std::string& data_dir, const std::string& config_dir, const std::string& project_dir);
    void BeforeFrame(); // between frames: font/image reloads
    void Frame();
    void Shutdown();
    void OnDrop(int count, const char** paths); // files dropped onto the window

    bool quit = false;
    float content_scale = 1.0f; // monitor DPI scale

    // for the command line modes in main.cpp
    void SelectNode(const std::string& id) { Select(id); }
    bool ExportTo(const std::string& dir, std::string& message);

private:
    // ---- editor.cpp
    void LoadSettings();
    void SaveSettings();
    void ApplyEditorStyle();
    bool OpenProject(const std::string& dir, bool create_if_missing);
    void CreateProjectFiles(const std::string& dir);
    void WriteClaudeGuide(const std::string& dir);
    void Save();
    void MarkDirty() { dirty_ = true; }
    void FlushEdits();      // push pending edits into undo history and save right now
    void CommitIfIdle();    // same, but waits until you let go of the mouse
    void ReloadIfChanged(bool force = false);
    void SetDoc(const json& doc);
    void Undo();
    void Redo();
    void SetStatus(const std::string& text);

    void Select(const std::string& id);
    json* Selected();
    void AddNode(const std::string& type);
    void InsertNode(json node);
    void DeleteSelected();
    void DuplicateSelected();
    void MoveSelected(int direction);
    void MoveNode(const std::string& id, const std::string& target, bool into);
    void CopySelected();
    void PasteNode();
    // Tree edits from inside a loop over the tree have to wait until the loop is done.
    void Later(std::function<void()> f) { deferred_.push_back(std::move(f)); }

    void HandleShortcuts();
    void SetupDockLayout(unsigned dock_id);
    void MenuBar();
    void Canvas();

    // ---- editor_panels.cpp
    void HierarchyPanel();
    void HierarchyNode(json& n);
    void InspectorPanel();
    void PropEditor(json& n, const PropDef& p);
    bool StyleOverrideEditor(json& overrides);
    bool IconPicker(const char* popup_id, std::string& icon);
    void ThemePanel();
    void AssetsPanel();
    void FontsTab();
    void ImagesTab();
    void IconsTab();
    void CodePanel();
    bool ImportFont(const std::string& src);
    std::string CopyIntoAssets(const std::string& src, const char* subdir);

    // ---- editor_claude.cpp
    void ClaudePanel();
    void ChatLog(float thumb_height);
    void AttachmentBar(float thumb_height);
    void SendPrompt(const std::string& text);
    bool AttachImage(const std::string& src);
    std::string NewAttachmentPath(const std::string& ext);
    void PasteImage();
    void PickImages();
    void CaptureScreen();
    const Texture* Thumbnail(const std::string& path);

    // ---- editor_files.cpp
    enum class FileDialog { Open, Save, Folder };
    bool HaveDialogs();
    std::string DialogCommand(FileDialog kind, const std::string& title, const std::string& start, const std::string& filter = "");
    void FileNew();
    void FileOpen();
    void FileSaveAs();
    bool SaveProjectAs(const std::string& dir);
    void ExportPopup();
    void ProjectPopup();

    // Runs a shell command on a background thread (file pickers, screen capture
    // block until you're done with them) and calls `done` on the UI thread
    // with its output. One job at a time.
    bool RunInBackground(const std::string& cmd, std::function<void(const std::string& output, bool ok)> done);
    void PollBackgroundJob();

    // ---- state
    std::string data_dir_, config_dir_, ini_path_, exe_path_;
    Settings settings_;

    // document
    std::string project_dir_, menu_path_;
    json doc_;
    std::string committed_; // last state that went into undo history (as a JSON dump)
    std::vector<std::string> undo_, redo_;
    bool dirty_ = false;    // doc_ has edits that aren't committed yet
    long long file_mtime_ = 0;
    double last_file_check_ = 0;
    std::string load_error_;
    std::vector<std::string> load_warnings_;
    std::string status_;
    double status_time_ = 0;

    // selection
    std::string selected_;
    std::set<std::string> reveal_in_tree_; // tree nodes to open so the selection is visible
    bool scroll_tree_to_selection_ = false;
    std::string tree_hovered_;
    PreviewState preview_;
    Assets assets_;
    std::vector<std::function<void()>> deferred_;

    // panels
    unsigned dock_id_ = 0;
    bool reset_layout_ = false;
    std::string code_cache_, code_cache_for_;
    std::string raw_json_, raw_json_for_, raw_json_error_;
    std::string id_edit_, id_edit_for_;
    std::string icon_filter_, override_filter_, color_filter_;
    std::string rename_theme_to_;
    ImVec4 recolor_bg_ = ImVec4(0.085f, 0.085f, 0.105f, 1);
    bool recolor_light_ = false;
    std::string import_path_;
    double last_image_check_ = 0;

    // claude
    Bridge bridge_;
    std::string prompt_;
    bool scroll_chat_ = false;
    size_t chat_messages_seen_ = 0;
    bool offer_json_fix_ = false;
    bool focus_claude_ = false;
    std::vector<std::string> attachments_;
    std::map<std::string, Texture> thumbnails_;
    std::string attach_path_input_;
    int attachment_counter_ = 0;

    // files
    enum class ProjectAction { Open, Create, SaveAs };
    bool open_export_popup_ = false, open_project_popup_ = false;
    ProjectAction project_action_ = ProjectAction::Open;
    std::string project_path_input_;
    std::string export_dir_, export_message_;

    // background job
    std::thread job_thread_;
    std::mutex job_mutex_;
    std::atomic<bool> job_running_{false};
    bool job_finished_ = false;
    bool job_ok_ = false;
    std::string job_output_;
    std::function<void(const std::string&, bool)> job_done_;
};

// Little UI helpers shared by the editor files.
std::string IconText(const char* icon, const std::string& text); // "<glyph>  text"
void ImageFit(ImTextureID tex, int w, int h, float max_w, float max_h);
