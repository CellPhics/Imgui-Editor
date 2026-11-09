// Talks to Claude for the prompt box.
//
// Two ways to do it:
//  - CLI (default): runs Claude Code headless (`claude -p ...`) inside the
//    project folder and streams its JSON output. Claude edits menu.json and the
//    editor hot-reloads it.
//  - Inbox: appends the prompt to .bridge/inbox.jsonl and waits for replies in
//    .bridge/outbox.jsonl. For when you already have a Claude session open on
//    the project folder and want it to do the work.
#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct ChatMessage {
    enum Role { User, Assistant, Tool, Info, Error } role;
    std::string text;
    std::vector<std::string> images; // attached screenshots (absolute paths)
};

struct BridgeRequest {
    std::string prompt;
    std::string project_dir;
    std::string selected_id, selected_type; // so "make this red" knows what "this" is
    std::string model;
    std::string claude_path;                // empty = find it ourselves
    bool use_inbox = false;
    std::vector<std::string> images;        // relative to project_dir
    std::string editor_exe;                 // lets Claude run `<editor> tool ...`
};

class Bridge {
public:
    ~Bridge();
    void Send(const BridgeRequest& req);
    void Stop();
    void Poll(); // call once per frame
    void NewConversation();
    void Reset(); // forget everything (used when switching projects)

    bool Busy() const { return busy_; }
    bool TakeFinished(); // true once right after a request completes
    const std::string& SessionId() const { return session_id_; }

    std::vector<ChatMessage> messages;
    double last_cost = 0;

    // Finds the Claude Code CLI: the configured path, then PATH, then the usual
    // install spots (including the copy that ships with the desktop app).
    static std::string FindClaude(const std::string& configured);

private:
    void StartCli(const BridgeRequest& req, const std::string& prompt);
    void StartInbox(const BridgeRequest& req, const std::string& prompt);
    void HandleCliLine(const std::string& line);
    void PollCli();
    void PollInbox();
    void Finish(int exit_code);

    std::atomic<bool> busy_{false};
    bool finished_ = false;
    std::string session_id_;

    // cli: child process + a thread reading its output
    int pid_ = -1;
    std::thread reader_;
    std::mutex lines_mutex_;
    std::vector<std::string> lines_;
    std::atomic<bool> output_done_{false};
    std::string stderr_tail_; // non-JSON output, shown if the CLI fails

    // inbox
    std::string outbox_path_;
    long long outbox_offset_ = 0;
    long long request_id_ = 0;
    double last_inbox_check_ = 0;
};
