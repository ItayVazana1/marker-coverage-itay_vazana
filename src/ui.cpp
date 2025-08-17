#include "mce/ui.hpp"
#include "mce/app.hpp" // <-- add this include to get full definition of app::State
#include "mce/ansi.hpp"

#include <filesystem>
#include <iostream>
#include <limits>
#include <cctype>
#include <cstdlib> // std::getenv
#include <regex>

// --- Author / contact (from CV) ---
static constexpr const char *kAuthorName = "Itay Vazana";
static constexpr const char *kAuthorEmail = "itay.vazana.b@gmail.com";
static constexpr const char *kAuthorLinkedIn = "linkedin.com/in/itayvazana";
static constexpr const char *kAuthorGitHub = "github.com/ItayVazana1";
static constexpr const char *kAuthorLocation = "Ashdod, Israel";

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
            << mce::ansi::bold << "What this app does" << mce::ansi::reset << "\n"
            << "• Detects the rectangular marker in each image and estimates its coverage (% of image area).\n"
            << "• Works on a single file or an entire folder (PNG/JPG/JPEG).\n"
            << "• Saves a CSV report and, if enabled, debug overlays.\n\n"

            << mce::ansi::bold << "Quick start" << mce::ansi::reset << "\n"
            << "1) " << mce::ansi::info << "Input" << mce::ansi::reset << ": Choose option 1 and paste a path.\n"
            << "   - Windows (native): e.g. C:\\Users\\You\\Pictures\n"
            << "   - Linux/Mac:        e.g. /home/you/images\n"
            << "   - Docker Compose:   If your drive is mounted as /host/c, you can also paste a Windows path\n"
            << "                       (C:\\...) and the app will map it internally to /host/c/... automatically.\n"
            << "2) " << mce::ansi::info << "Settings" << mce::ansi::reset << ": Option 2. Toggle:\n"
            << "   - Debug logs (prints extra diagnostic info in the console)\n"
            << "   - Save debug overlays (writes *_debug_*.png files per image)\n"
            << "3) " << mce::ansi::info << "Run" << mce::ansi::reset << ": Option 5 to process and see results.\n\n"

            << mce::ansi::bold << "Outputs" << mce::ansi::reset << "\n"
            << "• CSV report:   " << mce::ansi::muted << "mce_output/results/<YYYYMMDD-HHMMSS>.csv" << mce::ansi::reset << "\n"
            << "• Debug images: " << mce::ansi::muted << "mce_output/debug/<YYYYMMDD-HHMMSS>/" << mce::ansi::reset << "\n"
            << "  (Set " << mce::ansi::bold << "MCE_OUTPUT_ROOT" << mce::ansi::reset
            << " to change the root output folder; in Docker you can point this to a host path.)\n\n"

            << mce::ansi::bold << "Supported formats" << mce::ansi::reset << "\n"
            << "• .png  .jpg  .jpeg\n\n"

            << mce::ansi::bold << "Tips to improve detection" << mce::ansi::reset << "\n"
            << "• Prefer images where the marker is fully visible and not extremely skewed.\n"
            << "• Ensure good contrast between the marker and the background.\n"
            << "• Try enabling debug overlays to review quad/warp/mask outputs and tune your input set if needed.\n\n"

            << mce::ansi::bold << "Troubleshooting" << mce::ansi::reset << "\n"
            << "• " << mce::ansi::warn << "Invalid path" << mce::ansi::reset << ": Path must exist inside the environment.\n"
            << "  - In Docker: mount your host folder (or whole drive) and use the mapped path (e.g., /host/c/...).\n"
            << "• " << mce::ansi::warn << "No marker found" << mce::ansi::reset << ": Check the debug images for the mask/edges.\n"
            << "  - Try clearer lighting, less glare, or a straighter shot of the marker.\n\n";

        wait_for_enter();
    }

    void about()
    {
        title("About");

        std::cout
            << mce::ansi::bold << "Marker Coverage Estimator (TUI)" << mce::ansi::reset << "\n"
            << "A colorful terminal UI for running the marker detection & coverage pipeline.\n\n"

            << mce::ansi::bold << "Highlights" << mce::ansi::reset << "\n"
            << "• Clean TUI: titles, colorized feedback, and simple menus.\n"
            << "• Batch processing of folders with progress and per-image results.\n"
            << "• Organized outputs: timestamped CSV + optional debug overlays.\n"
            << "• Docker-friendly: works the same on any machine with Docker/Compose.\n\n"

            << mce::ansi::bold << "Tech" << mce::ansi::reset << "\n"
            << "• C++17, CMake, Ninja\n"
            << "• OpenCV (core, imgproc, imgcodecs)\n"
            << "• Docker/Compose for reproducible builds and runs\n\n"

            << mce::ansi::bold << "Author" << mce::ansi::reset << "\n"
            << "• " << kAuthorName << " — CS student & junior network engineer; experience across\n"
            << "  software/systems, communications, and support/operations.\n"
            << "• Location: " << kAuthorLocation << "\n\n"

            << mce::ansi::bold << "Contact" << mce::ansi::reset << "\n"
            << "• Email:    " << kAuthorEmail << "\n"
            << "• LinkedIn: " << kAuthorLinkedIn << "\n"
            << "• GitHub:   " << kAuthorGitHub << "\n\n"

            << mce::ansi::muted
            << "(Set TERM=xterm-256color for best colors. Run via Docker Compose for easy path mapping.)"
            << mce::ansi::reset << "\n\n";

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
