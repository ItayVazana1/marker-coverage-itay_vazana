#pragma once
#include <string>
#include <vector>

// Only reference app::State here; it's defined in app.hpp
namespace app
{
    struct State;
}

namespace app::ui
{

    // ---- High-level UI ----
    void title(const std::string &t);
    void main_menu(const app::State &s);
    void help();
    void about();

    // Blocks until the user presses Enter (customizable prompt)
    void wait_for_enter(const std::string &prompt = "Press Enter to continue...");

    // Reads menu choice from stdin (returns an int, typically 0..5)
    int read_menu_choice();

    // ---- Input & validation ----
    std::string trim(std::string s);
    std::string read_line(const std::string &prompt);

    // Map "C:\..." -> "/host/c/..." when MCE_HOST_ROOT is set (for Docker Compose drive mount)
    std::string map_host_path_if_needed(const std::string &p);

    // Validate a user-supplied path, update state with absolute normalized path
    bool validate_path(app::State &s, const std::string &pathStr);

    // Open the "Input" view to set file/folder path
    void input(app::State &s);

    // Toggle debug/saveDebug flags
    void settings(app::State &s);

    // Collect .png/.jpg/.jpeg files from a file or directory path
    std::vector<std::string> collect_images(const std::string &path, bool isDir);

} // namespace app::ui
