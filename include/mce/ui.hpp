#pragma once
#include <string>
#include <vector>
#include "mce/app.hpp"

namespace app::ui
{
    void title(const std::string &t);
    void main_menu(const State &s);
    void help();
    void about();
    void input(State &s);
    void settings(State &s); // NEW: toggle debug/save-debug
    void wait_for_enter(const std::string &prompt = "Press ENTER to continue...");
    int read_menu_choice();
    std::string read_line(const std::string &prompt);
    std::string trim(std::string s);
    bool validate_path(State &s, const std::string &pathStr);
    std::vector<std::string> collect_images(const std::string &path, bool isDir);
}