// Turns a menu into plain C++ you can drop into your own project.
#pragma once
#include "model.h"
#include <string>
#include <vector>

struct ExportResult {
    bool ok = false;
    std::string message;
    std::vector<std::string> files; // relative to the export folder
};

// Just the generated header (menu_generated.h). Used by the Code panel too.
std::string GenerateMenuHeader(const json& doc);

// Writes the header, the mm_widgets runtime, fonts, images and a small example
// app into out_dir.
ExportResult ExportProject(const json& doc, const std::string& project_dir, const std::string& data_dir, const std::string& out_dir);
