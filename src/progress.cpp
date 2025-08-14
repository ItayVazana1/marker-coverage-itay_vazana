#include "progress.hpp"
#include "ansi.hpp"
#include "ui.hpp"
#include <chrono>
#include <iostream>
#include <thread>
#include <utility>
#include <vector>

namespace app::progress
{

    static void draw_bar(int width, double f)
    {
        const int filled = static_cast<int>(f * width);
        std::cout << "[";
        for (int i = 0; i < width; ++i)
            std::cout << (i < filled ? '#' : '-');
        std::cout << "] ";
        const int pct = static_cast<int>(f * 100.0 + 0.5);
        std::cout << pct << "%  ";
    }

    void simulate_step(const std::string &label, int msTotal, int barWidth)
    {
        using namespace std::chrono_literals;
        const int chunks = 42;
        for (int i = 0; i <= chunks; ++i)
        {
            const double f = static_cast<double>(i) / chunks;
            std::cout << "\r" << app::ansi::info << label << app::ansi::reset << "  ";
            draw_bar(barWidth, f);
            std::cout.flush();
            std::this_thread::sleep_for(std::chrono::milliseconds(msTotal / chunks));
        }
        std::cout << "\r" << app::ansi::ok << label << "  \xE2\x9C\x93 Done" << app::ansi::reset << "                         \n";
    }

    void process_pipeline(const std::string &inputPath, bool isDir)
    {
        app::ui::title("Process");
        std::cout << "Input: " << app::ansi::bold << inputPath << app::ansi::reset
                  << (isDir ? "  (directory)\n\n" : "  (file)\n\n");

        std::vector<std::pair<std::string, int>> steps = {
            {"Loading input", 800},
            {"Scanning files", 1000},
            {"Pre-processing images", 1200},
            {"Running main algorithm", 1800},
            {"Post-processing & summaries", 900},
            {"Writing results", 700}};

        for (auto &st : steps)
            simulate_step(st.first, st.second);

        std::cout << "\n"
                  << app::ansi::ok << "\x1b[1mAll steps completed successfully." << app::ansi::reset << "\n\n";
        app::ui::wait_for_enter();
    }

} // namespace app::progress
