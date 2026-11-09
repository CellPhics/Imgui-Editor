// Small helpers that don't belong to anything in particular:
// files, running shell commands, strings, time.
#pragma once
#include <initializer_list>
#include <string>
#include <vector>

// ---- files
bool FileExists(const std::string& path);
long long FileMTime(const std::string& path); // nanoseconds, 0 if missing
std::string ReadFile(const std::string& path, bool* ok = nullptr);
// Writes to a temp file then renames it, so readers never see half a file.
bool WriteFileAtomic(const std::string& path, const std::string& data);
std::string ExpandHome(std::string path); // "~/x" -> "/home/you/x", trims trailing whitespace
std::string HomeDir();

// ---- shell
// Runs a command through the shell and returns its stdout (binary safe).
std::string RunCommand(const std::string& cmd, int* exit_status = nullptr);
bool HaveCommand(const char* name);
std::string ShellQuote(const std::string& s);
bool OnWayland();

// ---- strings
std::string ToLower(std::string s);
bool ContainsNoCase(const std::string& haystack, const std::string& needle);
bool HasExtension(const std::string& path, std::initializer_list<const char*> exts);
bool IsImageFile(const std::string& path);
std::vector<std::string> SplitLines(const std::string& s);
std::string Utf8(unsigned codepoint);

// ---- time
double Now(); // seconds, monotonic
