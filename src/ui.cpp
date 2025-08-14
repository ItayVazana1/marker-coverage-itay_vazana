#include "ui.hpp"
#include "ansi.hpp"
#include <filesystem>
#include <iostream>
#include <limits>

namespace fs = std::filesystem;

namespace app::ui
{

    void title(const std::string &t)
    {
        ansi::clear_screen();
        std::cout << ansi::title << "\x1b[1m" << t << ansi::reset << "\n";
        std::cout << ansi::muted << std::string(t.size(), '=') << ansi::reset << "\n\n";
    }

    void main_menu(const State &s)
    {
        title("Image Flow Console");
        std::cout << "Choose an option:\n\n";
        std::cout << "  " << ansi::info << "1)" << ansi::reset << " Input: Set image or folder path\n";
        std::cout << "  " << ansi::info << "2)" << ansi::reset << " Process: Run demo pipeline\n";
        std::cout << "  " << ansi::info << "3)" << ansi::reset << " Help: How to use\n";
        std::cout << "  " << ansi::info << "4)" << ansi::reset << " About\n";
        std::cout << "  " << ansi::err << "0)" << ansi::reset << " Exit\n\n";

        std::cout << ansi::muted << "Current path: "
                  << (s.hasValidPath ? s.inputPath : std::string("(none)"))
                  << ansi::reset << "\n\n";
    }

    void help()
    {
        title("Help");
        std::cout
            << "- Use option " << ansi::info << "1" << ansi::reset << " to provide a file or a folder.\n"
            << "- Then run option " << ansi::info << "2" << ansi::reset << " to start processing.\n"
            << "- The progress screen shows a live bar and the current step.\n\n"
            << ansi::muted << "Tips:\n"
            << "  • Drag-and-drop a path into this window on some terminals.\n"
            << "  • ANSI colors are enabled automatically on Windows 10+.\n"
            << ansi::reset << "\n";
        wait_for_enter();
    }

    void about()
    {
        title("About");
        std::cout
            << ansi::bold << "Image Flow Console" << ansi::reset << " (namespaced draft)\n"
            << "C++17, portable.\n\n"
            << "Contacts & Info:\n"
            << "  • Developer: You :)\n"
            << "  • GitHub   : <your-repo-here>\n"
            << "  • License  : MIT (suggested)\n\n";
        wait_for_enter();
    }

    void wait_for_enter(const std::string &prompt)
    {
        std::cout << ansi::muted << prompt << ansi::reset;
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    }

    std::string trim(std::string s)
    {
        const auto issp = [](unsigned char c)
        { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
        size_t a = 0;
        while (a < s.size() && issp((unsigned char)s[a]))
            ++a;
        size_t b = s.size();
        while (b > a && issp((unsigned char)s[b - 1]))
            --b;
        return s.substr(a, b - a);
    }

    std::string read_line(const std::string &prompt)
    {
        std::cout << ansi::info << prompt << ansi::reset;
        std::string s;
        std::getline(std::cin, s);
        return s;
    }

    bool validate_path(State &s, const std::string &pathStr)
    {
        using std::cout;
        using std::string;
        const fs::path p = trim(pathStr);
        if (p.empty() || !fs::exists(p))
        {
            s.hasValidPath = false;
            return false;
        }
        s.inputPath = fs::absolute(p).string();
        s.isDirectory = fs::is_directory(p);
        s.hasValidPath = true;
        return true;
    }

    void input(State &s)
    {
        title("Input");
        std::cout << "Provide a path to an image file or a folder.\n\n";
        std::cout << ansi::muted << "Examples:\n"
                  << "  C:\\Users\\You\\Pictures\\photo.jpg\n"
                  << "  /home/you/images\n"
                  << ansi::reset << "\n";

        const std::string path = read_line("Path> ");
        if (!std::cin.good())
            return; // EOF

        if (validate_path(s, path))
        {
            std::cout << ansi::ok << "\xE2\x9C\x93 Valid path: " << s.inputPath << ansi::reset << "\n";
            std::cout << (s.isDirectory ? "Detected: directory\n" : "Detected: file\n");
        }
        else
        {
            std::cout << ansi::err << "\xE2\x9C\x97 Invalid path. Please try again." << ansi::reset << "\n";
        }
        std::cout << "\n";
        wait_for_enter();
    }

    int read_menu_choice()
    {
        std::cout << "Select (0-4): ";
        std::string line;
        std::getline(std::cin, line);
        if (!std::cin.good())
            return 0; // EOF -> exit gracefully
        line = trim(line);
        if (line.size() == 1 && std::isdigit((unsigned char)line[0]))
            return line[0] - '0';
        return -1;
    }

} // namespace app::ui