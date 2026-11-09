#include "bridge.h"
#include "util.h"
#include "nlohmann/json.hpp"
#include "stb/stb_image.h"
#include <algorithm>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;
namespace fs = std::filesystem;
using njson = nlohmann::json;

static const char* const kWorkingText = "Claude is working...";
static const char* const kInboxWaitingText = "Sent to .bridge/inbox.jsonl, waiting for a reply...";

// ---------------------------------------------------------------- prompts

static const char* kSystemPrompt =
    "You are the design assistant built into ImGui Menu Editor, a visual editor for Dear ImGui mod menus. "
    "The user types requests in the editor's prompt box. The whole menu (windows, tabs, widgets, themes, fonts, images) "
    "lives in ./menu.json in the current directory, and the editor hot-reloads that file the moment you save it. "
    "Read ./CLAUDE.md for the JSON schema before your first edit. Make exactly the change requested, preserve everything else, "
    "keep the JSON valid and every id unique. Prefer targeted Edit calls over rewriting the whole file. "
    "Images and fonts must live under ./assets/ and be registered in menu.json. "
    "Never delete the user's existing themes: when you need new colors, add a new theme (or edit the active one) and make it active. "
    "Finish with one or two short sentences describing what you changed.";

// How the helper command is written, both in the prompt and the permission rule.
static std::string ToolCommand(const std::string& exe)
{
    return (exe.find(' ') != std::string::npos ? "\"" + exe + "\"" : exe) + " tool";
}

static std::string HelperToolsPrompt(const std::string& exe)
{
    if (exe.empty()) return "";
    const std::string t = ToolCommand(exe);
    return "\n\nHelper commands you can run with Bash (from the project folder):\n"
           "- " + t + " render [--show NODE_ID]   renders the menu exactly like the editor/export does into .bridge/render.png "
           "and prints every widget's rectangle in that image. Read the PNG to see the result. --show reveals the tab containing NODE_ID.\n"
           "- " + t + " compare REFERENCE .bridge/render.png   similarity score, size mismatch, the most different areas with "
           "reference vs render colors, plus .bridge/diff.png (differences in red).\n"
           "- " + t + " inspect IMAGE [--top N] [x,y ...] [x,y,w,h ...]   image size, dominant colors, exact pixel/region colors.\n"
           "- " + t + " crop IMAGE x y w h OUT.png [--scale N] [--key auto|#RRGGBB] [--tolerance N]   cut out / zoom into part of an image. "
           "Use it to cut logos and icons into assets/images/; --key auto makes the background (the region's top-left color) transparent.\n"
           "- " + t + " fonts [filter]   list installed fonts;  " + t + " import-font PATH   copy one into assets/fonts.\n"
           "Run each helper as its own Bash call: no pipes, `;`, `&&`, variables or scripts (they get blocked). "
           "Change menu.json only with the Edit/Write tools.\n"
           "When matching a reference image, iterate: edit menu.json -> render -> compare -> Read the diff/render -> fix the biggest "
           "differences first (size, layout, then colors, then fonts and small offsets). Don't stop while the comparison is "
           "still improving: do at least 6 passes and keep going until similarity is 97%+ or two passes in a row gain under "
           "0.2 points. Use the schema's look/style options (CLAUDE.md) instead of settling for defaults, and crop artwork the "
           "built-in icons can't match (tab icons, logos, weapon images) from the reference into assets/images. "
           "Then summarize the final similarity and what still differs.";
}

static std::string BuildPrompt(const BridgeRequest& r)
{
    std::string p = r.prompt;
    if (!r.images.empty()) {
        p += "\n\n[Attached reference images - open each one with the Read tool]\n";
        for (auto& image : r.images) {
            int w = 0, h = 0, channels = 0;
            std::string size;
            if (stbi_info((fs::path(r.project_dir) / image).string().c_str(), &w, &h, &channels))
                size = " (" + std::to_string(w) + "x" + std::to_string(h) + " px)";
            p += "- ./" + image + size + "\n";
        }
        p += "Follow the \"Recreating a screenshot\" workflow in CLAUDE.md: match it as exactly as possible "
             "(layout, sizes in pixels, colors sampled with `tool inspect`, fonts, icons, text), and check with `tool render`.\n";
    }
    p += "\n\n[Editor context]\n- Menu file: ./menu.json (the editor reloads it when you save)\n";
    if (!r.selected_id.empty())
        p += "- Selected in the editor: node id \"" + r.selected_id + "\" (type " + r.selected_type +
             "). \"this\", \"it\" or \"selected\" mean this node.\n";
    else
        p += "- Nothing is selected in the editor.\n";
    return p;
}

// ---------------------------------------------------------------- finding the CLI

static bool IsExecutable(const std::string& p)
{
    std::error_code ec;
    return !p.empty() && access(p.c_str(), X_OK) == 0 && fs::is_regular_file(p, ec);
}

// "2.1.284" -> {2, 1, 284} so versions sort properly
static std::vector<int> VersionNumbers(const std::string& s)
{
    std::vector<int> nums;
    int cur = -1;
    for (char c : s) {
        if (isdigit((unsigned char)c)) cur = (cur < 0 ? 0 : cur * 10) + (c - '0');
        else if (cur >= 0) { nums.push_back(cur); cur = -1; }
    }
    if (cur >= 0) nums.push_back(cur);
    return nums;
}

// Newest `dir/<prefix>*/<rel>` that's executable.
static std::string NewestInstall(const fs::path& dir, const std::string& prefix, const std::string& rel)
{
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return "";
    std::string best, best_name;
    for (auto& e : fs::directory_iterator(dir, ec)) {
        const std::string name = e.path().filename().string();
        if (name.rfind(prefix, 0) != 0) continue;
        const std::string candidate = (e.path() / rel).string();
        if (!IsExecutable(candidate)) continue;
        if (best.empty() || VersionNumbers(name) > VersionNumbers(best_name)) {
            best = candidate;
            best_name = name;
        }
    }
    return best;
}

std::string Bridge::FindClaude(const std::string& configured)
{
    if (IsExecutable(configured)) return configured;

    if (const char* path = getenv("PATH")) {
        std::string dirs = path;
        size_t start = 0;
        while (start <= dirs.size()) {
            size_t end = dirs.find(':', start);
            if (end == std::string::npos) end = dirs.size();
            const std::string candidate = dirs.substr(start, end - start) + "/claude";
            if (IsExecutable(candidate)) return candidate;
            start = end + 1;
        }
    }

    const fs::path home = HomeDir();
    for (const fs::path& p : {home / ".local/bin/claude", home / ".claude/local/claude", home / ".npm-global/bin/claude", fs::path("/usr/local/bin/claude")})
        if (IsExecutable(p.string())) return p.string();

    // the desktop app keeps its own copy, one folder per version
    std::string found = NewestInstall(home / ".config/Claude/claude-code", "", "claude");
    if (!found.empty()) return found;
    // editor extensions bundle it too
    for (const char* ext_dir : {".vscode/extensions", ".cursor/extensions", ".antigravity-ide/extensions", ".windsurf/extensions"}) {
        found = NewestInstall(home / ext_dir, "anthropic.claude-code-", "resources/native-binary/claude");
        if (!found.empty()) return found;
    }
    return "";
}

// ---------------------------------------------------------------- public api

Bridge::~Bridge()
{
    Stop();
    if (reader_.joinable()) reader_.join();
}

void Bridge::NewConversation()
{
    session_id_.clear();
    messages.push_back({ChatMessage::Info, "Started a new conversation."});
}

void Bridge::Reset()
{
    Stop();
    messages.clear();
    session_id_.clear();
    request_id_ = 0;
}

void Bridge::Send(const BridgeRequest& req)
{
    if (busy_) return;
    ChatMessage msg{ChatMessage::User, req.prompt, {}};
    for (auto& image : req.images) msg.images.push_back((fs::path(req.project_dir) / image).string());
    messages.push_back(msg);
    finished_ = false;

    const std::string prompt = BuildPrompt(req);
    if (req.use_inbox) StartInbox(req, prompt);
    else StartCli(req, prompt);
}

void Bridge::Stop()
{
    if (pid_ > 0) {
        kill(-pid_, SIGTERM); // whole process group, Claude spawns its own children
        kill(pid_, SIGTERM);
    } else if (busy_) { // inbox mode, just stop waiting
        busy_ = false;
        messages.push_back({ChatMessage::Info, "Stopped waiting for a reply."});
    }
}

void Bridge::Poll()
{
    if (pid_ > 0) PollCli();
    else if (busy_) PollInbox();
}

bool Bridge::TakeFinished()
{
    const bool f = finished_;
    finished_ = false;
    return f;
}

void Bridge::Finish(int exit_code)
{
    busy_ = false;
    finished_ = true;
    // drop the "working..." placeholder
    for (auto it = messages.begin(); it != messages.end(); ++it)
        if (it->role == ChatMessage::Info && (it->text == kWorkingText || it->text == kInboxWaitingText)) {
            messages.erase(it);
            break;
        }
    if (exit_code != 0 && exit_code != -1) {
        std::string msg = "Claude exited with code " + std::to_string(exit_code) + ".";
        if (exit_code == 127) msg += " The CLI couldn't be started.";
        if (!stderr_tail_.empty()) msg += "\n" + stderr_tail_;
        messages.push_back({ChatMessage::Error, msg});
    }
}

// ---------------------------------------------------------------- cli

void Bridge::StartCli(const BridgeRequest& req, const std::string& prompt)
{
    const std::string exe = FindClaude(req.claude_path);
    if (exe.empty()) {
        messages.push_back({ChatMessage::Error, "Couldn't find the Claude Code CLI. Install it (https://claude.com/claude-code) "
                                                "or set its path in the bridge settings (gear icon)."});
        return;
    }
    if (reader_.joinable()) reader_.join();

    // Claude only gets file tools, plus Bash restricted to our own helper command.
    std::vector<std::string> args = {exe, "-p", prompt, "--output-format", "stream-json", "--verbose",
                                     "--permission-mode", "acceptEdits",
                                     "--append-system-prompt", std::string(kSystemPrompt) + HelperToolsPrompt(req.editor_exe),
                                     "--allowedTools", "Read", "Edit", "Write", "Glob", "Grep"};
    if (!req.editor_exe.empty()) args.push_back("Bash(" + ToolCommand(req.editor_exe) + ":*)");
    if (!req.model.empty()) { args.push_back("--model"); args.push_back(req.model); }
    if (!session_id_.empty()) { args.push_back("--resume"); args.push_back(session_id_); }

    // If the editor itself was launched from inside a Claude Code session, its
    // env vars would make the CLI think it's nested and act weird. Drop them.
    const bool nested = getenv("CLAUDECODE") != nullptr;
    std::vector<std::string> env;
    for (char** e = environ; *e; e++) {
        const std::string kv = *e;
        const std::string key = kv.substr(0, kv.find('='));
        if (key == "CLAUDECODE" || key.rfind("CLAUDE_CODE_", 0) == 0 || key == "CLAUDE_PID" || key == "CLAUDE_AGENT_SDK_VERSION" ||
            key == "CLAUDE_EFFORT" || key.rfind("CLAUDE_PREVIEW", 0) == 0)
            continue;
        if (nested && key == "ANTHROPIC_BASE_URL") continue;
        env.push_back(kv);
    }

    // build everything before fork(), the child should only call exec-safe stuff
    std::vector<char*> argv, envp;
    for (auto& a : args) argv.push_back(a.data());
    argv.push_back(nullptr);
    for (auto& e : env) envp.push_back(e.data());
    envp.push_back(nullptr);

    int fds[2];
    if (pipe(fds) != 0) {
        messages.push_back({ChatMessage::Error, "pipe() failed"});
        return;
    }
    const std::string dir = req.project_dir;
    const pid_t pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        messages.push_back({ChatMessage::Error, "fork() failed"});
        return;
    }
    if (pid == 0) {
        if (chdir(dir.c_str()) != 0) _exit(126);
        const int devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0) dup2(devnull, 0);
        dup2(fds[1], 1); // stdout and stderr both go to our pipe
        dup2(fds[1], 2);
        close(fds[0]);
        close(fds[1]);
        setpgid(0, 0); // own process group so Stop() can kill everything it spawned
        execve(argv[0], argv.data(), envp.data());
        _exit(127);
    }

    close(fds[1]);
    pid_ = pid;
    output_done_ = false;
    busy_ = true;
    stderr_tail_.clear();
    messages.push_back({ChatMessage::Info, kWorkingText});

    reader_ = std::thread([this, fd = fds[0]] {
        std::string buffer;
        char chunk[4096];
        ssize_t n;
        while ((n = read(fd, chunk, sizeof(chunk))) > 0) {
            buffer.append(chunk, n);
            size_t newline;
            while ((newline = buffer.find('\n')) != std::string::npos) {
                std::lock_guard<std::mutex> lock(lines_mutex_);
                lines_.push_back(buffer.substr(0, newline));
                buffer.erase(0, newline + 1);
            }
        }
        if (!buffer.empty()) {
            std::lock_guard<std::mutex> lock(lines_mutex_);
            lines_.push_back(buffer);
        }
        close(fd);
        output_done_ = true;
    });
}

// Short "what Claude is doing" line for a tool call.
static std::string DescribeToolCall(const njson& block)
{
    const std::string name = block.value("name", "tool");
    const njson input = block.contains("input") ? block["input"] : njson::object();
    std::string target;
    if (input.contains("file_path") && input["file_path"].is_string()) target = fs::path(input["file_path"].get<std::string>()).filename().string();
    else if (input.contains("pattern") && input["pattern"].is_string()) target = input["pattern"].get<std::string>();

    if (name == "Read") return "Reading " + target;
    if (name == "Edit" || name == "MultiEdit") return "Editing " + target;
    if (name == "Write") return "Writing " + target;
    if (name == "Bash" && input.contains("command") && input["command"].is_string()) {
        const std::string cmd = input["command"].get<std::string>();
        const size_t at = cmd.find(" tool ");
        if (at != std::string::npos) {
            const std::string rest = cmd.substr(at + 6);
            if (rest.rfind("render", 0) == 0) return "Rendering the menu to compare";
            if (rest.rfind("compare", 0) == 0) return "Comparing with your screenshot";
            if (rest.rfind("inspect", 0) == 0) return "Measuring colors";
            if (rest.rfind("crop", 0) == 0) return "Zooming into the image";
            if (rest.rfind("fonts", 0) == 0) return "Looking for matching fonts";
            if (rest.rfind("import-font", 0) == 0) return "Importing a font";
        }
        return "Running: " + cmd.substr(0, 80);
    }
    return name + (target.empty() ? "" : " " + target);
}

// One line of `--output-format stream-json` output.
void Bridge::HandleCliLine(const std::string& line)
{
    if (line.empty()) return;
    const njson j = njson::parse(line, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        // not JSON, probably an error message. keep the end of it around
        stderr_tail_ += line + "\n";
        if (stderr_tail_.size() > 4000) stderr_tail_.erase(0, stderr_tail_.size() - 4000);
        return;
    }
    if (j.contains("session_id") && j["session_id"].is_string()) session_id_ = j["session_id"].get<std::string>();

    const std::string type = j.value("type", "");
    if (type == "assistant" && j.contains("message") && j["message"].contains("content")) {
        for (auto& block : j["message"]["content"]) {
            const std::string block_type = block.value("type", "");
            if (block_type == "text") {
                const std::string text = block.value("text", "");
                if (!text.empty()) messages.push_back({ChatMessage::Assistant, text});
            } else if (block_type == "tool_use") {
                messages.push_back({ChatMessage::Tool, DescribeToolCall(block)});
            }
        }
    } else if (type == "result") {
        if (j.contains("total_cost_usd") && j["total_cost_usd"].is_number()) last_cost = j["total_cost_usd"].get<double>();
        if (j.value("is_error", false)) {
            const std::string result = j.contains("result") && j["result"].is_string() ? j["result"].get<std::string>() : "Claude reported an error.";
            messages.push_back({ChatMessage::Error, result});
            const std::string lower = ToLower(result);
            if (lower.find("auth") != std::string::npos || lower.find("login") != std::string::npos || lower.find("api key") != std::string::npos)
                messages.push_back({ChatMessage::Info, "Log in once from a terminal: run the Claude CLI shown in the bridge settings and use /login."});
        }
    }
}

void Bridge::PollCli()
{
    std::vector<std::string> got;
    {
        std::lock_guard<std::mutex> lock(lines_mutex_);
        got.swap(lines_);
    }
    for (auto& line : got) HandleCliLine(line);

    if (output_done_) {
        if (reader_.joinable()) reader_.join();
        int status = 0;
        waitpid(pid_, &status, 0);
        pid_ = -1;
        Finish(WIFEXITED(status) ? WEXITSTATUS(status) : -1);
    }
}

// ---------------------------------------------------------------- inbox

void Bridge::StartInbox(const BridgeRequest& req, const std::string& prompt)
{
    const fs::path dir = fs::path(req.project_dir) / ".bridge";
    std::error_code ec;
    fs::create_directories(dir, ec);
    outbox_path_ = (dir / "outbox.jsonl").string();
    if (request_id_ == 0) {
        // ignore anything already sitting in the outbox from last time
        outbox_offset_ = fs::exists(outbox_path_, ec) ? (long long)fs::file_size(outbox_path_, ec) : 0;
        request_id_ = (long long)time(nullptr) * 1000;
    }
    request_id_++;
    const njson line = {{"id", request_id_}, {"time", (long long)time(nullptr)}, {"prompt", req.prompt}, {"full_prompt", prompt},
                        {"selected", req.selected_id}, {"selected_type", req.selected_type}, {"images", req.images}};
    std::ofstream(dir / "inbox.jsonl", std::ios::app) << line.dump() << "\n";
    busy_ = true;
    messages.push_back({ChatMessage::Info, kInboxWaitingText});
}

void Bridge::PollInbox()
{
    if (outbox_path_.empty() || Now() - last_inbox_check_ < 0.4) return;
    last_inbox_check_ = Now();

    std::ifstream f(outbox_path_, std::ios::binary);
    if (!f) return;
    f.seekg(0, std::ios::end);
    const long long size = f.tellg();
    if (size < outbox_offset_) outbox_offset_ = 0; // file got truncated
    if (size == outbox_offset_) return;
    f.seekg(outbox_offset_);
    std::string chunk(size - outbox_offset_, '\0');
    f.read(chunk.data(), chunk.size());

    const size_t last_newline = chunk.rfind('\n');
    if (last_newline == std::string::npos) return; // wait until a full line is there
    outbox_offset_ += last_newline + 1;

    for (const std::string& line : SplitLines(chunk.substr(0, last_newline))) {
        const njson j = njson::parse(line, nullptr, false);
        if (j.is_discarded() || !j.is_object()) continue;
        const long long id = j.value("id", 0LL);
        if (id != 0 && id != request_id_) continue; // id 0 = message for whoever's listening
        const std::string text = j.value("text", "");
        if (!text.empty()) messages.push_back({j.value("error", false) ? ChatMessage::Error : ChatMessage::Assistant, text});
        if (j.value("done", false)) Finish(0);
    }
}
