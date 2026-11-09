// The Claude panel: chat log, prompt box, and attaching reference screenshots
// (paste, drag & drop, file picker, or grabbing a region of the screen).
#include "editor.h"
#include "util.h"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include <cfloat>
#include <ctime>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace ImGui;

static const char* const kRecreatePrompt = "Recreate the menu in this screenshot as accurately as possible.";

// ---------------------------------------------------------------- sending

void Editor::SendPrompt(const std::string& text)
{
    if (text.empty() || bridge_.Busy()) return;
    if (dirty_) FlushEdits(); // Claude needs to see what's on screen right now

    BridgeRequest req;
    req.prompt = text;
    req.project_dir = project_dir_;
    req.selected_id = selected_;
    if (json* n = Selected()) req.selected_type = GetStr(*n, "type");
    req.model = settings_.model;
    req.claude_path = settings_.claude_path;
    req.use_inbox = settings_.use_inbox;
    req.editor_exe = exe_path_;
    for (auto& a : attachments_) req.images.push_back(fs::relative(a, project_dir_).string());
    attachments_.clear();
    offer_json_fix_ = false;
    bridge_.Send(req);
    scroll_chat_ = true;
}

// ---------------------------------------------------------------- attachments
// Attached screenshots get copied into <project>/.bridge/attachments so
// Claude (which only sees the project folder) can open them.

std::string Editor::NewAttachmentPath(const std::string& ext)
{
    const fs::path dir = fs::path(project_dir_) / ".bridge" / "attachments";
    std::error_code ec;
    fs::create_directories(dir, ec);
    char stamp[32];
    const time_t now = time(nullptr);
    strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", localtime(&now));
    return (dir / ("ref_" + std::string(stamp) + "_" + std::to_string(++attachment_counter_) + ext)).string();
}

bool Editor::AttachImage(const std::string& src)
{
    std::error_code ec;
    if (!fs::is_regular_file(src, ec)) {
        SetStatus("Not a file: " + src);
        return false;
    }
    if (!IsImageFile(src)) {
        SetStatus("Not an image: " + src);
        return false;
    }

    std::string dst;
    if (HasExtension(src, {".png", ".jpg", ".jpeg"})) {
        dst = NewAttachmentPath(fs::path(src).extension().string());
        fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            SetStatus("Couldn't copy " + src + ": " + ec.message());
            return false;
        }
    } else {
        // webp/bmp/etc: turn it into a png so every tool can read it
        dst = NewAttachmentPath(".png");
        int status = 0;
        RunCommand("magick " + ShellQuote(src) + " " + ShellQuote(dst) + " 2>/dev/null", &status);
        if (status != 0 || !fs::exists(dst, ec)) {
            dst = NewAttachmentPath(fs::path(src).extension().string());
            fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
        }
    }
    attachments_.push_back(dst);
    focus_claude_ = true;
    SetStatus("Attached " + fs::path(src).filename().string());
    return true;
}

// Grabs an image straight off the clipboard (wl-paste on Wayland, xclip on X11).
// Also handles files copied in a file manager, which show up as a URI list.
void Editor::PasteImage()
{
    const bool wayland = OnWayland();
    auto clipboard = [&](const std::string& type) {
        return wayland ? RunCommand("wl-paste --no-newline --type " + type + " 2>/dev/null")
                       : RunCommand("xclip -selection clipboard -t " + type + " -o 2>/dev/null");
    };
    const std::string types = wayland ? RunCommand("wl-paste --list-types 2>/dev/null") : clipboard("TARGETS");

    std::string image_type;
    bool has_files = false;
    for (const std::string& t : SplitLines(types)) {
        if (t == "image/png") { image_type = t; break; } // png is the safest, take it if it's offered
        if (image_type.empty() && t.rfind("image/", 0) == 0) image_type = t;
        if (t == "text/uri-list") has_files = true;
    }
    // the type goes into a shell command, so only allow boring characters
    for (char c : image_type)
        if (!(isalnum((unsigned char)c) || c == '/' || c == '+' || c == '-' || c == '.')) {
            image_type.clear();
            break;
        }

    if (!image_type.empty()) {
        const std::string data = clipboard(image_type);
        if (data.empty()) {
            SetStatus("The clipboard image was empty");
            return;
        }
        const std::string ext = image_type == "image/png" ? ".png" : image_type == "image/jpeg" ? ".jpg" : "." + image_type.substr(6);
        const std::string path = NewAttachmentPath(ext);
        std::ofstream(path, std::ios::binary) << data;
        if (ext == ".png" || ext == ".jpg") {
            attachments_.push_back(path);
            focus_claude_ = true;
            SetStatus("Pasted image");
        } else {
            AttachImage(path); // converts it to png
            std::error_code ec;
            fs::remove(path, ec);
        }
        return;
    }

    if (has_files) {
        for (const std::string& uri : SplitLines(clipboard("text/uri-list"))) {
            if (uri.rfind("file://", 0) != 0) continue;
            std::string path; // undo the %20 style escaping
            for (size_t i = 7; i < uri.size(); i++) {
                if (uri[i] == '%' && i + 2 < uri.size()) {
                    path += (char)std::stoi(uri.substr(i + 1, 2), nullptr, 16);
                    i += 2;
                } else {
                    path += uri[i];
                }
            }
            if (IsImageFile(path)) AttachImage(path);
        }
    }
}

void Editor::PickImages()
{
    if (!HaveCommand("zenity")) {
        OpenPopup("attach_path"); // no file picker, type a path instead
        return;
    }
    RunInBackground("zenity --file-selection --multiple --separator='\n' --title='Attach reference screenshots' "
                    "--file-filter='Images | *.png *.jpg *.jpeg *.webp *.bmp *.gif' 2>/dev/null",
                    [this](const std::string& output, bool ok) {
                        if (!ok) return;
                        for (const std::string& path : SplitLines(output))
                            if (!path.empty()) AttachImage(path);
                    });
}

// Drag a box around part of the screen and attach it.
void Editor::CaptureScreen()
{
    const std::string dst = NewAttachmentPath(".png");
    std::string cmd;
    if (OnWayland() && HaveCommand("grim") && HaveCommand("slurp")) cmd = "region=$(slurp) && grim -g \"$region\" " + ShellQuote(dst);
    else if (HaveCommand("magick")) cmd = "magick import " + ShellQuote(dst);
    else if (HaveCommand("import")) cmd = "import " + ShellQuote(dst);
    else {
        SetStatus("No screenshot tool found (install grim + slurp, or ImageMagick)");
        return;
    }
    const bool started = RunInBackground(cmd + " 2>/dev/null", [this, dst](const std::string&, bool ok) {
        std::error_code ec;
        if (ok && fs::exists(dst, ec) && fs::file_size(dst, ec) > 0) {
            attachments_.push_back(dst);
            focus_claude_ = true;
        } else {
            fs::remove(dst, ec);
            SetStatus("Capture cancelled");
        }
    });
    if (started) SetStatus("Drag a box around what you want to capture (Esc cancels)");
}

void Editor::OnDrop(int count, const char** paths)
{
    for (int i = 0; i < count; i++) {
        const std::string path = paths[i];
        if (IsImageFile(path)) AttachImage(path);
        else if (HasExtension(path, {".ttf", ".otf"})) ImportFont(path);
        else SetStatus("Drop images (to show Claude) or .ttf/.otf fonts");
    }
}

const Texture* Editor::Thumbnail(const std::string& path)
{
    auto it = thumbnails_.find(path);
    if (it == thumbnails_.end()) {
        Texture t;
        t.path = path;
        LoadTexture(t);
        it = thumbnails_.emplace(path, t).first;
    }
    return it->second.gl ? &it->second : nullptr;
}

// ---------------------------------------------------------------- panel

void Editor::ClaudePanel()
{
    if (focus_claude_) {
        SetNextWindowFocus();
        focus_claude_ = false;
    }
    if (!Begin("Claude")) {
        End();
        return;
    }
    // Ctrl+V anywhere in this panel pastes an image (if there's one on the clipboard)
    if (IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_V)) PasteImage();

    // settings row: mode, model, gear, new chat
    const char* modes[] = {"Claude Code CLI", "Inbox file"};
    int mode = settings_.use_inbox ? 1 : 0;
    SetNextItemWidth(140);
    if (Combo("##mode", &mode, modes, 2)) {
        settings_.use_inbox = mode == 1;
        SaveSettings();
    }
    SetItemTooltip("CLI: runs Claude Code in the project folder.\nInbox: writes prompts to .bridge/inbox.jsonl for a Claude session that's watching the folder.");
    if (!settings_.use_inbox) {
        SameLine();
        static const char* const models[] = {"claude-opus-5-5", "claude-sonnet-5-5", "claude-fable-5-1", "claude-haiku-4-5-20251001"};
        SetNextItemWidth(170);
        if (BeginCombo("##model", settings_.model.c_str())) {
            for (const char* m : models)
                if (Selectable(m, settings_.model == m)) {
                    settings_.model = m;
                    SaveSettings();
                }
            EndCombo();
        }
    }
    SameLine();
    if (Button(IconUtf8("gear"))) OpenPopup("bridge_settings");
    SetItemTooltip("Bridge settings");
    SameLine();
    if (Button(IconText("rotate-left", "New chat").c_str())) bridge_.NewConversation();

    if (BeginPopup("bridge_settings")) {
        TextUnformatted("Claude Code CLI path (empty = find it automatically)");
        SetNextItemWidth(420);
        if (InputText("##path", &settings_.claude_path)) SaveSettings();
        const std::string found = Bridge::FindClaude(settings_.claude_path);
        if (found.empty()) {
            TextColored(ImVec4(1, 0.6f, 0.4f, 1), "Not found. Install Claude Code or put its path above.");
        } else {
            TextDisabled("Using: %s", found.c_str());
            if (SmallButton("Copy login command")) SetClipboardText(("\"" + found + "\"").c_str());
        }
        TextWrapped("First time: run that in a terminal and log in with /login. The editor reuses that login.");
        Separator();
        TextDisabled("Session: %s", bridge_.SessionId().empty() ? "(new)" : bridge_.SessionId().c_str());
        if (bridge_.last_cost > 0) TextDisabled("Last request: $%.4f", bridge_.last_cost);
        EndPopup();
    }

    const float input_height = GetTextLineHeight() * 4 + GetStyle().FramePadding.y * 2;
    const float thumb_height = GetFrameHeight() * 2.2f;
    const float footer = input_height + GetFrameHeightWithSpacing() * 2 + GetStyle().ItemSpacing.y * 2 +
                         (attachments_.empty() ? 0.0f : thumb_height + GetStyle().ItemSpacing.y);
    BeginChild("chat", ImVec2(0, -footer), ImGuiChildFlags_Borders);
    ChatLog(thumb_height);
    EndChild();

    if (json* n = Selected()) TextDisabled("%s Context: %s (%s)", IconUtf8("crosshairs"), selected_.c_str(), GetStr(*n, "type").c_str());
    else TextDisabled("Context: nothing selected (whole menu)");

    AttachmentBar(thumb_height);

    const bool busy = bridge_.Busy();
    BeginDisabled(busy);
    const bool pressed_enter = InputTextMultiline("##prompt", &prompt_, ImVec2(GetContentRegionAvail().x - 70, input_height),
                                                  ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CtrlEnterForNewLine);
    EndDisabled();
    if (IsItemHovered() && prompt_.empty())
        SetTooltip(attachments_.empty() ? "Enter to send, Ctrl+Enter for a new line" : "Enter to send. Empty prompt = recreate the screenshot");
    SameLine();
    if (busy) {
        if (Button(IconText("stop", "Stop").c_str(), ImVec2(-FLT_MIN, input_height))) bridge_.Stop();
    } else if (Button(IconText("paper-plane", "Send").c_str(), ImVec2(-FLT_MIN, input_height)) || pressed_enter) {
        std::string text = prompt_;
        while (!text.empty() && (text.back() == '\n' || text.back() == ' ')) text.pop_back();
        if (text.empty() && !attachments_.empty()) text = kRecreatePrompt;
        if (!text.empty()) {
            SendPrompt(text);
            prompt_.clear();
        }
    }
    End();
}

void Editor::ChatLog(float thumb_height)
{
    if (bridge_.messages.size() <= 1) {
        TextDisabled("Describe a change and Claude edits the menu live. Try:");
        static const char* const ideas[] = {
            "Make the theme blood red with sharper corners",
            "Add a Players tab with a player list and teleport/spectate buttons",
            "Turn the sidebar into top tabs with icons",
            "Add a config tab with save/load slots and a theme selector",
            "Make every toggle in Visuals a checkbox instead",
        };
        for (const char* idea : ideas)
            if (Selectable(IconText("wand-magic-sparkles", idea).c_str())) prompt_ = idea;
        Spacing();
        TextDisabled("%s Got a screenshot of a menu you like? Paste it (Ctrl+V), drop it here,", IconUtf8("image"));
        TextDisabled("   or use Attach / Capture below, then hit Send.");
    }

    const ImVec4 user_color(0.55f, 0.7f, 1.0f, 1), tool_color(0.6f, 0.62f, 0.68f, 1), error_color(1, 0.5f, 0.45f, 1);
    auto colored_text = [](const ImVec4& color, const char* icon, const std::string& text) {
        PushStyleColor(ImGuiCol_Text, color);
        if (icon) TextWrapped("%s %s", IconUtf8(icon), text.c_str());
        else TextWrapped("%s", text.c_str());
        PopStyleColor();
    };

    for (size_t i = 0; i < bridge_.messages.size(); i++) {
        const ChatMessage& m = bridge_.messages[i];
        PushID((int)i);
        switch (m.role) {
        case ChatMessage::User:
            Spacing();
            colored_text(user_color, "user", m.text);
            for (size_t k = 0; k < m.images.size(); k++) {
                if (k) SameLine();
                if (const Texture* t = Thumbnail(m.images[k])) {
                    ImageFit(t->id(), t->w, t->h, thumb_height * 2.5f, thumb_height * 1.5f);
                    if (BeginItemTooltip()) {
                        ImageFit(t->id(), t->w, t->h, 640, 480);
                        EndTooltip();
                    }
                } else {
                    TextDisabled("[%s]", fs::path(m.images[k]).filename().string().c_str());
                }
            }
            break;
        case ChatMessage::Assistant: TextWrapped("%s", m.text.c_str()); break;
        case ChatMessage::Tool: colored_text(tool_color, "pen-to-square", m.text); break;
        case ChatMessage::Info: colored_text(GetStyle().Colors[ImGuiCol_TextDisabled], nullptr, m.text); break;
        case ChatMessage::Error: colored_text(error_color, "triangle-exclamation", m.text); break;
        }
        PopID();
    }

    if (bridge_.Busy()) TextDisabled("%c working", "|/-\\"[(int)(GetTime() * 8) % 4]);
    if (offer_json_fix_ && !bridge_.Busy() && !load_error_.empty() && Button(IconText("wrench", "Ask Claude to fix menu.json").c_str()))
        SendPrompt("menu.json doesn't parse anymore: " + load_error_ + "\nPlease fix the JSON syntax without changing the design.");

    // stick to the bottom when new messages come in
    if (bridge_.messages.size() != chat_messages_seen_ || scroll_chat_) {
        SetScrollHereY(1.0f);
        chat_messages_seen_ = bridge_.messages.size();
        scroll_chat_ = false;
    }
}

void Editor::AttachmentBar(float thumb_height)
{
    if (Button(IconText("paperclip", "Attach").c_str())) PickImages();
    SetItemTooltip("Attach screenshots (you can also drop image files on the window or Ctrl+V a copied image)");
    SameLine();
    if (Button(IconText("crop-simple", "Capture").c_str())) CaptureScreen();
    SetItemTooltip("Drag a box around part of your screen to attach it");
    SameLine();
    if (Button(IconText("paste", "Paste").c_str())) PasteImage();
    SetItemTooltip("Paste an image from the clipboard (Ctrl+V)");
    if (job_running_) {
        SameLine();
        TextDisabled("waiting...");
    }

    if (BeginPopup("attach_path")) {
        TextUnformatted("Image path:");
        SetNextItemWidth(380);
        if (IsWindowAppearing()) SetKeyboardFocusHere();
        if (InputText("##path", &attach_path_input_, ImGuiInputTextFlags_EnterReturnsTrue) && AttachImage(ExpandHome(attach_path_input_))) {
            attach_path_input_.clear();
            CloseCurrentPopup();
        }
        EndPopup();
    }

    int remove = -1;
    for (size_t k = 0; k < attachments_.size(); k++) {
        if (k) SameLine();
        PushID((int)k);
        BeginGroup();
        if (const Texture* t = Thumbnail(attachments_[k])) {
            ImageFit(t->id(), t->w, t->h, thumb_height * 2.0f, thumb_height);
            if (BeginItemTooltip()) {
                ImageFit(t->id(), t->w, t->h, 720, 540);
                TextDisabled("%d x %d px", t->w, t->h);
                EndTooltip();
            }
        } else {
            Button(IconUtf8("image"), ImVec2(thumb_height, thumb_height));
        }
        // little x in the corner of the thumbnail
        const ImVec2 top_left = GetItemRectMin(), bottom_right = GetItemRectMax();
        SetCursorScreenPos(ImVec2(bottom_right.x - GetFrameHeight() * 0.8f, top_left.y));
        if (SmallButton(IconUtf8("xmark"))) remove = (int)k;
        EndGroup();
        PopID();
    }
    if (remove >= 0) attachments_.erase(attachments_.begin() + remove);
}
