#include "mce/progress.hpp"
#include "mce/ansi.hpp"
#include "mce/detector.hpp"
#include "mce/coverage.hpp"
#include "mce/log.hpp"

#include <opencv2/imgcodecs.hpp>

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace
{

    // decide output root
    fs::path resolve_output_root()
    {
        const char *env = std::getenv("MCE_OUTPUT_ROOT");
        if (env && *env)
            return fs::path(env);
        return fs::current_path() / "mce_output";
    }

    std::string now_stamp()
    {
        using clock = std::chrono::system_clock;
        auto t = clock::to_time_t(clock::now());
        std::tm tm{};
#if defined(_WIN32)
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y%m%d-%H%M%S");
        return oss.str();
    }

    void ensure_dir(const fs::path &p)
    {
        std::error_code ec;
        fs::create_directories(p, ec);
    }

    std::string stem_of(const fs::path &p)
    {
        return p.stem().string();
    }

} // namespace

namespace app::progress
{

    void process_and_report(const std::vector<std::string> &images,
                            const app::State &state)
    {
        mce::log::set(state.debug, state.saveDebug);

        const fs::path root = resolve_output_root();
        const std::string ts = now_stamp();
        const fs::path resultsDir = root / "results";
        const fs::path debugDir = root / "debug" / ts;

        ensure_dir(resultsDir);
        if (state.saveDebug)
            ensure_dir(debugDir);

        const fs::path csvPath = resultsDir / (ts + ".csv");
        std::ofstream csv(csvPath);
        csv << "index,input_path,found,percent,debug_quad,debug_warp,debug_mask\n";

        const int N = static_cast<int>(images.size());
        std::cout << mce::ansi::title << "Running detection on " << N
                  << " image(s)" << mce::ansi::reset << "\n\n";
        std::cout << mce::ansi::muted << "Results CSV: " << csvPath.string()
                  << mce::ansi::reset << "\n";
        if (state.saveDebug)
            std::cout << mce::ansi::muted << "Debug dir : " << debugDir.string()
                      << mce::ansi::reset << "\n";
        std::cout << "\n";

        int foundCount = 0;
        int i = 0;

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
                csv << i << "," << '"' << path << '"' << ",0,,,,\n";
                continue;
            }

            // Build debug base under our organized debug dir
            std::string prefix = std::to_string(i) + "_" + stem_of(path);
            fs::path debugBasePath = debugDir / prefix; // .../debug/<ts>/<i>_<name>
            std::string debugBase = debugBasePath.string();

            std::vector<cv::Point2f> quad;
            // <<< key change: DO NOT pass `path` here; pass our run-scoped debugBase
            bool ok = mce::detect_marker_polygon(img, quad, state.debug, state.saveDebug, debugBase);
            if (!ok)
            {
                std::cout << mce::ansi::warn << "No marker found"
                          << mce::ansi::reset << "\n";
                csv << i << "," << '"' << path << '"' << ",0,,,";
                if (state.saveDebug)
                {
                    csv << '"' << (debugBase + "_debug_mask.png") << '"';
                }
                csv << "\n";
                continue;
            }

            int pct = mce::coverage_percent(quad, img.size());

            // Required plain output line:
            std::cout << path << " " << pct << "%\n";
            if (state.saveDebug)
            {
                std::cout << mce::ansi::ok << "        Saved result."
                          << mce::ansi::reset << "\n";
            }
            ++foundCount;

            csv << i << "," << '"' << path << '"' << ",1," << pct << ",";
            if (state.saveDebug)
            {
                csv << '"' << (debugBase + "_debug_quad.png") << '"' << ","
                    << '"' << (debugBase + "_debug_warp.png") << '"' << ","
                    << '"' << (debugBase + "_debug_mask.png") << '"';
            }
            csv << "\n";
        }

        std::cout << "\n"
                  << mce::ansi::bold << "Found " << foundCount << "/"
                  << N << mce::ansi::reset << " images with a valid marker.\n\n";
    }

} // namespace app::progress
