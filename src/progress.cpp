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
        using clock = std::chrono::steady_clock;
        using std::chrono::duration_cast;
        using std::chrono::milliseconds;

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
        // NEW: add elapsed_ms column
        csv << "index,input_path,found,percent,debug_quad,debug_warp,debug_mask,elapsed_ms\n";

        const int N = static_cast<int>(images.size());
        std::cout << mce::ansi::title << "Running detection on " << N
                  << " image(s)" << mce::ansi::reset << "\n\n";
        std::cout << mce::ansi::muted << "Results CSV: " << csvPath.string()
                  << mce::ansi::reset << "\n";
        if (state.saveDebug)
            std::cout << mce::ansi::muted << "Debug dir : " << debugDir.string()
                      << mce::ansi::reset << "\n";
        std::cout << "\n";

        long long total_ms_accum = 0;
        int foundCount = 0;
        int i = 0;

        auto run_t0 = clock::now();

        for (const auto &path : images)
        {
            ++i;
            std::cout << mce::ansi::muted << "(" << i << "/" << N << ")"
                      << mce::ansi::reset << " Processing: " << path << "\n";

            auto t0 = clock::now();

            cv::Mat img = cv::imread(path, cv::IMREAD_COLOR);
            if (img.empty())
            {
                auto t1 = clock::now();
                long long ms = duration_cast<milliseconds>(t1 - t0).count();
                total_ms_accum += ms;

                std::cout << mce::ansi::err << "Failed to read image"
                          << mce::ansi::reset
                          << mce::ansi::muted << " [" << ms << " ms]"
                          << mce::ansi::reset << "\n";
                csv << i << "," << '"' << path << '"' << ",0,,,,," << ms << "\n";
                continue;
            }

            // Build debug base under our organized debug dir
            std::string prefix = std::to_string(i) + "_" + stem_of(path);
            fs::path debugBasePath = debugDir / prefix; // .../debug/<ts>/<i>_<name>
            std::string debugBase = debugBasePath.string();

            std::vector<cv::Point2f> quad;
            bool ok = mce::detect_marker_polygon(img, quad, state.debug, state.saveDebug, debugBase);

            int pct = -1;
            if (ok)
            {
                pct = mce::coverage_percent(quad, img.size());
                std::cout << path << " " << pct << "%\n";
                if (state.saveDebug)
                {
                    std::cout << mce::ansi::ok << "        Saved result."
                              << mce::ansi::reset << "\n";
                }
                ++foundCount;
            }
            else
            {
                std::cout << mce::ansi::warn << "No marker found"
                          << mce::ansi::reset << "\n";
            }

            auto t1 = clock::now();
            long long ms = duration_cast<milliseconds>(t1 - t0).count();
            total_ms_accum += ms;

            // show timing inline
            std::cout << mce::ansi::muted << "        [" << ms << " ms]"
                      << mce::ansi::reset << "\n";

            // write CSV row (with ms)
            if (!ok)
            {
                csv << i << "," << '"' << path << '"' << ",0,,,";
                if (state.saveDebug)
                {
                    csv << '"' << (debugBase + "_debug_mask.png") << '"';
                }
                else
                {
                    csv << "";
                }
                csv << "," << ms << "\n";
            }
            else
            {
                csv << i << "," << '"' << path << '"' << ",1," << pct << ",";
                if (state.saveDebug)
                {
                    csv << '"' << (debugBase + "_debug_quad.png") << '"' << ","
                        << '"' << (debugBase + "_debug_warp.png") << '"' << ","
                        << '"' << (debugBase + "_debug_mask.png") << '"';
                }
                else
                {
                    csv << ",,";
                }
                csv << "," << ms << "\n";
            }
        }

        auto run_t1 = clock::now();
        long long run_ms = duration_cast<milliseconds>(run_t1 - run_t0).count();
        double avg_ms = (N > 0) ? (double)total_ms_accum / (double)N : 0.0;
        double ips = (run_ms > 0) ? (1000.0 * (double)N / (double)run_ms) : 0.0;

        std::cout << "\n"
                  << mce::ansi::bold << "Found " << foundCount << "/"
                  << N << mce::ansi::reset << " images with a valid marker.\n"
                  << mce::ansi::muted
                  << "Total: " << run_ms << " ms, "
                  << "Avg: " << std::fixed << std::setprecision(1) << avg_ms << " ms/img, "
                  << std::setprecision(2) << ips << " img/s"
                  << mce::ansi::reset << "\n\n";
    }

} // namespace app::progress
