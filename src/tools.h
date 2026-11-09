#pragma once
#include <string>

// Runs `imgui-menu-editor tool <name> ...`. argv[0] is the tool name.
int RunTool(int argc, char** argv, const std::string& data_dir);
