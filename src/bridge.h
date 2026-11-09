// Bridge between the editor's prompt box and Claude.
//
// Mode "cli":   runs Claude Code headless (`claude -p ... --output-format
//               stream-json`) inside the project folder. Claude edits
//               menu.json directly; the editor hot-reloads it.
// Mode "inbox": appends prompts to <project>/.bridge/inbox.jsonl and shows
//               replies from .bridge/outbox.jsonl, for a Claude session
//               that is already watching the project.
#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct ChatMsg {
    enum Role { User, Assistant, Tool, Info, Error } role;
    std::string text;
    std::vector<std::string> images; // absolute paths (user attachments)
};

struct BridgeRequest {
    std::string prompt;
    std::string project_dir;
    std::string selected_id;
    std::string selected_type;
    std::string model;
    std::string claude_path;
    std::string mode; // "cli" | "inbox"
    std::vector<std::string> images; // reference screenshots, relative to project_dir
    std::string tool_exe;            // editor binary, for `<exe> tool ...` helpers
};

class Bridge {
public:
    ~Bridge();
    void Send(const BridgeRequest& req);
    void Stop();
    void Poll();                 // call every frame
    void NewConversation();
    bool Busy() const { return busy_; }
    bool TakeFinished();         // true once after a request completes
    const std::string& SessionId() const { return session_id_; }
    void SetSessionId(const std::string& s) { session_id_ = s; }

    std::vector<ChatMsg> messages;
    double last_cost = 0;

    static std::string DetectClaude(const std::string& configured);

private:
    void StartCli(const BridgeRequest& req, const std::string& full_prompt);
    void StartInbox(const BridgeRequest& req, const std::string& full_prompt);
    void HandleLine(const std::string& line);
    void Finish(int exit_code);

    std::atomic<bool> busy_{false};
    bool finished_ = false;
    std::string session_id_;

    // cli
    int pid_ = -1;
    int out_fd_ = -1;
    std::thread reader_;
    std::mutex mu_;
    std::vector<std::string> lines_;
    std::atomic<bool> eof_{false};
    std::string stderr_tail_;

    // inbox
    std::string outbox_path_;
    long long outbox_offset_ = 0;
    long long inbox_id_ = 0;
    double inbox_poll_ = 0;
};
