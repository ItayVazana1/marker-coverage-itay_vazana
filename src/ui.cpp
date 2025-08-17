#include "mce/ui.hpp"
#include "mce/ansi.hpp"
#include <filesystem>
#include <iostream>
#include <limits>
#include <cctype> // for std::isdigit

namespace fs = std::filesystem;

namespace app::ui
{

    static const char *kMenu = R"MENU(
Choose an option:

  1) Input: Set image or folder path
  2) Settings: Toggle debug / save-debug
  3) Help: How to use
  4) About
  5) Run: Detect & report coverage
  0) Exit
)MENU";

    void title(const std::string &t)
    {
        mce::ansi::clear_screen();
        std::cout << mce::ansi::title << mce::ansi::bold << t << mce::ansi::reset << "\n";
        std::cout << mce::ansi::muted << std::string(t.size(), '=') << mce::ansi::reset << "\n\n";
    }

    void main_menu(const State &s)
    {
        title("Marker Coverage Estimator (TUI)");
        std::cout << kMenu << "\n";
        std::cout << mce::ansi::muted
                  << "Current path: " << (s.hasValidPath ? s.inputPath : std::string("(none)"))
                  << mce::ansi::reset << "\n";
        std::cout << mce::ansi::muted
                  << "Debug: " << (s.debug ? "ON" : "OFF")
                  << ", Save debug: " << (s.saveDebug ? "ON" : "OFF")
                  << mce::ansi::reset << "\n\n";
    }

    void help()
    {
        title("Help");
        std::cout
            << "- Provide a file or folder with PNG/JPEG images (option 1).\n"
            << "- Toggle debug overlays/logging in Settings.\n"
            << "- Run detection (option 5) to print '<file> <percent>%'.\n\n";
        wait_for_enter();
    }

    void about()
    {
        title("About");
        std::cout
            << "Decorated TUI on top of the required CLI logic. Uses OpenCV for detection.\n\n";
        wait_for_enter();
    }

    void wait_for_enter(const std::string &prompt)
    {
        std::cout << mce::ansi::muted << prompt << mce::ansi::reset;
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    }

    std::string trim(std::string s)
    {
        const auto sp = [](unsigned char c)
        {
            return c == ' ' || c == '\t' || c == '\n' || c == '\r';
        };
        size_t a = 0;
        while (a < s.size() && sp((unsigned char)s[a]))
            ++a;
        size_t b = s.size();
        while (b > a && sp((unsigned char)s[b - 1]))
            --b;
        return s.substr(a, b - a);
    }

    std::string read_line(const std::string &prompt)
    {
        std::cout << mce::ansi::info << prompt << mce::ansi::reset;
        std::string s;
        std::getline(std::cin, s);
        return s;
    }

    bool validate_path(State &s, const std::string &pathStr)
    {
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
        std::cout << mce::ansi::muted
                  << "Examples:\n"
                     "  C:\\Users\\You\\Pictures\\photo.jpg\n"
                     "  /home/you/images\n"
                  << mce::ansi::reset << "\n";

        const std::string path = read_line("Path> ");
        if (!std::cin.good())
            return;

        if (validate_path(s, path))
        {
            std::cout << mce::ansi::ok << "[OK] Valid path: " << s.inputPath << mce::ansi::reset << "\n";
            std::cout << (s.isDirectory ? "Detected: directory\n" : "Detected: file\n");
        }
        else
        {
            std::cout << mce::ansi::err << "[X] Invalid path. Please try again." << mce::ansi::reset << "\n";
        }
        std::cout << "\n";
        wait_for_enter();
    }

    void settings(State &s)
    {
        title("Settings");
        std::cout
            << "Toggle options (type number):\n"
            << "  1) Debug logs: " << (s.debug ? "ON" : "OFF") << "\n"
            << "  2) Save debug overlays: " << (s.saveDebug ? "ON" : "OFF") << "\n"
            << "  0) Back\n\n";
        std::cout << "Select: ";
        std::string line;
        std::getline(std::cin, line);
        if (line == "1")
            s.debug = !s.debug;
        else if (line == "2")
            s.saveDebug = !s.saveDebug;
    }

    int read_menu_choice()
    {
        std::cout << "Select (0-5): ";
        std::string line;

        // If user hits Ctrl+D / EOF, treat as Exit (0)
        if (!std::getline(std::cin, line))
            return 0;

        line = trim(line);

        if (line.size() == 1 && std::isdigit(static_cast<unsigned char>(line[0])))
        {
            return line[0] - '0'; // '0'..'5' -> 0..5
        }

        // anything else -> invalid (the app loop will print "Invalid choice.")
        return -1;
    }

    std::vector<std::string> collect_images(const std::string &path, bool isDir)
    {
        std::vector<std::string> out;
        if (!isDir)
        {
            out.push_back(path);
            return out;
        }

        for (auto &p : fs::recursive_directory_iterator(path))
        {
            if (!p.is_regular_file())
                continue;
            auto ext = p.path().extension().string();
            for (auto &c : ext)
                c = (char)tolower((unsigned char)c);
            if (ext == ".png" || ext == ".jpg" || ext == ".jpeg")
                out.push_back(p.path().string());
        }
        return out;
    }

} // namespace app::ui
