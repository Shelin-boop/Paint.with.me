#pragma once

#include <memory>
#include <vector>

#include "tools/tool.h"

// Everything the app knows about each tool, in one place. Adding a tool means:
// a ToolType value, the tool class, an entry here, its icon in toolicons.cpp,
// which options it shows in tooloptionspanel.cpp, and a test.
struct ToolInfo {
    ToolType type;
    const char *key;          // stable name for settings ("paintbrush"); never change it
    const char *name;         // shown in the Tools panel and the tool list
    const char *shortcut;     // default key (Settings > Keyboard can change it);
                              // tools sharing a key cycle in list order
    int row;                  // slot in the Tools panel's two columns
    int col;
    const char *description;  // second line of the tooltip
    const char *hint;         // status bar text while the tool is active
};

// Every tool in Paint.NET's documented order, which the tool list, the
// one-row Tools panel and shortcut cycling follow. Not in Paint.NET: the
// Perspective, Smudge and Blur Brush tools, last.
const std::vector<ToolInfo> &toolInfos();
const ToolInfo &toolInfo(ToolType type);
std::unique_ptr<Tool> createTool(ToolType type);
// Tools that put paint on the image: the ones "Last Used Painting Tool"
// brings back (Paintbrush, Pencil, Clone Stamp, Recolor, Smudge, Blur Brush).
bool isPaintingTool(ToolType type);
