#include "mce/ui.hpp"
#include "mce/app.hpp" // <-- add this include to get full definition of app::State
#include "mce/ansi.hpp"

#include <filesystem>
#include <iostream>
#include <limits>
#include <cctype>
#include <cstdlib> // std::getenv
#include <regex>

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

    std::string trim(std::string s)
    {
        const auto sp = [](unsigned char c)
        { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
        std::size_t a = 0;
        while (a < s.size() && sp((unsigned char)s[a]))
            ++a;
        std::size_t b = s.size();
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

    // Map "C:\Users\..." -> "/host/c/Users/..." if MCE_HOST_ROOT and drive bind are set in compose.yml
    std::string map_host_path_if_needed(const std::string &in)
    {
        const char *root = std::getenv("MCE_HOST_ROOT"); // e.g. "/host"
        if (!root || !*root)
            return in;

        static const std::regex winDrive(R"(^([A-Za-z]):[\\/](.*))");
        std::smatch m;
        if (std::regex_match(in, m, winDrive) && m.size() == 3)
        {
            std::string drive = m[1].str();
            std::string rest = m[2].str();
            for (auto &ch : rest)
                if (ch == '\\')
                    ch = '/';
            const char lower = (char)std::tolower((unsigned char)drive[0]);
            return std::string(root) + "/" + lower + "/" + rest; // "/host/c/Users/..."
        }
        return in;
    }

    void title(const std::string &t)
    {
        mce::ansi::clear_screen();
        std::cout << mce::ansi::title << mce::ansi::bold << t << mce::ansi::reset << "\n";
        std::cout << mce::ansi::muted << std::string(t.size(), '=') << mce::ansi::reset << "\n\n";
    }

    void main_menu(const app::State &s)
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
            << "Decorated TUI on top of the required CLI logic.\n"
            << "Uses OpenCV for marker detection and coverage estimation.\n\n";
        wait_for_enter();
    }

    void wait_for_enter(const std::string &prompt)
    {
        std::cout << mce::ansi::muted << prompt << mce::ansi::reset;
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    }

    int read_menu_choice()
    {
        std::cout << "Select (0-5): ";
        std::string line;
        std::getline(std::cin, line);
        line = trim(line);
        if (line.empty())
            return -1;
        bool ok = true;
        for (char ch : line)
            if (!std::isdigit((unsigned char)ch) && ch != '-' && ch != '+')
            {
                ok = false;
                break;
            }
        if (!ok)
            return -1;
        try
        {
            return std::stoi(line);
        }
        catch (...)
        {
            return -1;
        }
    }

    bool validate_path(app::State &s, const std::string &pathStr)
    {
        const std::string mapped = map_host_path_if_needed(trim(pathStr));
        const fs::path p = mapped;

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

    void input(app::State &s)
    {
        title("Input");
        std::cout << "Provide a path to an image file or a folder.\n\n";
        std::cout << mce::ansi::muted
                  << "Examples:\n"
                     "  C:\\Users\\You\\Pictures\\photo.jpg\n"
                     "  /home/you/images\n"
                     "  /host/c/Users/You/Pictures   (when running in Docker Compose)\n"
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

    void settings(app::State &s)
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
        line = trim(line);
        if (line == "1")
            s.debug = !s.debug;
        else if (line == "2")
            s.saveDebug = !s.saveDebug;
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
                c = (char)std::tolower((unsigned char)c);
            if (ext == ".png" || ext == ".jpg" || ext == ".jpeg")
                out.push_back(p.path().string());
        }
        return out;
    }

} // namespace app::ui
