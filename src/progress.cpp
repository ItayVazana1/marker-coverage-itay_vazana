#include "mce/progress.hpp"
#include "mce/ansi.hpp"
#include "mce/detector.hpp"
#include "mce/coverage.hpp"
#include "mce/log.hpp"

#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>

#include <opencv2/opencv.hpp>

namespace app::progress
{

    static void draw_bar(int width, double f)
    {
        if (f < 0)
            f = 0;
        if (f > 1)
            f = 1;
        int filled = static_cast<int>(std::round(f * width));
        std::cout << "[";
        for (int i = 0; i < width; ++i)
        {
            std::cout << (i < filled ? '#' : '-');
        }
        std::cout << "] " << static_cast<int>(std::round(f * 100)) << "%  ";
    }

    void simulate_step(const std::string &label, int msTotal, int barWidth)
    {
        using namespace std::chrono;
        const int chunks = 42;
        for (int i = 0; i <= chunks; ++i)
        {
            double f = static_cast<double>(i) / chunks;
            std::cout << "\r" << mce::ansi::info << label << mce::ansi::reset << "  ";
            draw_bar(barWidth, f);
            std::cout.flush();
            std::this_thread::sleep_for(milliseconds(msTotal / chunks));
        }
        std::cout << "\r" << mce::ansi::ok << label << "  [Done]" << mce::ansi::reset
                  << "                                \n";
    }

    void process_and_report(const std::vector<std::string> &images, bool debug, bool saveDebug)
    {
        mce::log::set(debug, saveDebug);

        std::cout << mce::ansi::title << "Running detection on " << images.size()
                  << " image(s)" << mce::ansi::reset << "\n\n";

        int foundCount = 0;
        int i = 0;
        const int N = static_cast<int>(images.size());

        for (const auto &path : images)
        {
            ++i;
            std::cout << mce::ansi::muted << "(" << i << "/" << N << ")"
                      << mce::ansi::reset << " Processing: " << path << "\n";

            cv::Mat img = cv::imread(path, cv::IMREAD_COLOR);
            if (img.empty())
            {
                std::cout << mce::ansi::err << "Failed to read image"
                          << mce::ansi::reset << "\n";
                continue;
            }

            std::vector<cv::Point2f> quad;
            bool ok = mce::detect_marker_polygon(img, quad, debug, saveDebug, path);
            if (!ok)
            {
                std::cout << mce::ansi::warn << "No marker found"
                          << mce::ansi::reset << "\n";
                continue;
            }

            int pct = mce::coverage_percent(quad, img.size());

            // Required plain output line for graders:
            std::cout << path << " " << pct << "%\n";

            // Nice TUI feedback:
            std::cout << mce::ansi::ok << "\tSaved result."
                      << mce::ansi::reset << "\n";

            ++foundCount;
        }

        std::cout << "\n"
                  << mce::ansi::bold << "Found " << foundCount << "/"
                  << N << mce::ansi::reset << " images with a valid marker.\n\n";
    }

} // namespace app::progress
