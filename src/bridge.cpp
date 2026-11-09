#include "bridge.h"
#include "assets.h"
#include "nlohmann/json.hpp"
#include "stb/stb_image.h"
#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;
namespace fs = std::filesystem;
using njson = nlohmann::json;

static const char* kSystemPrompt =
    "You are the design assistant built into ImGui Menu Editor, a visual editor for Dear ImGui mod menus. "
    "The user types requests in the editor's prompt box. The whole menu (windows, tabs, widgets, themes, fonts, images) "
    "lives in ./menu.json in the current directory, and the editor hot-reloads that file the moment you save it. "
    "Read ./CLAUDE.md for the JSON schema before your first edit. Make exactly the change requested, preserve everything else, "
    "keep the JSON valid and every id unique. Prefer targeted Edit calls over rewriting the whole file. "
    "Images and fonts must live under ./assets/ and be registered in menu.json. "
    "Never delete the user's existing themes: when you need new colors, add a new theme (or edit the active one) and make it active. "
    "Finish with one or two short sentences describing what you changed.";

static double Now()
{
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

static bool IsExec(const std::string& p)
{
    return !p.empty() && access(p.c_str(), X_OK) == 0 && fs::is_regular_file(p);
}

static std::vector<int> VersionKey(const std::string& s)
{
    std::vector<int> v;
    int cur = -1;
    for (char c : s) {
        if (isdigit((unsigned char)c)) cur = (cur < 0 ? 0 : cur * 10) + (c - '0');
        else if (cur >= 0) { v.push_back(cur); cur = -1; }
    }
    if (cur >= 0) v.push_back(cur);
    return v;
}

static std::string NewestMatch(const fs::path& dir, const std::string& prefix, const std::string& rel)
{
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return "";
    std::string best, best_name;
    for (auto& e : fs::directory_iterator(dir, ec)) {
        std::string name = e.path().filename().string();
        if (name.rfind(prefix, 0) != 0) continue;
        std::string cand = (e.path() / rel).string();
        if (!IsExec(cand)) continue;
        if (best.empty() || VersionKey(name) > VersionKey(best_name)) { best = cand; best_name = name; }
    }
    return best;
}

std::string Bridge::DetectClaude(const std::string& configured)
{
    if (IsExec(configured)) return configured;
    if (const char* path = getenv("PATH")) {
        std::string p = path;
        size_t start = 0;
        while (start <= p.size()) {
            size_t end = p.find(':', start);
            if (end == std::string::npos) end = p.size();
            std::string c = p.substr(start, end - start) + "/claude";
            if (IsExec(c)) return c;
            start = end + 1;
        }
    }
    const char* h = getenv("HOME");
    fs::path home = h ? h : "";
    for (auto c : {home / ".local/bin/claude", home / ".claude/local/claude", home / ".npm-global/bin/claude", fs::path("/usr/local/bin/claude")})
        if (IsExec(c.string())) return c.string();
    std::string r = NewestMatch(home / ".config/Claude/claude-code", "", "claude");
    if (!r.empty()) return r;
    for (auto ext : {".vscode/extensions", ".cursor/extensions", ".antigravity-ide/extensions", ".windsurf/extensions"}) {
        r = NewestMatch(home / ext, "anthropic.claude-code-", "resources/native-binary/claude");
        if (!r.empty()) return r;
    }
    return "";
}

Bridge::~Bridge()
{
    Stop();
    if (reader_.joinable()) reader_.join();
}

void Bridge::NewConversation()
{
    session_id_.clear();
    messages.push_back({ChatMsg::Info, "Started a new conversation."});
}

// How the helper command is spelled, both in the prompt and in the permission rule.
static std::string ToolCmd(const std::string& exe)
{
    return (exe.find(' ') != std::string::npos ? "\"" + exe + "\"" : exe) + " tool";
}

static std::string ToolsPrompt(const std::string& exe)
{
    if (exe.empty()) return "";
    const std::string t = ToolCmd(exe);
    return "\n\nHelper commands you may run with Bash (run them from the project folder):\n"
           "- " + t + " render [--show NODE_ID]   renders the menu exactly as the editor/export draws it to .bridge/render.png "
           "and prints every widget's rectangle in that image. Read the PNG to see your result. --show reveals the tab containing NODE_ID.\n"
           "- " + t + " compare REFERENCE .bridge/render.png   similarity score, size mismatch, and the most different areas with "
           "reference vs render colors, plus .bridge/diff.png (differences in red).\n"
           "- " + t + " inspect IMAGE [--top N] [x,y ...] [x,y,w,h ...]   image size, dominant colors, exact pixel/region colors.\n"
           "- " + t + " crop IMAGE x y w h OUT.png [--scale N] [--key auto|#RRGGBB] [--tolerance N]   cut out / zoom into part of an image. "
           "Use it to cut logos and icons into assets/images/; --key auto makes the background (the region's top-left color) transparent.\n"
           "- " + t + " fonts [filter]   list installed fonts;  " + t + " import-font PATH   copy one into assets/fonts.\n"
           "Run each helper as its own Bash call: no pipes, `;`, `&&`, variables or scripts (they will be blocked). "
           "Change menu.json only with the Edit/Write tools.\n"
           "When matching a reference image, iterate: edit menu.json -> render -> compare -> Read the diff/render -> fix the biggest "
           "differences first (size, layout, then colors, then fonts and small offsets). Never stop while the comparison is "
           "still improving: do at least 6 passes and keep going until similarity is 97%+ or two passes in a row gain under "
           "0.2 points. Use the schema's look/style options (CLAUDE.md) instead of settling for defaults, and crop artwork the "
           "built-in icons can't match (tab icons, logos, weapon images) from the reference into assets/images. "
           "Then summarize the final similarity and what still differs.";
}

static std::string BuildPrompt(const BridgeRequest& r)
{
    std::string p = r.prompt;
    if (!r.images.empty()) {
        p += "\n\n[Attached reference images — open each one with the Read tool]\n";
        for (auto& im : r.images) {
            int w = 0, h = 0, n = 0;
            std::string dims;
            if (stbi_info((fs::path(r.project_dir) / im).string().c_str(), &w, &h, &n)) dims = " (" + std::to_string(w) + "x" + std::to_string(h) + " px)";
            p += "- ./" + im + dims + "\n";
        }
        p += "Follow the \"Recreating a screenshot\" workflow in CLAUDE.md: match it as exactly as possible "
             "(layout, sizes in pixels, colors sampled with `tool inspect`, fonts, icons, text), and verify with `tool render`.\n";
    }
    p += "\n\n[Editor context]\n- Menu file: ./menu.json (hot-reloaded by the editor on save)\n";
    if (!r.selected_id.empty())
        p += "- Currently selected in the editor: node id \"" + r.selected_id + "\" (type " + r.selected_type +
             "). Words like \"this\", \"it\" or \"selected\" refer to it.\n";
    else
        p += "- Nothing is selected in the editor.\n";
    return p;
}

void Bridge::Send(const BridgeRequest& req)
{
    if (busy_) return;
    ChatMsg um{ChatMsg::User, req.prompt, {}};
    for (auto& im : req.images) um.images.push_back((fs::path(req.project_dir) / im).string());
    messages.push_back(um);
    finished_ = false;
    std::string full = BuildPrompt(req);
    if (req.mode == "inbox") StartInbox(req, full);
    else StartCli(req, full);
}

// ---------------------------------------------------------------- cli

void Bridge::StartCli(const BridgeRequest& req, const std::string& full_prompt)
{
    std::string exe = DetectClaude(req.claude_path);
    if (exe.empty()) {
        messages.push_back({ChatMsg::Error, "Couldn't find the Claude Code CLI. Install it (https://claude.com/claude-code) "
                                            "or set its path in Bridge settings."});
        return;
    }
    if (reader_.joinable()) reader_.join();

    std::vector<std::string> args = {exe, "-p", full_prompt, "--output-format", "stream-json", "--verbose",
                                     "--permission-mode", "acceptEdits",
                                     "--append-system-prompt", std::string(kSystemPrompt) + ToolsPrompt(req.tool_exe),
                                     "--allowedTools", "Read", "Edit", "Write", "Glob", "Grep"};
    if (!req.tool_exe.empty()) args.push_back("Bash(" + ToolCmd(req.tool_exe) + ":*)");
    if (!req.model.empty()) { args.push_back("--model"); args.push_back(req.model); }
    if (!session_id_.empty()) { args.push_back("--resume"); args.push_back(session_id_); }

    // Child environment: drop variables that would make the CLI think it is
    // nested inside another Claude Code session.
    const bool nested = getenv("CLAUDECODE") != nullptr;
    std::vector<std::string> env;
    for (char** e = environ; *e; e++) {
        std::string kv = *e;
        std::string k = kv.substr(0, kv.find('='));
        if (k == "CLAUDECODE" || k.rfind("CLAUDE_CODE_", 0) == 0 || k == "CLAUDE_PID" || k == "CLAUDE_AGENT_SDK_VERSION" ||
            k == "CLAUDE_EFFORT" || k.rfind("CLAUDE_PREVIEW", 0) == 0)
            continue;
        if (nested && k == "ANTHROPIC_BASE_URL") continue;
        env.push_back(kv);
    }
    std::vector<char*> argv, envp;
    for (auto& a : args) argv.push_back(a.data());
    argv.push_back(nullptr);
    for (auto& e : env) envp.push_back(e.data());
    envp.push_back(nullptr);

    int fds[2];
    if (pipe(fds) != 0) { messages.push_back({ChatMsg::Error, "pipe() failed"}); return; }
    const std::string dir = req.project_dir;
    pid_t pid = fork();
    if (pid < 0) { close(fds[0]); close(fds[1]); messages.push_back({ChatMsg::Error, "fork() failed"}); return; }
    if (pid == 0) {
        if (chdir(dir.c_str()) != 0) _exit(126);
        int devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0) dup2(devnull, 0);
        dup2(fds[1], 1);
        dup2(fds[1], 2);
        close(fds[0]);
        close(fds[1]);
        setpgid(0, 0);
        execve(argv[0], argv.data(), envp.data());
        _exit(127);
    }
    close(fds[1]);
    pid_ = pid;
    out_fd_ = fds[0];
    eof_ = false;
    busy_ = true;
    stderr_tail_.clear();
    messages.push_back({ChatMsg::Info, "Claude is working..."});

    reader_ = std::thread([this, fd = out_fd_] {
        std::string buf;
        char tmp[4096];
        for (;;) {
            ssize_t n = read(fd, tmp, sizeof(tmp));
            if (n <= 0) break;
            buf.append(tmp, n);
            size_t pos;
            while ((pos = buf.find('\n')) != std::string::npos) {
                std::lock_guard<std::mutex> lk(mu_);
                lines_.push_back(buf.substr(0, pos));
                buf.erase(0, pos + 1);
            }
        }
        if (!buf.empty()) { std::lock_guard<std::mutex> lk(mu_); lines_.push_back(buf); }
        close(fd);
        eof_ = true;
    });
}

static std::string ToolSummary(const njson& block)
{
    std::string name = block.value("name", "tool");
    const njson& in = block.contains("input") ? block["input"] : njson::object();
    std::string target;
    if (in.contains("file_path") && in["file_path"].is_string()) target = fs::path(in["file_path"].get<std::string>()).filename().string();
    else if (in.contains("pattern") && in["pattern"].is_string()) target = in["pattern"].get<std::string>();
    if (name == "Read") return "Reading " + target;
    if (name == "Edit" || name == "MultiEdit") return "Editing " + target;
    if (name == "Write") return "Writing " + target;
    if (name == "Bash" && in.contains("command") && in["command"].is_string()) {
        std::string c = in["command"].get<std::string>();
        size_t k = c.find(" tool ");
        if (k != std::string::npos) {
            std::string rest = c.substr(k + 6);
            if (rest.rfind("render", 0) == 0) return "Rendering the menu to compare";
            if (rest.rfind("inspect", 0) == 0) return "Measuring colors: " + rest.substr(8);
            if (rest.rfind("crop", 0) == 0) return "Zooming into the image";
            if (rest.rfind("compare", 0) == 0) return "Comparing with your screenshot";
            if (rest.rfind("fonts", 0) == 0) return "Looking for matching fonts";
            if (rest.rfind("import-font", 0) == 0) return "Importing a font";
        }
        return "Running: " + c.substr(0, 80);
    }
    return name + (target.empty() ? "" : " " + target);
}

void Bridge::HandleLine(const std::string& line)
{
    if (line.empty()) return;
    njson j = njson::parse(line, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        stderr_tail_ += line + "\n";
        if (stderr_tail_.size() > 4000) stderr_tail_.erase(0, stderr_tail_.size() - 4000);
        return;
    }
    std::string type = j.value("type", "");
    if (j.contains("session_id") && j["session_id"].is_string()) session_id_ = j["session_id"].get<std::string>();
    if (type == "assistant" && j.contains("message") && j["message"].contains("content")) {
        for (auto& b : j["message"]["content"]) {
            std::string bt = b.value("type", "");
            if (bt == "text") {
                std::string t = b.value("text", "");
                if (!t.empty()) messages.push_back({ChatMsg::Assistant, t});
            } else if (bt == "tool_use") {
                messages.push_back({ChatMsg::Tool, ToolSummary(b)});
            }
        }
    } else if (type == "result") {
        if (j.contains("total_cost_usd") && j["total_cost_usd"].is_number()) last_cost = j["total_cost_usd"].get<double>();
        if (j.value("is_error", false)) {
            std::string r = j.contains("result") && j["result"].is_string() ? j["result"].get<std::string>() : "Claude reported an error.";
            messages.push_back({ChatMsg::Error, r});
            std::string lower = r;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (lower.find("auth") != std::string::npos || lower.find("login") != std::string::npos || lower.find("api key") != std::string::npos)
                messages.push_back({ChatMsg::Info, "Log in once from a terminal: run the Claude CLI shown in Bridge settings and use /login."});
        }
    }
}

void Bridge::Finish(int code)
{
    busy_ = false;
    finished_ = true;
    // Drop the "working" placeholder.
    for (auto it = messages.rbegin(); it != messages.rend(); ++it)
        if (it->role == ChatMsg::Info && it->text == "Claude is working...") { messages.erase(std::next(it).base()); break; }
    if (code != 0 && code != -1) {
        std::string msg = "Claude exited with code " + std::to_string(code) + ".";
        if (code == 127) msg += " The CLI could not be started.";
        if (!stderr_tail_.empty()) msg += "\n" + stderr_tail_;
        messages.push_back({ChatMsg::Error, msg});
    }
}

void Bridge::Stop()
{
    if (pid_ > 0) {
        kill(-pid_, SIGTERM);
        kill(pid_, SIGTERM);
    }
    if (busy_ && pid_ <= 0) { // inbox mode: stop waiting
        busy_ = false;
        messages.push_back({ChatMsg::Info, "Stopped waiting for a reply."});
    }
}

bool Bridge::TakeFinished()
{
    bool f = finished_;
    finished_ = false;
    return f;
}

// ---------------------------------------------------------------- inbox

void Bridge::StartInbox(const BridgeRequest& req, const std::string& full_prompt)
{
    fs::path dir = fs::path(req.project_dir) / ".bridge";
    std::error_code ec;
    fs::create_directories(dir, ec);
    outbox_path_ = (dir / "outbox.jsonl").string();
    if (inbox_id_ == 0) {
        // Start after anything already in the outbox.
        outbox_offset_ = fs::exists(outbox_path_, ec) ? (long long)fs::file_size(outbox_path_, ec) : 0;
        inbox_id_ = (long long)time(nullptr) * 1000;
    }
    inbox_id_++;
    njson line = {{"id", inbox_id_}, {"time", (long long)time(nullptr)}, {"prompt", req.prompt}, {"full_prompt", full_prompt},
                  {"selected", req.selected_id}, {"selected_type", req.selected_type}, {"images", req.images}};
    std::ofstream f(dir / "inbox.jsonl", std::ios::app);
    f << line.dump() << "\n";
    busy_ = true;
    messages.push_back({ChatMsg::Info, "Sent to .bridge/inbox.jsonl — waiting for the watching Claude session..."});
}

void Bridge::Poll()
{
    if (pid_ > 0) {
        std::vector<std::string> got;
        {
            std::lock_guard<std::mutex> lk(mu_);
            got.swap(lines_);
        }
        for (auto& l : got) HandleLine(l);
        if (eof_) {
            if (reader_.joinable()) reader_.join();
            int status = 0;
            waitpid(pid_, &status, 0);
            pid_ = -1;
            int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            Finish(code);
        }
        return;
    }
    if (!busy_ || outbox_path_.empty()) return;
    if (Now() - inbox_poll_ < 0.4) return;
    inbox_poll_ = Now();
    std::ifstream f(outbox_path_, std::ios::binary);
    if (!f) return;
    f.seekg(0, std::ios::end);
    long long size = f.tellg();
    if (size < outbox_offset_) outbox_offset_ = 0; // truncated
    if (size == outbox_offset_) return;
    f.seekg(outbox_offset_);
    std::string chunk(size - outbox_offset_, '\0');
    f.read(chunk.data(), chunk.size());
    size_t last_nl = chunk.rfind('\n');
    if (last_nl == std::string::npos) return; // wait for a complete line
    outbox_offset_ += last_nl + 1;
    size_t start = 0;
    while (start <= last_nl) {
        size_t end = chunk.find('\n', start);
        std::string l = chunk.substr(start, end - start);
        start = end + 1;
        njson j = njson::parse(l, nullptr, false);
        if (j.is_discarded() || !j.is_object()) continue;
        long long id = j.value("id", 0LL);
        if (id != 0 && id != inbox_id_) continue;
        std::string text = j.value("text", "");
        if (!text.empty()) messages.push_back({j.value("error", false) ? ChatMsg::Error : ChatMsg::Assistant, text});
        if (j.value("done", false)) {
            for (auto it = messages.begin(); it != messages.end(); ++it)
                if (it->role == ChatMsg::Info && it->text.rfind("Sent to .bridge", 0) == 0) { messages.erase(it); break; }
            busy_ = false;
            finished_ = true;
        }
    }
}
