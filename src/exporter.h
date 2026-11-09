// Generates standalone Dear ImGui C++ code from a menu document.
#pragma once
#include "model.h"
#include <string>
#include <vector>

struct ExportResult {
    bool ok = false;
    std::string message;
    std::vector<std::string> files;
};

// The generated header (menu_generated.h). `font_files` maps font id ->
// file name inside the exported assets/fonts folder.
std::string GenerateMenuHeader(const json& doc);

// Writes menu_generated.h, the mm_widgets runtime, assets and an example host
// into out_dir.
ExportResult ExportProject(const json& doc, const std::string& project_dir, const std::string& data_dir,
                           const std::string& out_dir);
